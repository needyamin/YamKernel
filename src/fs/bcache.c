#include "bcache.h"
#include "../mem/heap.h"
#include "../lib/string.h"
#include "../lib/kprintf.h"

#define BCACHE_PAGE_SIZE 4096
#define BCACHE_MAX_PAGES 256 // 1MB cache for now

typedef struct {
    block_device_t *dev;
    u64 page_lba;     // Start LBA of this 4KB page
    u32 sectors;      // How many sectors in this page
    u64 last_use;     // LRU timestamp
    bool dirty;
    u8 data[BCACHE_PAGE_SIZE];
} bcache_entry_t;

static bcache_entry_t *g_bcache;
static u64 g_bcache_ticks = 0;

void bcache_init(void) {
    g_bcache = (bcache_entry_t *)kmalloc(sizeof(bcache_entry_t) * BCACHE_MAX_PAGES);
    memset(g_bcache, 0, sizeof(bcache_entry_t) * BCACHE_MAX_PAGES);
    kprintf_color(0xFF00FF88, "[BCACHE] Initialized LRU block cache (%d pages, %d KB)\n", BCACHE_MAX_PAGES, (BCACHE_MAX_PAGES * BCACHE_PAGE_SIZE) / 1024);
}

static bcache_entry_t *bcache_find(block_device_t *dev, u64 page_lba) {
    for (int i = 0; i < BCACHE_MAX_PAGES; i++) {
        if (g_bcache[i].dev == dev && g_bcache[i].page_lba == page_lba) {
            g_bcache[i].last_use = ++g_bcache_ticks;
            return &g_bcache[i];
        }
    }
    return NULL;
}

static void bcache_evict(int index) {
    bcache_entry_t *e = &g_bcache[index];
    if (e->dev && e->dirty) {
        if (e->dev->write) {
            e->dev->write(e->dev, e->page_lba, e->sectors, e->data);
        }
        e->dirty = false;
    }
    e->dev = NULL;
}

static bcache_entry_t *bcache_alloc(block_device_t *dev, u64 page_lba, u32 sectors) {
    int oldest_idx = 0;
    u64 oldest_time = (u64)-1;

    for (int i = 0; i < BCACHE_MAX_PAGES; i++) {
        if (!g_bcache[i].dev) {
            g_bcache[i].dev = dev;
            g_bcache[i].page_lba = page_lba;
            g_bcache[i].sectors = sectors;
            g_bcache[i].last_use = ++g_bcache_ticks;
            g_bcache[i].dirty = false;
            return &g_bcache[i];
        }
        if (g_bcache[i].last_use < oldest_time) {
            oldest_time = g_bcache[i].last_use;
            oldest_idx = i;
        }
    }

    bcache_evict(oldest_idx);
    g_bcache[oldest_idx].dev = dev;
    g_bcache[oldest_idx].page_lba = page_lba;
    g_bcache[oldest_idx].sectors = sectors;
    g_bcache[oldest_idx].last_use = ++g_bcache_ticks;
    g_bcache[oldest_idx].dirty = false;
    return &g_bcache[oldest_idx];
}

int bcache_read(block_device_t *dev, u64 lba, u32 count, void *buf) {
    if (!dev || !dev->read || !buf) return -1;
    u32 sectors_per_page = BCACHE_PAGE_SIZE / dev->sector_size;
    if (sectors_per_page == 0) sectors_per_page = 1;

    u8 *p = (u8 *)buf;
    u64 current_lba = lba;
    u32 remaining = count;

    while (remaining > 0) {
        u64 page_lba = (current_lba / sectors_per_page) * sectors_per_page;
        u32 offset_sectors = current_lba - page_lba;
        u32 chunk_sectors = sectors_per_page - offset_sectors;
        if (chunk_sectors > remaining) chunk_sectors = remaining;

        bcache_entry_t *e = bcache_find(dev, page_lba);
        if (!e) {
            e = bcache_alloc(dev, page_lba, sectors_per_page);
            if (dev->read(dev, page_lba, sectors_per_page, e->data) < 0) {
                e->dev = NULL;
                return -1;
            }
        }

        memcpy(p, e->data + offset_sectors * dev->sector_size, chunk_sectors * dev->sector_size);
        p += chunk_sectors * dev->sector_size;
        current_lba += chunk_sectors;
        remaining -= chunk_sectors;
    }
    return count;
}

int bcache_write(block_device_t *dev, u64 lba, u32 count, const void *buf) {
    if (!dev || !dev->write || !buf) return -1;
    u32 sectors_per_page = BCACHE_PAGE_SIZE / dev->sector_size;
    if (sectors_per_page == 0) sectors_per_page = 1;

    const u8 *p = (const u8 *)buf;
    u64 current_lba = lba;
    u32 remaining = count;

    while (remaining > 0) {
        u64 page_lba = (current_lba / sectors_per_page) * sectors_per_page;
        u32 offset_sectors = current_lba - page_lba;
        u32 chunk_sectors = sectors_per_page - offset_sectors;
        if (chunk_sectors > remaining) chunk_sectors = remaining;

        bcache_entry_t *e = bcache_find(dev, page_lba);
        if (!e) {
            e = bcache_alloc(dev, page_lba, sectors_per_page);
            if (chunk_sectors < sectors_per_page) {
                if (dev->read(dev, page_lba, sectors_per_page, e->data) < 0) {
                    e->dev = NULL;
                    return -1;
                }
            }
        }

        memcpy(e->data + offset_sectors * dev->sector_size, p, chunk_sectors * dev->sector_size);
        e->dirty = true;
        
        p += chunk_sectors * dev->sector_size;
        current_lba += chunk_sectors;
        remaining -= chunk_sectors;
    }
    return count;
}

void bcache_flush(block_device_t *dev) {
    if (!g_bcache) return;
    for (int i = 0; i < BCACHE_MAX_PAGES; i++) {
        bcache_entry_t *e = &g_bcache[i];
        if (e->dev && (dev == NULL || e->dev == dev) && e->dirty) {
            if (e->dev->write) {
                e->dev->write(e->dev, e->page_lba, e->sectors, e->data);
            }
            e->dirty = false;
        }
    }
    if (dev && dev->flush) {
        dev->flush(dev);
    }
}
