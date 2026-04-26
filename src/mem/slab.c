/* YamKernel — Slab: per-cache freelist over PMM-allocated slabs.
 *
 * Each cache owns a list of slabs (one PMM page each). Each slab is sliced
 * into objects of obj_size, threaded onto a single freelist via the first
 * 8 bytes of each free object. Allocation/free are O(1).  No NUMA, no
 * per-CPU magazines yet. */
#include "slab.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "../lib/string.h"
#include "../lib/spinlock.h"
#include "../lib/kprintf.h"

typedef struct slab {
    struct slab *next;
    void        *base;          /* HHDM virt of the page */
} slab_t;

struct kmem_cache {
    const char *name;
    usize       obj_size;
    usize       align;
    void       *freelist;       /* singly-linked via *(void**)obj */
    slab_t     *slabs;
    u64         alloc_count;
    u64         free_count;
    spinlock_t  lock;
};

#define SLAB_PAGE_SIZE 4096

static kmem_cache_t cache_pool[32];
static u32          cache_count = 0;

static void slab_grow(kmem_cache_t *c) {
    u64 phys = pmm_alloc_page();
    if (!phys) return;
    void *base = vmm_phys_to_virt(phys);
    memset(base, 0, SLAB_PAGE_SIZE);

    slab_t *s = (slab_t *)kmalloc(sizeof(slab_t));
    s->base = base; s->next = c->slabs; c->slabs = s;

    /* Carve into objects, push onto freelist */
    usize n = SLAB_PAGE_SIZE / c->obj_size;
    for (usize i = 0; i < n; i++) {
        void *obj = (u8 *)base + i * c->obj_size;
        *(void **)obj = c->freelist;
        c->freelist = obj;
    }
}

kmem_cache_t *kmem_cache_create(const char *name, usize obj_size, usize align) {
    if (cache_count >= sizeof(cache_pool)/sizeof(cache_pool[0])) return NULL;
    if (obj_size < sizeof(void *)) obj_size = sizeof(void *);
    /* Round up to alignment */
    if (align < 8) align = 8;
    obj_size = (obj_size + align - 1) & ~(align - 1);

    kmem_cache_t *c = &cache_pool[cache_count++];
    c->name = name; c->obj_size = obj_size; c->align = align;
    c->freelist = NULL; c->slabs = NULL;
    c->alloc_count = c->free_count = 0;
    spin_init(&c->lock);
    kprintf_color(0xFF00FF88, "[SLAB] cache '%s' obj=%lu align=%lu\n",
                  name, obj_size, align);
    return c;
}

void *kmem_cache_alloc(kmem_cache_t *c) {
    u64 f = spin_lock_irqsave(&c->lock);
    if (!c->freelist) slab_grow(c);
    void *o = c->freelist;
    if (o) {
        c->freelist = *(void **)o;
        c->alloc_count++;
    }
    spin_unlock_irqrestore(&c->lock, f);
    if (o) memset(o, 0, c->obj_size);
    return o;
}

void kmem_cache_free(kmem_cache_t *c, void *obj) {
    if (!obj) return;
    u64 f = spin_lock_irqsave(&c->lock);
    *(void **)obj = c->freelist;
    c->freelist = obj;
    c->free_count++;
    spin_unlock_irqrestore(&c->lock, f);
}

void kmem_cache_stats(const kmem_cache_t *c, u64 *a, u64 *fr) {
    if (a)  *a  = c->alloc_count;
    if (fr) *fr = c->free_count;
}
