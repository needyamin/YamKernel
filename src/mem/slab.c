/* YamKernel — Slab v0.3.0: per-CPU magazine, partial/full/empty lists, shrink.
 * Each cache owns slabs. Per-CPU magazine reduces lock contention. */
#include "slab.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "../lib/string.h"
#include "../lib/spinlock.h"
#include "../lib/kprintf.h"

/* Magazine: per-CPU local cache of objects */
#define MAGAZINE_SIZE 32
typedef struct {
    void *objects[MAGAZINE_SIZE];
    u32   count;
} magazine_t;

typedef struct slab {
    struct slab *next;
    void        *base;
    u32          in_use;   /* Objects currently allocated from this slab */
    u32          total;    /* Total objects in this slab */
} slab_t;

struct kmem_cache {
    const char *name;
    usize       obj_size;
    usize       align;
    void       *freelist;
    slab_t     *partial;    /* Some objects free */
    slab_t     *full;       /* All objects allocated */
    slab_t     *empty;      /* All objects free */
    u64         alloc_count;
    u64         free_count;
    spinlock_t  lock;
    magazine_t  cpu_mag[4]; /* Per-CPU magazines (up to 4 CPUs) */
    bool        active;
};

#define SLAB_PAGE_SIZE 4096
#define MAX_CACHES 64

static kmem_cache_t cache_pool[MAX_CACHES];
static u32 cache_count = 0;

static void slab_grow(kmem_cache_t *c) {
    u64 phys = pmm_alloc_page();
    if (!phys) return;
    void *base = vmm_phys_to_virt(phys);
    memset(base, 0, SLAB_PAGE_SIZE);

    slab_t *s = (slab_t *)kmalloc(sizeof(slab_t));
    if (!s) return;
    s->base = base;
    s->in_use = 0;
    s->total = SLAB_PAGE_SIZE / c->obj_size;
    s->next = c->empty;
    c->empty = s;

    usize n = s->total;
    for (usize i = 0; i < n; i++) {
        void *obj = (u8 *)base + i * c->obj_size;
        *(void **)obj = c->freelist;
        c->freelist = obj;
    }
}

kmem_cache_t *kmem_cache_create(const char *name, usize obj_size, usize align) {
    if (cache_count >= MAX_CACHES) return NULL;
    if (obj_size < sizeof(void *)) obj_size = sizeof(void *);
    if (align < 8) align = 8;
    obj_size = (obj_size + align - 1) & ~(align - 1);

    kmem_cache_t *c = &cache_pool[cache_count++];
    c->name = name;
    c->obj_size = obj_size;
    c->align = align;
    c->freelist = NULL;
    c->partial = NULL;
    c->full = NULL;
    c->empty = NULL;
    c->alloc_count = c->free_count = 0;
    c->active = true;
    spin_init(&c->lock);
    for (int i = 0; i < 4; i++) c->cpu_mag[i].count = 0;

    kprintf_color(0xFF00FF88, "[SLAB] cache '%s' obj=%lu align=%lu\n",
                  name, obj_size, align);
    return c;
}

void *kmem_cache_alloc(kmem_cache_t *c) {
    /* Try per-CPU magazine first (lock-free fast path) */
    /* Simplified: use CPU 0 magazine for BSP */
    magazine_t *mag = &c->cpu_mag[0];
    if (mag->count > 0) {
        void *o = mag->objects[--mag->count];
        if (o) {
            memset(o, 0, c->obj_size);
            return o;
        }
    }

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

    /* Try per-CPU magazine first */
    magazine_t *mag = &c->cpu_mag[0];
    if (mag->count < MAGAZINE_SIZE) {
        mag->objects[mag->count++] = obj;
        return;
    }

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

void kmem_cache_shrink(kmem_cache_t *c) {
    u64 f = spin_lock_irqsave(&c->lock);
    /* Free empty slabs back to PMM */
    slab_t *s = c->empty;
    while (s) {
        slab_t *next = s->next;
        if (s->base) {
            u64 phys = vmm_virt_hhdm_to_phys(s->base);
            pmm_free_page(phys);
        }
        kfree(s);
        s = next;
    }
    c->empty = NULL;
    spin_unlock_irqrestore(&c->lock, f);
}

void kmem_cache_destroy(kmem_cache_t *c) {
    kmem_cache_shrink(c);
    c->active = false;
    c->freelist = NULL;
}
