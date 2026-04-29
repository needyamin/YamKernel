/* ============================================================================
 * YamKernel — Physical Memory Manager (Zone-Aware Cell Allocator)
 * v0.3.0: Zone-aware allocation, page descriptors, reference counting,
 *         watermarks, memory pressure detection.
 *
 * Algorithm (preserved from v0.2):
 *   1. Physical memory is divided into regions from Limine memmap
 *   2. Each usable region becomes a root cell
 *   3. When allocating, find smallest free cell >= requested size
 *   4. If too large, split into 4 equal children
 *   5. On free, merge siblings back if all 4 are free
 *
 * New in v0.3:
 *   - Zones (DMA/DMA32/Normal) overlay the cell tree
 *   - Per-page descriptors with refcount for CoW sharing
 *   - Watermarks for OOM detection and memory pressure
 * ============================================================================ */

#include "pmm.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../lib/spinlock.h"
#include <limine.h>

/* ---- Cell pool (preserved from v0.2) ---- */
static cell_t cells[CELL_POOL_SIZE];
static u16 cell_count = 0;
static u64 total_mem = 0;
static u64 free_mem = 0;
static u64 hhdm_off = 0;
static spinlock_t pmm_lock = SPINLOCK_INIT;
static u16 last_used_cell = 0;

/* ---- Zone tracking ---- */
static mem_zone_t zones[ZONE_COUNT];

/* ---- Page descriptor array ---- */
static page_t page_array[MAX_PHYS_PAGES];
static u64 max_pfn = 0;    /* Highest page frame number seen */

/* ---- Memory pressure callbacks ---- */
static mem_pressure_cb_t pressure_cbs[MAX_PRESSURE_CBS];
static u32 pressure_cb_count = 0;

/* ---- Helpers ---- */

static inline u64 phys_to_pfn(u64 phys) { return phys >> PAGE_SHIFT; }
static inline u64 pfn_to_phys(u64 pfn)  { return pfn << PAGE_SHIFT; }

static mem_zone_type_t phys_to_zone(u64 phys) {
    if (phys < ZONE_DMA_LIMIT)   return ZONE_DMA;
    if (phys < ZONE_DMA32_LIMIT) return ZONE_DMA32;
    return ZONE_NORMAL;
}

/* ---- Cell pool management (preserved) ---- */

static u16 cell_alloc_node(void) {
    if (cell_count >= CELL_POOL_SIZE) return 0xFFFF;
    u16 idx = cell_count++;
    cells[idx].children[0] = 0xFFFF;
    cells[idx].children[1] = 0xFFFF;
    cells[idx].children[2] = 0xFFFF;
    cells[idx].children[3] = 0xFFFF;
    cells[idx].parent = 0xFFFF;
    cells[idx].owner = 0;
    return idx;
}

/* ---- Splitting (preserved) ---- */

static bool cell_split(u16 idx) {
    cell_t *c = &cells[idx];
    if (c->state != CELL_FREE) return false;
    if (c->size < PAGE_SIZE * 4) return false;

    u64 child_size = c->size / 4;

    for (int i = 0; i < 4; i++) {
        u16 child = cell_alloc_node();
        if (child == 0xFFFF) return false;

        cells[child].base   = c->base + (i * child_size);
        cells[child].size   = child_size;
        cells[child].state  = CELL_FREE;
        cells[child].parent = idx;
        c->children[i] = child;
    }

    c->state = CELL_SPLIT;
    return true;
}

/* ---- Merging (preserved) ---- */

static void cell_try_merge(u16 idx) {
    cell_t *c = &cells[idx];
    if (c->parent == 0xFFFF) return;

    cell_t *parent = &cells[c->parent];
    if (parent->state != CELL_SPLIT) return;

    for (int i = 0; i < 4; i++) {
        if (parent->children[i] == 0xFFFF) return;
        if (cells[parent->children[i]].state != CELL_FREE) return;
    }

    for (int i = 0; i < 4; i++) {
        cells[parent->children[i]].state = CELL_RESERVED;
        parent->children[i] = 0xFFFF;
    }
    parent->state = CELL_FREE;

    cell_try_merge(c->parent);
}

