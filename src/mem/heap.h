/* ============================================================================
 * YamKernel — Kernel Heap
 * ============================================================================ */

#ifndef _MEM_HEAP_H
#define _MEM_HEAP_H

#include <nexus/types.h>

void  heap_init(void);

void *kmalloc(usize size);
void *kcalloc(usize count, usize size);
void *krealloc(void *ptr, usize new_size);
void  kfree(void *ptr);

#endif /* _MEM_HEAP_H */
