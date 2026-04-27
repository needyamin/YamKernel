/* ============================================================================
 * YamKernel — Kernel Heap Implementation
 * Free-list allocator built on top of Cell Allocator (PMM)
 * ============================================================================ */

#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "../lib/string.h"
#include "../lib/kprintf.h"
#include "../lib/spinlock.h"

/* Heap block header */
typedef struct heap_block {
    usize             size;     /* Usable size (excluding header) */
    bool              free;
    struct heap_block *next;
    struct heap_block *prev;
    u32               magic;    /* 0xDEADBEEF for corruption detection */
    u32               _pad[3];  /* pad to 48 bytes (multiple of 16) */
} heap_block_t;

#define HEAP_MAGIC  0xDEADBEEF
#define HEAP_ALIGN  16
#define HEAP_INITIAL_PAGES 16384   /* 64MB initial heap */

static heap_block_t *heap_head = NULL;
static u64 heap_base = 0;
static u64 heap_size = 0;
static spinlock_t heap_lock = SPINLOCK_INIT;

/* Align up to HEAP_ALIGN */
static inline usize align_up(usize val, usize align) {
    return (val + align - 1) & ~(align - 1);
}

void heap_init(void) {
    /* Allocate initial heap pages from PMM */
    u64 phys = pmm_alloc_pages(HEAP_INITIAL_PAGES);
    if (!phys) {
        kprintf_color(0xFFFF3333, "[HEAP] FATAL: Cannot allocate initial heap!\n");
        return;
    }

    heap_base = (u64)vmm_phys_to_virt(phys);
    heap_size = HEAP_INITIAL_PAGES * PAGE_SIZE;

    /* Initialize single free block spanning entire heap */
    heap_head = (heap_block_t *)heap_base;
    heap_head->size  = heap_size - sizeof(heap_block_t);
    heap_head->free  = true;
    heap_head->next  = NULL;
    heap_head->prev  = NULL;
    heap_head->magic = HEAP_MAGIC;

    kprintf_color(0xFF00FF88, "[HEAP] Initialized: %lu KB at 0x%lx\n",
                  heap_size / 1024, heap_base);
}

void *kmalloc(usize size) {
    if (size == 0) return NULL;
    size = align_up(size, HEAP_ALIGN);

    u64 f = spin_lock_irqsave(&heap_lock);
    heap_block_t *block = heap_head;
    while (block) {
        if (block->magic != HEAP_MAGIC) {
            kprintf_color(0xFFFF3333, "[HEAP] Corruption detected!\n");
            spin_unlock_irqrestore(&heap_lock, f);
            return NULL;
        }

        if (block->free && block->size >= size) {
            /* Split if there's enough room for another block */
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
            void *result = (void *)((u8 *)block + sizeof(heap_block_t));
            spin_unlock_irqrestore(&heap_lock, f);
            return result;
        }

        block = block->next;
    }

    /* Out of heap space */
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

    u64 f = spin_lock_irqsave(&heap_lock);
    heap_block_t *block = (heap_block_t *)((u8 *)ptr - sizeof(heap_block_t));
    if (block->magic != HEAP_MAGIC) {
        kprintf_color(0xFFFF3333, "[HEAP] kfree: invalid pointer or corruption!\n");
        spin_unlock_irqrestore(&heap_lock, f);
        return;
    }

    block->free = true;

    /* Coalesce with next block */
    if (block->next && block->next->free) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
    }

    /* Coalesce with previous block */
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
    if (block->size >= new_size) return ptr;

    void *new_ptr = kmalloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        kfree(ptr);
    }
    return new_ptr;
}