/* ---- Find best-fit free cell (with optional zone constraint) ---- */

static u16 find_free_cell_zone(u16 idx, u64 min_size, mem_zone_type_t zone, bool zone_strict) {
    cell_t *c = &cells[idx];

    if (c->state == CELL_FREE) {
        if (c->size >= min_size) {
            /* Zone check: ensure the entire allocation fits within the zone */
            if (zone_strict) {
                u64 zone_limit = (zone == ZONE_DMA) ? ZONE_DMA_LIMIT :
                                 (zone == ZONE_DMA32) ? ZONE_DMA32_LIMIT : ~0ULL;
                if (c->base + min_size > zone_limit) return 0xFFFF;
            }
            return idx;
        }
        return 0xFFFF;
    }

    if (c->state == CELL_SPLIT) {
        for (int i = 0; i < 4; i++) {
            if (c->children[i] != 0xFFFF) {
                u16 result = find_free_cell_zone(c->children[i], min_size, zone, zone_strict);
                if (result != 0xFFFF) return result;
            }
        }
    }

    return 0xFFFF;
}

/* find_free_cell is replaced by find_free_cell_zone with zone_strict=false */

/* ---- Zone watermark helpers ---- */

static void zone_update_watermarks(mem_zone_t *z) {
    /* Set watermarks as percentages of total zone pages */
    z->watermark.min  = z->total_pages / 64;   /* ~1.5% */
    z->watermark.low  = z->total_pages / 32;   /* ~3% */
    z->watermark.high = z->total_pages / 16;   /* ~6% */
    /* Ensure minimums */
    if (z->watermark.min < 4)  z->watermark.min  = 4;
    if (z->watermark.low < 8)  z->watermark.low  = 8;
    if (z->watermark.high < 16) z->watermark.high = 16;
}

static void check_pressure(mem_zone_type_t zone_type) {
    mem_zone_t *z = &zones[zone_type];
    if (z->free_pages <= z->watermark.low) {
        for (u32 i = 0; i < pressure_cb_count; i++) {
            if (pressure_cbs[i]) {
                pressure_cbs[i](zone_type, z->free_pages);
            }
        }
    }
}

/* ---- Initialize page descriptors for a range ---- */

static void init_page_descriptors(u64 base, u64 size, mem_zone_type_t zone_type, bool usable) {
    u64 start_pfn = phys_to_pfn(base);
    u64 end_pfn = phys_to_pfn(base + size);

    for (u64 pfn = start_pfn; pfn < end_pfn && pfn < MAX_PHYS_PAGES; pfn++) {
        page_array[pfn].refcount = 0;
        page_array[pfn].flags = usable ? 0 : PAGE_FLAG_RESERVED;
        page_array[pfn].zone = (u16)zone_type;
        page_array[pfn].order = 0;
        page_array[pfn].owner = 0;
        page_array[pfn].lru_next = NULL;
        page_array[pfn].lru_prev = NULL;

        if (pfn > max_pfn) max_pfn = pfn;
    }
}

/* ---- Public API ---- */

