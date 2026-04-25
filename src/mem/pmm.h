/* ============================================================================
 * YamKernel — Physical Memory Manager (Cell Allocator)
 * Novel: Fractal quad-tree subdivision instead of buddy system
 * ============================================================================ */

#ifndef _MEM_PMM_H
#define _MEM_PMM_H

#include <nexus/types.h>

/* Page size = 4KB */
#define PAGE_SIZE 4096

/* Cell allocator — fractal quad-tree memory management
 *
 * Unlike Linux's buddy allocator (power-of-2 blocks), YamKernel uses
 * a quad-tree where each cell can split into 4 children.
 * This gives finer-grained allocation with O(log4 n) search.
 *
 * The root cell represents all physical memory.
 * Each cell tracks: base address, size, state, owner (YamGraph node ID).
 */

typedef enum {
    CELL_FREE    = 0,   /* Available for allocation */
    CELL_SPLIT   = 1,   /* Subdivided into 4 children */
    CELL_USED    = 2,   /* Allocated and in use */
    CELL_RESERVED= 3,   /* Reserved (firmware, MMIO, etc.) */
} cell_state_t;

#define CELL_POOL_SIZE 16384  /* Max number of cell nodes */

typedef struct cell {
    u64           base;     /* Physical base address */
    u64           size;     /* Size in bytes */
    cell_state_t  state;    /* Current state */
    yam_node_id_t owner;    /* Owning node in YamGraph (0 = kernel) */
    u16           parent;   /* Parent index (0xFFFF = root) */
    u16           children[4]; /* Child indices (0xFFFF = none) */
} cell_t;

/* Initialize the physical memory manager from Limine memory map */
void pmm_init(void *memmap_response, u64 hhdm_offset);

/* Allocate a physical page (returns physical address, 0 on failure) */
u64  pmm_alloc_page(void);

/* Allocate N contiguous pages */
u64  pmm_alloc_pages(u64 count);

/* Free a physical page */
void pmm_free_page(u64 phys_addr);

/* Free N contiguous pages */
void pmm_free_pages(u64 phys_addr, u64 count);

/* Set ownership of a cell to a YamGraph node */
void pmm_set_owner(u64 phys_addr, yam_node_id_t owner);

/* Get total/free memory in bytes */
u64  pmm_total_memory(void);
u64  pmm_free_memory(void);

/* Run self-test */
void pmm_self_test(void);

#endif /* _MEM_PMM_H */
