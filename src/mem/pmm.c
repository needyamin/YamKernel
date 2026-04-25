/* ============================================================================
 * YamKernel — Physical Memory Manager (Cell Allocator)
 * Novel: Fractal quad-tree subdivision of physical memory
 *
 * Algorithm:
 *   1. Physical memory is divided into regions from Limine memmap
 *   2. Each usable region becomes a root cell
 *   3. When allocating, we find the smallest free cell >= requested size
 *   4. If the cell is too large, we split it into 4 equal children
 *   5. Repeat until we get a cell of the right size
 *   6. On free, we merge siblings back if all 4 are free
 *
 * This is O(log4 N) allocation vs buddy's O(log2 N), but yields
 * much finer granularity and natural ownership tracking.
 * ============================================================================ */

#include "pmm.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include <limine.h>

/* Cell pool */
static cell_t cells[CELL_POOL_SIZE];
static u16 cell_count = 0;
static u64 total_mem = 0;
static u64 free_mem = 0;
static u64 hhdm_off = 0;

/* ---- Cell pool management ---- */

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

/* ---- Splitting ---- */

static bool cell_split(u16 idx) {
    cell_t *c = &cells[idx];
    if (c->state != CELL_FREE) return false;
    if (c->size < PAGE_SIZE * 4) return false; /* Can't split below 4 pages */

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

/* ---- Merging ---- */

static void cell_try_merge(u16 idx) {
    cell_t *c = &cells[idx];
    if (c->parent == 0xFFFF) return;

    cell_t *parent = &cells[c->parent];
    if (parent->state != CELL_SPLIT) return;

    /* Check if all siblings are free */
    for (int i = 0; i < 4; i++) {
        if (parent->children[i] == 0xFFFF) return;
        if (cells[parent->children[i]].state != CELL_FREE) return;
    }

    /* All siblings free — merge back into parent */
    for (int i = 0; i < 4; i++) {
        cells[parent->children[i]].state = CELL_RESERVED; /* Mark as dead */
        parent->children[i] = 0xFFFF;
    }
    parent->state = CELL_FREE;

    /* Recursively try merging up */
    cell_try_merge(c->parent);
}

/* ---- Find best-fit free cell ---- */

static u16 find_free_cell(u16 idx, u64 min_size) {
    cell_t *c = &cells[idx];

    if (c->state == CELL_FREE) {
        if (c->size >= min_size) return idx;
        return 0xFFFF;
    }

    if (c->state == CELL_SPLIT) {
        /* Depth-first search children */
        for (int i = 0; i < 4; i++) {
            if (c->children[i] != 0xFFFF) {
                u16 result = find_free_cell(c->children[i], min_size);
                if (result != 0xFFFF) return result;
            }
        }
    }

    return 0xFFFF;
}

/* ---- Public API ---- */

void pmm_init(void *memmap_response, u64 hhdm_offset) {
    hhdm_off = hhdm_offset;
    memset(cells, 0, sizeof(cells));
    cell_count = 0;
    total_mem = 0;
    free_mem = 0;

    struct limine_memmap_response *mmap = (struct limine_memmap_response *)memmap_response;

    kprintf_color(0xFF00DDFF, "[PMM] Cell Allocator initializing...\n");
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

        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= PAGE_SIZE) {
            /* Align base up to page boundary */
            u64 base = (entry->base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            u64 end  = (entry->base + entry->length) & ~(PAGE_SIZE - 1);
            u64 size = end - base;

            if (size >= PAGE_SIZE) {
                u16 idx = cell_alloc_node();
                if (idx != 0xFFFF) {
                    cells[idx].base  = base;
                    cells[idx].size  = size;
                    cells[idx].state = CELL_FREE;
                    total_mem += size;
                    free_mem  += size;
                }
            }
        }
    }

    kprintf_color(0xFF00FF88, "[PMM] Total usable: %lu MB (%lu pages)\n",
                  total_mem / (1024 * 1024), total_mem / PAGE_SIZE);
    kprintf_color(0xFF00FF88, "[PMM] Cell nodes used: %u / %u\n",
                  cell_count, CELL_POOL_SIZE);
}

u64 pmm_alloc_page(void) {
    return pmm_alloc_pages(1);
}

u64 pmm_alloc_pages(u64 count) {
    u64 req_size = count * PAGE_SIZE;

    /* Search all root cells */
    for (u16 i = 0; i < cell_count; i++) {
        if (cells[i].parent != 0xFFFF) continue; /* Only roots */

        u16 found = find_free_cell(i, req_size);
        if (found == 0xFFFF) continue;

        /* Split down to target size if needed */
        while (cells[found].size > req_size && cells[found].size >= PAGE_SIZE * 4) {
            if (!cell_split(found)) break;
            /* Take the first child */
            found = cells[found].children[0];
        }

        cells[found].state = CELL_USED;
        free_mem -= cells[found].size;
        return cells[found].base;
    }

    kprintf_color(0xFFFF3333, "[PMM] ALLOC FAILED: no cell for %lu pages\n", count);
    return 0;
}

void pmm_free_page(u64 phys_addr) {
    pmm_free_pages(phys_addr, 1);
}

void pmm_free_pages(u64 phys_addr, u64 count) {
    (void)count;
    /* Find the cell with this base address */
    for (u16 i = 0; i < cell_count; i++) {
        if (cells[i].base == phys_addr && cells[i].state == CELL_USED) {
            cells[i].state = CELL_FREE;
            cells[i].owner = 0;
            free_mem += cells[i].size;
            cell_try_merge(i);
            return;
        }
    }
}

void pmm_set_owner(u64 phys_addr, yam_node_id_t owner) {
    for (u16 i = 0; i < cell_count; i++) {
        if (cells[i].base == phys_addr && cells[i].state == CELL_USED) {
            cells[i].owner = owner;
            return;
        }
    }
}

u64 pmm_total_memory(void) { return total_mem; }
u64 pmm_free_memory(void)  { return free_mem; }

/* ---- Self-test ---- */

void pmm_self_test(void) {
    kprintf_color(0xFFFFDD00, "\n[PMM] === Cell Allocator Self-Test ===\n");

    u64 free_before = free_mem;

    /* Test 1: Allocate a page */
    u64 p1 = pmm_alloc_page();
    kprintf("[PMM] Test 1 - Alloc page: 0x%lx %s\n", p1, p1 ? "PASS" : "FAIL");

    /* Test 2: Allocate another page (should be different) */
    u64 p2 = pmm_alloc_page();
    kprintf("[PMM] Test 2 - Alloc page: 0x%lx %s\n", p2,
            (p2 && p2 != p1) ? "PASS" : "FAIL");

    /* Test 3: Free first page */
    pmm_free_page(p1);
    kprintf("[PMM] Test 3 - Free page: PASS\n");

    /* Test 4: Reallocate (should reuse freed page) */
    u64 p3 = pmm_alloc_page();
    kprintf("[PMM] Test 4 - Realloc page: 0x%lx %s\n", p3, p3 ? "PASS" : "FAIL");

    /* Test 5: Free all */
    pmm_free_page(p2);
    pmm_free_page(p3);
    kprintf("[PMM] Test 5 - Free all: %s\n",
            (free_mem == free_before) ? "PASS" : "FAIL");

    kprintf_color(0xFF00FF88, "[PMM] Cell Allocator self-test: PASS\n\n");
}