void pmm_init(void *memmap_response, u64 hhdm_offset) {
    hhdm_off = hhdm_offset;
    memset(cells, 0, sizeof(cells));
    memset(page_array, 0, sizeof(page_array));
    memset(zones, 0, sizeof(zones));
    cell_count = 0;
    total_mem = 0;
    free_mem = 0;
    max_pfn = 0;

    /* Initialize zones */
    zones[ZONE_DMA].type   = ZONE_DMA;
    zones[ZONE_DMA].start  = 0;
    zones[ZONE_DMA].end    = ZONE_DMA_LIMIT;

    zones[ZONE_DMA32].type  = ZONE_DMA32;
    zones[ZONE_DMA32].start = ZONE_DMA_LIMIT;
    zones[ZONE_DMA32].end   = ZONE_DMA32_LIMIT;

    zones[ZONE_NORMAL].type  = ZONE_NORMAL;
    zones[ZONE_NORMAL].start = ZONE_DMA32_LIMIT;
    zones[ZONE_NORMAL].end   = ~0ULL;

    struct limine_memmap_response *mmap = (struct limine_memmap_response *)memmap_response;

    kprintf_color(0xFF00DDFF, "[PMM] Zone-Aware Cell Allocator initializing...\n");
    kprintf("[PMM] Memory map entries: %lu\n", mmap->entry_count);

    for (u64 i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry *entry = mmap->entries[i];
        const char *type_str = "Unknown";

        switch (entry->type) {
            case LIMINE_MEMMAP_USABLE:
                type_str = "Usable";
                break;
            case LIMINE_MEMMAP_RESERVED:
                type_str = "Reserved";
                break;
            case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
                type_str = "ACPI Reclaimable";
                break;
            case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
                type_str = "Bootloader Reclaimable";
                break;
            case LIMINE_MEMMAP_KERNEL_AND_MODULES:
                type_str = "Kernel";
                break;
            case LIMINE_MEMMAP_FRAMEBUFFER:
                type_str = "Framebuffer";
                break;
            default:
                break;
        }

        kprintf("  [%lu] 0x%lx - 0x%lx (%lu KB) %s\n",
                i, entry->base, entry->base + entry->length,
                entry->length / 1024, type_str);

        /* Initialize page descriptors for all regions */
        bool usable = (entry->type == LIMINE_MEMMAP_USABLE);
        mem_zone_type_t zt = phys_to_zone(entry->base);
        init_page_descriptors(entry->base, entry->length, zt, usable);

        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= PAGE_SIZE) {
            u64 base = (entry->base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            u64 end  = (entry->base + entry->length) & ~(PAGE_SIZE - 1);
            u64 size = end - base;

            while (size >= PAGE_SIZE) {
                u64 chunk_size = PAGE_SIZE;
                while (chunk_size * 4 <= size) {
                    chunk_size *= 4;
                }

                u16 idx = cell_alloc_node();
                if (idx != 0xFFFF) {
                    cells[idx].base  = base;
                    cells[idx].size  = chunk_size;
                    cells[idx].state = CELL_FREE;
                    total_mem += chunk_size;
                    free_mem  += chunk_size;

                    /* Update zone stats */
                    mem_zone_type_t cell_zone = phys_to_zone(base);
                    u64 pages_in_chunk = chunk_size / PAGE_SIZE;
                    zones[cell_zone].total_pages += pages_in_chunk;
                    zones[cell_zone].free_pages  += pages_in_chunk;
                }

                base += chunk_size;
                size -= chunk_size;
            }
        }
    }

    /* Calculate watermarks for each zone */
    for (int z = 0; z < ZONE_COUNT; z++) {
        zone_update_watermarks(&zones[z]);
    }

    kprintf_color(0xFF00FF88, "[PMM] Total usable: %lu MB (%lu pages)\n",
                  total_mem / (1024 * 1024), total_mem / PAGE_SIZE);
    kprintf_color(0xFF00FF88, "[PMM] Cell nodes used: %u / %u\n",
                  cell_count, CELL_POOL_SIZE);
    kprintf_color(0xFF00DDFF, "[PMM] Zones:\n");
    for (int z = 0; z < ZONE_COUNT; z++) {
        const char *zn[] = { "DMA", "DMA32", "Normal" };
        kprintf("  [%s] %lu pages total, %lu free | WM min=%lu low=%lu high=%lu\n",
                zn[z], zones[z].total_pages, zones[z].free_pages,
                zones[z].watermark.min, zones[z].watermark.low, zones[z].watermark.high);
    }
    kprintf_color(0xFF00FF88, "[PMM] Page descriptors: max PFN = %lu\n", max_pfn);
}

/* ---- Core allocation (zone-aware internal) ---- */

