/* YamKernel — Wait queues, sleep/wake, mutexes */
#include "wait.h"
#include "sched.h"
#include "../cpu/percpu.h"
#include "../lib/spinlock.h"

void wq_init(wait_queue_t *wq) { wq->head = NULL; spin_init(&wq->lock); }

void wq_sleep(wait_queue_t *wq) {
    /* Keep IF=0 across enqueue+yield to avoid lost-wakeup race */
    cli();
    spin_lock(&wq->lock);
    task_t *cur = sched_current();
    cur->state = TASK_BLOCKED;
    cur->wait_next = wq->head;
    wq->head = cur;
    spin_unlock(&wq->lock);
    sched_yield();   /* re-enables IF on the way back */
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
    /* APIC timer = 100 Hz -> 1 tick = 10 ms */
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
