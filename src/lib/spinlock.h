/* YamKernel — Spinlock (saves/restores IF for IRQ-safe regions) */
#ifndef _LIB_SPINLOCK_H
#define _LIB_SPINLOCK_H

#include <nexus/types.h>

typedef struct { volatile u32 locked; } spinlock_t;

#define SPINLOCK_INIT { 0 }

ALWAYS_INLINE void spin_init(spinlock_t *s) { s->locked = 0; }

ALWAYS_INLINE u64 spin_lock_irqsave(spinlock_t *s) {
    u64 flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags));
    while (__atomic_test_and_set(&s->locked, __ATOMIC_ACQUIRE))
        __asm__ volatile ("pause");
    return flags;
}

ALWAYS_INLINE void spin_unlock_irqrestore(spinlock_t *s, u64 flags) {
    __atomic_clear(&s->locked, __ATOMIC_RELEASE);
    if (flags & 0x200) __asm__ volatile ("sti");
}

ALWAYS_INLINE void spin_lock(spinlock_t *s) {
    while (__atomic_test_and_set(&s->locked, __ATOMIC_ACQUIRE))
        __asm__ volatile ("pause");
}

ALWAYS_INLINE void spin_unlock(spinlock_t *s) {
    __atomic_clear(&s->locked, __ATOMIC_RELEASE);
}

#endif