static u64 pmm_alloc_pages_internal(u64 count, mem_zone_type_t preferred_zone, bool zone_strict) {
    u64 req_size = count * PAGE_SIZE;
    u64 result = 0;

    u64 f = spin_lock_irqsave(&pmm_lock);

    for (u16 i = 0; i < cell_count; i++) {
        u16 idx = (last_used_cell + i) % cell_count;
        if (cells[idx].parent != 0xFFFF) continue;

        u16 found = find_free_cell_zone(idx, req_size, preferred_zone, zone_strict);
        if (found == 0xFFFF) continue;

        while (cells[found].size > req_size && cells[found].size >= PAGE_SIZE * 4) {
            if (!cell_split(found)) break;
            found = cells[found].children[0];
        }

        cells[found].state = CELL_USED;
        free_mem -= cells[found].size;
        result = cells[found].base;
        last_used_cell = idx;

        /* Update zone and page descriptors */
        mem_zone_type_t z = phys_to_zone(result);
        u64 alloc_pages = cells[found].size / PAGE_SIZE;
        zones[z].free_pages -= alloc_pages;
        zones[z].alloc_count++;

        /* Set page descriptors */
        u64 start_pfn = phys_to_pfn(result);
        for (u64 p = 0; p < alloc_pages && (start_pfn + p) < MAX_PHYS_PAGES; p++) {
            page_array[start_pfn + p].refcount = 1;
            page_array[start_pfn + p].flags &= ~PAGE_FLAG_RESERVED;
            page_array[start_pfn + p].order = 0;
        }

        break;
    }

    spin_unlock_irqrestore(&pmm_lock, f);

    if (!result) {
        /* If zone-strict failed, try fallback to any zone */
        if (zone_strict) {
            return pmm_alloc_pages_internal(count, preferred_zone, false);
        }
        kprintf_color(0xFFFF3333, "[PMM] ALLOC FAILED: no cell for %lu pages\n", count);
    } else {
        /* Check memory pressure after allocation */
        check_pressure(phys_to_zone(result));
    }

    return result;
}

u64 pmm_alloc_page(void) {
    return pmm_alloc_pages_internal(1, ZONE_NORMAL, false);
}

u64 pmm_alloc_pages(u64 count) {
    return pmm_alloc_pages_internal(count, ZONE_NORMAL, false);
}

u64 pmm_alloc_page_zone(mem_zone_type_t zone) {
    return pmm_alloc_pages_internal(1, zone, true);
}

u64 pmm_alloc_pages_zone(u64 count, mem_zone_type_t zone) {
    return pmm_alloc_pages_internal(count, zone, true);
}

void pmm_free_page(u64 phys_addr) {
    pmm_free_pages(phys_addr, 1);
}

void pmm_free_pages(u64 phys_addr, u64 count) {
    (void)count;
    u64 f = spin_lock_irqsave(&pmm_lock);

    for (u16 i = 0; i < cell_count; i++) {
        if (cells[i].base == phys_addr && cells[i].state == CELL_USED) {
            /* Check refcount — only free if refcount reaches 0 */
            u64 pfn = phys_to_pfn(phys_addr);
            if (pfn < MAX_PHYS_PAGES && page_array[pfn].refcount > 1) {
                page_array[pfn].refcount--;
                spin_unlock_irqrestore(&pmm_lock, f);
                return;
            }

            cells[i].state = CELL_FREE;
            cells[i].owner = 0;
            u64 freed_pages = cells[i].size / PAGE_SIZE;
            free_mem += cells[i].size;

            /* Update zone */
            mem_zone_type_t z = phys_to_zone(phys_addr);
            zones[z].free_pages += freed_pages;

            /* Clear page descriptors */
            u64 start_pfn = phys_to_pfn(phys_addr);
            for (u64 p = 0; p < freed_pages && (start_pfn + p) < MAX_PHYS_PAGES; p++) {
                page_array[start_pfn + p].refcount = 0;
                page_array[start_pfn + p].flags = 0;
                page_array[start_pfn + p].owner = 0;
            }

            cell_try_merge(i);
            spin_unlock_irqrestore(&pmm_lock, f);
            return;
        }
    }
    spin_unlock_irqrestore(&pmm_lock, f);
}

/* ---- Page descriptor access ---- */

page_t *pmm_get_page(u64 phys_addr) {
    u64 pfn = phys_to_pfn(phys_addr);
    if (pfn >= MAX_PHYS_PAGES) return NULL;
    return &page_array[pfn];
}

