/* ============================================================================
 * YamKernel — Kernel Heap v0.3.0
 * Size-class buckets for small allocs, free-list for large, auto-expand
 * ============================================================================ */

#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "../lib/string.h"
#include "../lib/kprintf.h"
#include "../lib/spinlock.h"

typedef struct heap_block {
    usize             size;
    bool              free;
    struct heap_block *next;
    struct heap_block *prev;
    u32               magic;
    u32               _pad[3];
} heap_block_t;

#define HEAP_MAGIC  0xDEADBEEF
#define HEAP_ALIGN  16
#define HEAP_INITIAL_PAGES 16384   /* 64MB initial heap */
#define HEAP_EXPAND_PAGES  4096    /* 16MB per expansion */

/* Size-class buckets for O(1) small allocation */
#define BUCKET_COUNT    10
#define BUCKET_MAX_SIZE 4096
static const usize bucket_sizes[BUCKET_COUNT] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};

typedef struct bucket_obj {
    struct bucket_obj *next;
} bucket_obj_t;

static bucket_obj_t *buckets[BUCKET_COUNT];
static spinlock_t bucket_locks[BUCKET_COUNT];

static heap_block_t *heap_head = NULL;
static u64 heap_base = 0;
static u64 heap_end = 0;
static u64 heap_size = 0;
static spinlock_t heap_lock = SPINLOCK_INIT;

/* Statistics */
static u64 stat_alloc_count = 0;
static u64 stat_free_count = 0;
static u64 stat_expansions = 0;

static inline usize align_up(usize val, usize align) {
    return (val + align - 1) & ~(align - 1);
}

static int bucket_index(usize size) {
    for (int i = 0; i < BUCKET_COUNT; i++) {
        if (size <= bucket_sizes[i]) return i;
    }
    return -1;
}

/* Allocate raw memory from the main heap free-list (bypasses buckets) */
static void *heap_alloc_raw(usize size) {
    size = (size + 15) & ~(usize)15;
    heap_block_t *block = heap_head;
    while (block) {
        if (block->magic != HEAP_MAGIC) return NULL;
        if (block->free && block->size >= size) {
            if (block->size > size + sizeof(heap_block_t) + HEAP_ALIGN) {
                heap_block_t *nb = (heap_block_t *)((u8 *)block + sizeof(heap_block_t) + size);
                nb->size  = block->size - size - sizeof(heap_block_t);
                nb->free  = true;
                nb->next  = block->next;
                nb->prev  = block;
                nb->magic = HEAP_MAGIC;
                if (block->next) block->next->prev = nb;
                block->next = nb;
                block->size = size;
            }
            block->free = false;
            void *ret = (void *)((u8 *)block + sizeof(heap_block_t));
            memset(ret, 0, size);
            return ret;
        }
        block = block->next;
    }
    return NULL;
}

/* Populate a bucket by carving a heap page into fixed-size objects */
static void bucket_refill(int idx) {
    usize obj_size = bucket_sizes[idx];
    if (obj_size < sizeof(bucket_obj_t)) obj_size = sizeof(bucket_obj_t);

    /* Allocate directly from main heap to avoid recursive kmalloc -> bucket_refill */
    usize page_alloc = PAGE_SIZE;
    u64 hf = spin_lock_irqsave(&heap_lock);
    void *block = heap_alloc_raw(page_alloc);
    spin_unlock_irqrestore(&heap_lock, hf);
    if (!block) return;

    /* Offset by HEAP_ALIGN so the first object's kfree() won't perfectly align 
       with the page's heap_block_t header and accidentally free the whole page. */
    u8 *usable = (u8 *)block + HEAP_ALIGN;
    usize count = (page_alloc - HEAP_ALIGN) / obj_size;
    for (usize i = 0; i < count; i++) {
        bucket_obj_t *obj = (bucket_obj_t *)(usable + i * obj_size);
        obj->next = buckets[idx];
        buckets[idx] = obj;
    }
}

static bool heap_expand(void) {
    u64 phys = pmm_alloc_pages(HEAP_EXPAND_PAGES);
    if (!phys) return false;

    u64 new_base = (u64)vmm_phys_to_virt(phys);
    usize new_size = HEAP_EXPAND_PAGES * PAGE_SIZE;

    /* Create a free block in the new region */
    heap_block_t *new_block = (heap_block_t *)new_base;
    new_block->size  = new_size - sizeof(heap_block_t);
    new_block->free  = true;
    new_block->magic = HEAP_MAGIC;
    new_block->prev  = NULL;
    new_block->next  = NULL;

    /* Append to the end of the heap list */
    heap_block_t *last = heap_head;
    if (last) {
        while (last->next) last = last->next;
        last->next = new_block;
        new_block->prev = last;
    } else {
        heap_head = new_block;
    }

    heap_size += new_size;
    heap_end = new_base + new_size;
    stat_expansions++;

    kprintf_color(0xFF00FF88, "[HEAP] Expanded by %lu KB (total %lu KB)\n",
                  new_size / 1024, heap_size / 1024);
    return true;
}

void heap_init(void) {
    u64 phys = pmm_alloc_pages(HEAP_INITIAL_PAGES);
    if (!phys) {
        kprintf_color(0xFFFF3333, "[HEAP] FATAL: Cannot allocate initial heap!\n");
        return;
    }

    heap_base = (u64)vmm_phys_to_virt(phys);
    heap_size = HEAP_INITIAL_PAGES * PAGE_SIZE;
    heap_end  = heap_base + heap_size;

    heap_head = (heap_block_t *)heap_base;
    heap_head->size  = heap_size - sizeof(heap_block_t);
    heap_head->free  = true;
    heap_head->next  = NULL;
    heap_head->prev  = NULL;
    heap_head->magic = HEAP_MAGIC;

    /* Initialize bucket locks */
    for (int i = 0; i < BUCKET_COUNT; i++) {
        buckets[i] = NULL;
        spin_init(&bucket_locks[i]);
    }

    kprintf_color(0xFF00FF88, "[HEAP] Initialized: %lu KB at 0x%lx (bucket alloc enabled)\n",
                  heap_size / 1024, heap_base);
}

