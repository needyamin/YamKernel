/* YamKernel — Slab allocator (fast fixed-size object cache) */
#ifndef _MEM_SLAB_H
#define _MEM_SLAB_H

#include <nexus/types.h>

typedef struct kmem_cache kmem_cache_t;

kmem_cache_t *kmem_cache_create(const char *name, usize obj_size, usize align);
void         *kmem_cache_alloc(kmem_cache_t *c);
void          kmem_cache_free(kmem_cache_t *c, void *obj);
void          kmem_cache_stats(const kmem_cache_t *c, u64 *out_alloc, u64 *out_free);

#endif