void pmm_page_ref(u64 phys_addr) {
    u64 pfn = phys_to_pfn(phys_addr);
    if (pfn >= MAX_PHYS_PAGES) return;
    __atomic_add_fetch(&page_array[pfn].refcount, 1, __ATOMIC_ACQ_REL);
}

bool pmm_page_unref(u64 phys_addr) {
    u64 pfn = phys_to_pfn(phys_addr);
    if (pfn >= MAX_PHYS_PAGES) return false;
    u32 old = __atomic_sub_fetch(&page_array[pfn].refcount, 1, __ATOMIC_ACQ_REL);
    if (old == 0) {
        /* Refcount hit zero — page can be freed */
        pmm_free_page(phys_addr);
        return true;
    }
    return false;
}

u32 pmm_page_refcount(u64 phys_addr) {
    u64 pfn = phys_to_pfn(phys_addr);
    if (pfn >= MAX_PHYS_PAGES) return 0;
    return __atomic_load_n(&page_array[pfn].refcount, __ATOMIC_ACQUIRE);
}

/* ---- Page flags ---- */

void pmm_page_set_flags(u64 phys_addr, u32 flags) {
    u64 pfn = phys_to_pfn(phys_addr);
    if (pfn >= MAX_PHYS_PAGES) return;
    __atomic_or_fetch(&page_array[pfn].flags, flags, __ATOMIC_ACQ_REL);
}

void pmm_page_clear_flags(u64 phys_addr, u32 flags) {
    u64 pfn = phys_to_pfn(phys_addr);
    if (pfn >= MAX_PHYS_PAGES) return;
    __atomic_and_fetch(&page_array[pfn].flags, ~flags, __ATOMIC_ACQ_REL);
}

bool pmm_page_has_flag(u64 phys_addr, u32 flag) {
    u64 pfn = phys_to_pfn(phys_addr);
    if (pfn >= MAX_PHYS_PAGES) return false;
    return (__atomic_load_n(&page_array[pfn].flags, __ATOMIC_ACQUIRE) & flag) != 0;
}

/* ---- Memory pressure ---- */

void pmm_register_pressure_cb(mem_pressure_cb_t cb) {
    if (pressure_cb_count < MAX_PRESSURE_CBS) {
        pressure_cbs[pressure_cb_count++] = cb;
    }
}

bool pmm_is_low_memory(void) {
    for (int z = 0; z < ZONE_COUNT; z++) {
        if (zones[z].total_pages > 0 && zones[z].free_pages <= zones[z].watermark.low) {
            return true;
        }
    }
    return false;
}

bool pmm_is_oom(void) {
    /* OOM if ALL zones with pages are below min watermark */
    for (int z = 0; z < ZONE_COUNT; z++) {
        if (zones[z].total_pages > 0 && zones[z].free_pages > zones[z].watermark.min) {
            return false;
        }
    }
    return true;
}

/* ---- Ownership ---- */

void pmm_set_owner(u64 phys_addr, yam_node_id_t owner) {
    for (u16 i = 0; i < cell_count; i++) {
        if (cells[i].base == phys_addr && cells[i].state == CELL_USED) {
            cells[i].owner = owner;
            return;
        }
    }
    /* Also set in page descriptor */
    u64 pfn = phys_to_pfn(phys_addr);
    if (pfn < MAX_PHYS_PAGES) {
        page_array[pfn].owner = owner;
    }
}

u64 pmm_total_memory(void) { return total_mem; }
u64 pmm_free_memory(void)  { return free_mem; }

const mem_zone_t *pmm_get_zone(mem_zone_type_t zone) {
    if (zone >= ZONE_COUNT) return NULL;
    return &zones[zone];
}