void *kmalloc(usize size) {
    if (size == 0) return NULL;

    /* Try bucket allocator for small sizes */
    int bidx = bucket_index(size);
    if (bidx >= 0) {
        u64 f = spin_lock_irqsave(&bucket_locks[bidx]);
        if (!buckets[bidx]) bucket_refill(bidx);
        bucket_obj_t *obj = buckets[bidx];
        if (obj) {
            buckets[bidx] = obj->next;
            spin_unlock_irqrestore(&bucket_locks[bidx], f);
            stat_alloc_count++;
            return (void *)obj;
        }
        spin_unlock_irqrestore(&bucket_locks[bidx], f);
        /* Fall through to main allocator if bucket refill failed */
    }

    size = align_up(size, HEAP_ALIGN);

    u64 f = spin_lock_irqsave(&heap_lock);

retry:;
    heap_block_t *block = heap_head;
    while (block) {
        if (block->magic != HEAP_MAGIC) {
            kprintf_color(0xFFFF3333, "[HEAP] Corruption detected!\n");
            spin_unlock_irqrestore(&heap_lock, f);
            return NULL;
        }
        if (block->free && block->size >= size) {
            if (block->size > size + sizeof(heap_block_t) + HEAP_ALIGN) {
                heap_block_t *new_block = (heap_block_t *)((u8 *)block + sizeof(heap_block_t) + size);
                new_block->size  = block->size - size - sizeof(heap_block_t);
                new_block->free  = true;
                new_block->next  = block->next;
                new_block->prev  = block;
                new_block->magic = HEAP_MAGIC;
                if (block->next) block->next->prev = new_block;
                block->next = new_block;
                block->size = size;
            }
            block->free = false;
            stat_alloc_count++;
            spin_unlock_irqrestore(&heap_lock, f);
            return (void *)((u8 *)block + sizeof(heap_block_t));
        }
        block = block->next;
    }

    /* Try expanding the heap */
    if (heap_expand()) goto retry;

    spin_unlock_irqrestore(&heap_lock, f);
    kprintf_color(0xFFFF3333, "[HEAP] Out of memory (requested %lu bytes)\n", size);
    return NULL;
}

void *kcalloc(usize count, usize size) {
    usize total = count * size;
    void *ptr = kmalloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;

    /* Check if this is a bucket object (we can't easily tell, so use main free) */
    u64 f = spin_lock_irqsave(&heap_lock);
    heap_block_t *block = (heap_block_t *)((u8 *)ptr - sizeof(heap_block_t));

    if (block->magic != HEAP_MAGIC) {
        /* May be a bucket object — just ignore (leaked, but safe) */
        spin_unlock_irqrestore(&heap_lock, f);
        return;
    }

    block->free = true;
    stat_free_count++;

    if (block->next && block->next->free) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
    }
    if (block->prev && block->prev->free) {
        block->prev->size += sizeof(heap_block_t) + block->size;
        block->prev->next = block->next;
        if (block->next) block->next->prev = block->prev;
    }
    spin_unlock_irqrestore(&heap_lock, f);
}

void *krealloc(void *ptr, usize new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return NULL; }
    heap_block_t *block = (heap_block_t *)((u8 *)ptr - sizeof(heap_block_t));
    if (block->magic == HEAP_MAGIC && block->size >= new_size) return ptr;
    void *new_ptr = kmalloc(new_size);
    if (new_ptr) {
        usize copy_size = (block->magic == HEAP_MAGIC) ? block->size : new_size;
        if (copy_size > new_size) copy_size = new_size;
        memcpy(new_ptr, ptr, copy_size);
        kfree(ptr);
    }
    return new_ptr;
}

void *kmemalign(usize alignment, usize size) {
    if (alignment < HEAP_ALIGN) alignment = HEAP_ALIGN;
    /* Over-allocate and align within */
    void *ptr = kmalloc(size + alignment);
    if (!ptr) return NULL;
    u64 aligned = ((u64)ptr + alignment - 1) & ~(alignment - 1);
    return (void *)aligned;
}

usize ksize(void *ptr) {
    if (!ptr) return 0;
    heap_block_t *block = (heap_block_t *)((u8 *)ptr - sizeof(heap_block_t));
    if (block->magic != HEAP_MAGIC) return 0;
    return block->size;
}

void heap_get_stats(heap_stats_t *out) {
    if (!out) return;
    out->total_bytes = heap_size;
    out->alloc_count = stat_alloc_count;
    out->free_count = stat_free_count;
    out->expansions = stat_expansions;
    out->used_bytes = 0;
    out->free_bytes = 0;
    for (heap_block_t *b = heap_head; b; b = b->next) {
        if (b->free) out->free_bytes += b->size;
        else out->used_bytes += b->size;
    }
}

void heap_print_stats(void) {
    heap_stats_t s;
    heap_get_stats(&s);
    kprintf_color(0xFF00DDFF, "[HEAP] Total: %lu KB | Used: %lu KB | Free: %lu KB\n",
                  s.total_bytes/1024, s.used_bytes/1024, s.free_bytes/1024);
    kprintf("[HEAP] Allocs: %lu | Frees: %lu | Expansions: %lu\n",
            s.alloc_count, s.free_count, s.expansions);
}
