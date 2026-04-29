/* YamKernel — Wait queues, sleep/wake, mutex, rwlock, semaphore, futex
 * v0.3.0 */
#include "wait.h"
#include "sched.h"
#include "../cpu/percpu.h"
#include "../lib/spinlock.h"
#include "../lib/kprintf.h"

/* ---- Wait Queue ---- */
void wq_init(wait_queue_t *wq) { wq->head = NULL; spin_init(&wq->lock); }

void wq_sleep(wait_queue_t *wq) {
    cli();
    spin_lock(&wq->lock);
    task_t *cur = sched_current();
    cur->state = TASK_BLOCKED;
    cur->wait_next = wq->head;
    wq->head = cur;
    spin_unlock(&wq->lock);
    sched_yield();
}

void wq_wake_one(wait_queue_t *wq) {
    u64 f = spin_lock_irqsave(&wq->lock);
    task_t *t = wq->head;
    if (t) { wq->head = t->wait_next; t->wait_next = NULL; }
    spin_unlock_irqrestore(&wq->lock, f);
    if (t) sched_unblock(t);
}

void wq_wake_all(wait_queue_t *wq) {
    u64 f = spin_lock_irqsave(&wq->lock);
    task_t *list = wq->head; wq->head = NULL;
    spin_unlock_irqrestore(&wq->lock, f);
    while (list) {
        task_t *n = list->wait_next; list->wait_next = NULL;
        sched_unblock(list);
        list = n;
    }
}

void task_sleep_ms(u64 ms) {
    u64 ticks = (ms + 9) / 10;
    if (ticks == 0) ticks = 1;
    sched_sleep_until(this_cpu()->ticks + ticks);
}

/* ---- Mutex ---- */
void mutex_init(mutex_t *m) { m->locked = 0; wq_init(&m->waiters); }

void mutex_lock(mutex_t *m) {
    while (__atomic_exchange_n(&m->locked, 1, __ATOMIC_ACQUIRE) != 0)
        wq_sleep(&m->waiters);
}

void mutex_unlock(mutex_t *m) {
    __atomic_store_n(&m->locked, 0, __ATOMIC_RELEASE);
    wq_wake_one(&m->waiters);
}

/* ---- RW Lock ---- */
void rwlock_init(rwlock_t *rw) {
    rw->readers = 0;
    rw->write_locked = 0;
    wq_init(&rw->read_waiters);
    wq_init(&rw->write_waiters);
    spin_init(&rw->lock);
}

void rwlock_read_lock(rwlock_t *rw) {
    for (;;) {
        u64 f = spin_lock_irqsave(&rw->lock);
        if (!rw->write_locked) {
            rw->readers++;
            spin_unlock_irqrestore(&rw->lock, f);
            return;
        }
        spin_unlock_irqrestore(&rw->lock, f);
        wq_sleep(&rw->read_waiters);
    }
}

void rwlock_read_unlock(rwlock_t *rw) {
    u64 f = spin_lock_irqsave(&rw->lock);
    rw->readers--;
    if (rw->readers == 0) {
        spin_unlock_irqrestore(&rw->lock, f);
        wq_wake_one(&rw->write_waiters);
    } else {
        spin_unlock_irqrestore(&rw->lock, f);
    }
}

void rwlock_write_lock(rwlock_t *rw) {
    for (;;) {
        u64 f = spin_lock_irqsave(&rw->lock);
        if (!rw->write_locked && rw->readers == 0) {
            rw->write_locked = 1;
            spin_unlock_irqrestore(&rw->lock, f);
            return;
        }
        spin_unlock_irqrestore(&rw->lock, f);
        wq_sleep(&rw->write_waiters);
    }
}

void rwlock_write_unlock(rwlock_t *rw) {
    u64 f = spin_lock_irqsave(&rw->lock);
    rw->write_locked = 0;
    spin_unlock_irqrestore(&rw->lock, f);
    wq_wake_all(&rw->read_waiters);
    wq_wake_one(&rw->write_waiters);
}

/* ---- Semaphore ---- */
void sem_init(semaphore_t *s, i32 initial) {
    s->count = initial;
    wq_init(&s->waiters);
    spin_init(&s->lock);
}

void sem_wait(semaphore_t *s) {
    for (;;) {
        u64 f = spin_lock_irqsave(&s->lock);
        if (s->count > 0) {
            s->count--;
            spin_unlock_irqrestore(&s->lock, f);
            return;
        }
        spin_unlock_irqrestore(&s->lock, f);
        wq_sleep(&s->waiters);
    }
}

void sem_post(semaphore_t *s) {
    u64 f = spin_lock_irqsave(&s->lock);
    s->count++;
    spin_unlock_irqrestore(&s->lock, f);
    wq_wake_one(&s->waiters);
}

bool sem_trywait(semaphore_t *s) {
    u64 f = spin_lock_irqsave(&s->lock);
    if (s->count > 0) {
        s->count--;
        spin_unlock_irqrestore(&s->lock, f);
        return true;
    }
    spin_unlock_irqrestore(&s->lock, f);
    return false;
}

/* ---- Futex ---- */
static wait_queue_t futex_table[FUTEX_HASH_SIZE];
static bool futex_initialized = false;

static u32 futex_hash(u64 addr) {
    return (u32)((addr >> 2) % FUTEX_HASH_SIZE);
}

i64 sys_futex(u32 *uaddr, int op, u32 val, u64 timeout) {
    (void)timeout;
    if (!futex_initialized) {
        for (int i = 0; i < FUTEX_HASH_SIZE; i++) wq_init(&futex_table[i]);
        futex_initialized = true;
    }

    u32 idx = futex_hash((u64)uaddr);
    wait_queue_t *wq = &futex_table[idx];

    switch (op) {
    case FUTEX_WAIT:
        if (__atomic_load_n(uaddr, __ATOMIC_ACQUIRE) != val)
            return -1; /* Value changed, don't sleep */
        wq_sleep(wq);
        return 0;

    case FUTEX_WAKE:
        for (u32 i = 0; i < val; i++) wq_wake_one(wq);
        return 0;

    default:
        return -1;
    }
}