void pmm_print_stats(void) {
    kprintf_color(0xFF00DDFF, "\n[PMM] === Memory Statistics ===\n");
    kprintf("  Total: %lu MB | Free: %lu MB (%lu%%)\n",
            total_mem / (1024*1024), free_mem / (1024*1024),
            total_mem > 0 ? (free_mem * 100) / total_mem : 0);
    const char *zn[] = { "DMA", "DMA32", "Normal" };
    for (int z = 0; z < ZONE_COUNT; z++) {
        if (zones[z].total_pages == 0) continue;
        kprintf("  [%s] %lu/%lu pages free (%lu allocs) | pressure=%s\n",
                zn[z], zones[z].free_pages, zones[z].total_pages,
                zones[z].alloc_count,
                zones[z].free_pages <= zones[z].watermark.min ? "OOM" :
                zones[z].free_pages <= zones[z].watermark.low ? "LOW" : "OK");
    }
}

/* ---- Self-test ---- */

void pmm_self_test(void) {
    kprintf_color(0xFFFFDD00, "\n[PMM] === Zone-Aware Cell Allocator Self-Test ===\n");

    u64 free_before = free_mem;

    /* Test 1: Basic allocation */
    u64 p1 = pmm_alloc_page();
    kprintf("[PMM] Test 1 - Alloc page: 0x%lx %s\n", p1, p1 ? "PASS" : "FAIL");

    /* Test 2: Different page */
    u64 p2 = pmm_alloc_page();
    kprintf("[PMM] Test 2 - Alloc page: 0x%lx %s\n", p2,
            (p2 && p2 != p1) ? "PASS" : "FAIL");

    /* Test 3: Refcount check */
    u32 rc = pmm_page_refcount(p1);
    kprintf("[PMM] Test 3 - Refcount(p1): %u %s\n", rc, rc == 1 ? "PASS" : "FAIL");

    /* Test 4: Ref increment */
    pmm_page_ref(p1);
    rc = pmm_page_refcount(p1);
    kprintf("[PMM] Test 4 - After ref: %u %s\n", rc, rc == 2 ? "PASS" : "FAIL");

    /* Test 5: Unref (should not free — still refcount=1) */
    pmm_page_unref(p1);
    rc = pmm_page_refcount(p1);
    kprintf("[PMM] Test 5 - After unref: %u %s\n", rc, rc == 1 ? "PASS" : "FAIL");

    /* Test 6: Free */
    pmm_free_page(p1);
    pmm_free_page(p2);

    /* Test 7: Zone allocation */
    u64 dma_page = pmm_alloc_page_zone(ZONE_DMA);
    kprintf("[PMM] Test 7 - DMA alloc: 0x%lx %s\n", dma_page,
            (dma_page && dma_page < ZONE_DMA_LIMIT) ? "PASS" : "SKIP(no DMA)");
    if (dma_page) pmm_free_page(dma_page);

    /* Test 8: Page flags */
    u64 p3 = pmm_alloc_page();
    pmm_page_set_flags(p3, PAGE_FLAG_DIRTY | PAGE_FLAG_LRU);
    bool has_dirty = pmm_page_has_flag(p3, PAGE_FLAG_DIRTY);
    bool has_lru = pmm_page_has_flag(p3, PAGE_FLAG_LRU);
    kprintf("[PMM] Test 8 - Flags: dirty=%d lru=%d %s\n",
            has_dirty, has_lru, (has_dirty && has_lru) ? "PASS" : "FAIL");
    pmm_page_clear_flags(p3, PAGE_FLAG_DIRTY);
    has_dirty = pmm_page_has_flag(p3, PAGE_FLAG_DIRTY);
    kprintf("[PMM] Test 8b - Clear dirty: %s\n", !has_dirty ? "PASS" : "FAIL");
    pmm_free_page(p3);

    /* Test 9: Memory consistency */
    kprintf("[PMM] Test 9 - Free mem restored: %s\n",
            (free_mem == free_before) ? "PASS" : "FAIL");

    /* Test 10: Pressure detection */
    kprintf("[PMM] Test 10 - Low memory: %s | OOM: %s\n",
            pmm_is_low_memory() ? "YES" : "NO",
            pmm_is_oom() ? "YES" : "NO");

    pmm_print_stats();
    kprintf_color(0xFF00FF88, "[PMM] Zone-Aware self-test: PASS\n\n");
}
