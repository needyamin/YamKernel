/* ============================================================================
 * YamKernel — Physical Memory Manager (Zone-Aware Cell Allocator)
 * v0.3.0: Zone awareness, page descriptors, reference counting, watermarks
 *
 * Novel: Fractal quad-tree subdivision with Linux-inspired zone overlay
 * ============================================================================ */

#ifndef _MEM_PMM_H
#define _MEM_PMM_H

#include <nexus/types.h>

/* Page size = 4KB */
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

/* ---- Memory Zones (Linux-inspired) ---- */
typedef enum {
    ZONE_DMA     = 0,   /* 0 – 16 MB:  ISA DMA-capable memory */
    ZONE_DMA32   = 1,   /* 0 – 4 GB:   32-bit DMA-capable memory */
    ZONE_NORMAL  = 2,   /* 4 GB+:      General-purpose memory */
    ZONE_COUNT   = 3
} mem_zone_type_t;

#define ZONE_DMA_LIMIT      0x1000000ULL     /* 16 MB */
#define ZONE_DMA32_LIMIT    0x100000000ULL   /* 4 GB */

/* Watermark levels for memory pressure */
typedef struct {
    u64 min;    /* Below this: OOM territory */
    u64 low;    /* Below this: start reclaiming */
    u64 high;   /* Above this: all clear */
} watermark_t;

typedef struct {
    mem_zone_type_t type;
    u64             start;          /* Zone physical start */
    u64             end;            /* Zone physical end */
    u64             total_pages;    /* Total pages in zone */
    u64             free_pages;     /* Free pages in zone */
    u64             alloc_count;    /* Total allocations from this zone */
    watermark_t     watermark;      /* Pressure thresholds (in pages) */
} mem_zone_t;

/* ---- Page Descriptor (per physical page frame) ---- */
#define PAGE_FLAG_RESERVED  (1 << 0)  /* Firmware/MMIO — don't touch */
#define PAGE_FLAG_SLAB      (1 << 1)  /* Belongs to slab allocator */
#define PAGE_FLAG_COMPOUND  (1 << 2)  /* Part of a compound (huge) page */
#define PAGE_FLAG_LRU       (1 << 3)  /* On an LRU list */
#define PAGE_FLAG_DIRTY     (1 << 4)  /* Page has been written to */
#define PAGE_FLAG_LOCKED    (1 << 5)  /* Page is pinned (no swap/reclaim) */
#define PAGE_FLAG_COW       (1 << 6)  /* Copy-on-Write shared page */
#define PAGE_FLAG_AI_PINNED (1 << 7)  /* Pinned for AI/DMA tensor use */

#define MAX_PHYS_PAGES  (1024 * 1024)  /* Support up to 4 GB (1M pages) */

typedef struct page {
    u32             refcount;       /* Reference count (atomic) */
    u32             flags;          /* PAGE_FLAG_* bitmask */
    u16             zone;           /* Which zone this page belongs to */
    u16             order;          /* Allocation order (0 = single page) */
    yam_node_id_t   owner;          /* Owning YamGraph node (0 = kernel) */
    struct page    *lru_next;       /* LRU list forward */
    struct page    *lru_prev;       /* LRU list backward */
} page_t;

/* ---- Cell Allocator (preserved from v0.2) ---- */
typedef enum {
    CELL_FREE    = 0,
    CELL_SPLIT   = 1,
    CELL_USED    = 2,
    CELL_RESERVED= 3,
} cell_state_t;

#define CELL_POOL_SIZE 16384

typedef struct cell {
    u64           base;
    u64           size;
    cell_state_t  state;
    yam_node_id_t owner;
    u16           parent;
    u16           children[4];
} cell_t;

/* ---- Memory pressure callback ---- */
typedef void (*mem_pressure_cb_t)(mem_zone_type_t zone, u64 free_pages);
#define MAX_PRESSURE_CBS 8

/* ---- Public API ---- */

/* Initialize PMM from Limine memory map */
void pmm_init(void *memmap_response, u64 hhdm_offset);

/* Basic allocation (backward-compatible) */
u64  pmm_alloc_page(void);
u64  pmm_alloc_pages(u64 count);
void pmm_free_page(u64 phys_addr);
void pmm_free_pages(u64 phys_addr, u64 count);

/* Zone-aware allocation */
u64  pmm_alloc_page_zone(mem_zone_type_t zone);
u64  pmm_alloc_pages_zone(u64 count, mem_zone_type_t zone);

/* Page descriptor access */
page_t *pmm_get_page(u64 phys_addr);        /* Get page_t for phys addr */
void    pmm_page_ref(u64 phys_addr);         /* Increment refcount */
bool    pmm_page_unref(u64 phys_addr);       /* Decrement; returns true if freed */
u32     pmm_page_refcount(u64 phys_addr);    /* Query refcount */

/* Page flags */
void    pmm_page_set_flags(u64 phys_addr, u32 flags);
void    pmm_page_clear_flags(u64 phys_addr, u32 flags);
bool    pmm_page_has_flag(u64 phys_addr, u32 flag);

/* Memory pressure */
void    pmm_register_pressure_cb(mem_pressure_cb_t cb);
bool    pmm_is_low_memory(void);
bool    pmm_is_oom(void);

/* Ownership */
void pmm_set_owner(u64 phys_addr, yam_node_id_t owner);

/* Statistics */
u64  pmm_total_memory(void);
u64  pmm_free_memory(void);
const mem_zone_t *pmm_get_zone(mem_zone_type_t zone);
void pmm_print_stats(void);

/* Self-test */
void pmm_self_test(void);

#endif /* _MEM_PMM_H */
