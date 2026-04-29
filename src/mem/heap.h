/* ============================================================================
 * YamKernel — Kernel Heap v0.3.0
 * Size-class buckets, auto-expansion, aligned alloc, stats
 * ============================================================================ */

#ifndef _MEM_HEAP_H
#define _MEM_HEAP_H

#include <nexus/types.h>

void  heap_init(void);

void *kmalloc(usize size);
void *kcalloc(usize count, usize size);
void *krealloc(void *ptr, usize new_size);
void *kmemalign(usize alignment, usize size);
void  kfree(void *ptr);
usize ksize(void *ptr);   /* Query usable size of allocation */

/* Statistics */
typedef struct {
    u64 total_bytes;
    u64 used_bytes;
    u64 free_bytes;
    u64 alloc_count;
    u64 free_count;
    u64 expansions;
} heap_stats_t;

void heap_get_stats(heap_stats_t *out);
void heap_print_stats(void);

#endif /* _MEM_HEAP_H */
