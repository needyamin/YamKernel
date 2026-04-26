/* YamKernel — Wait queues, sleep/wake, mutexes */
#ifndef _SCHED_WAIT_H
#define _SCHED_WAIT_H

#include <nexus/types.h>
#include "../lib/spinlock.h"

struct task;

typedef struct wait_queue {
    struct task *head;          /* singly-linked via task->wait_next */
    spinlock_t   lock;
} wait_queue_t;

#define WAIT_QUEUE_INIT { 0, SPINLOCK_INIT }

void  wq_init(wait_queue_t *wq);
void  wq_sleep(wait_queue_t *wq);   /* block current task */
void  wq_wake_one(wait_queue_t *wq);
void  wq_wake_all(wait_queue_t *wq);

/* Sleep current task for `ms` milliseconds (uses APIC timer ticks). */
void  task_sleep_ms(u64 ms);

/* ---- Mutex (blocking) ---- */
typedef struct {
    volatile u32  locked;
    wait_queue_t  waiters;
} mutex_t;

#define MUTEX_INIT { 0, WAIT_QUEUE_INIT }

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);

#endif
