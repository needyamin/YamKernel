/* YamKernel — Wait queues, sleep/wake, mutexes, rwlocks, semaphores, futex
 * v0.3.0 */
#ifndef _SCHED_WAIT_H
#define _SCHED_WAIT_H

#include <nexus/types.h>
#include "../lib/spinlock.h"

struct task;

typedef struct wait_queue {
    struct task *head;
    spinlock_t   lock;
} wait_queue_t;

#define WAIT_QUEUE_INIT { 0, SPINLOCK_INIT }

void  wq_init(wait_queue_t *wq);
void  wq_sleep(wait_queue_t *wq);
void  wq_wake_one(wait_queue_t *wq);
void  wq_wake_all(wait_queue_t *wq);

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

/* ---- RW Lock (readers-writer) ---- */
typedef struct {
    volatile i32  readers;      /* >0 = active readers, -1 = writer active */
    volatile u32  write_locked;
    wait_queue_t  read_waiters;
    wait_queue_t  write_waiters;
    spinlock_t    lock;
} rwlock_t;

#define RWLOCK_INIT { 0, 0, WAIT_QUEUE_INIT, WAIT_QUEUE_INIT, SPINLOCK_INIT }

void rwlock_init(rwlock_t *rw);
void rwlock_read_lock(rwlock_t *rw);
void rwlock_read_unlock(rwlock_t *rw);
void rwlock_write_lock(rwlock_t *rw);
void rwlock_write_unlock(rwlock_t *rw);

/* ---- Semaphore (counting) ---- */
typedef struct {
    volatile i32  count;
    wait_queue_t  waiters;
    spinlock_t    lock;
} semaphore_t;

void sem_init(semaphore_t *s, i32 initial);
void sem_wait(semaphore_t *s);      /* P operation (down) */
void sem_post(semaphore_t *s);      /* V operation (up) */
bool sem_trywait(semaphore_t *s);   /* Non-blocking try */

/* ---- Futex (fast userspace mutex) ---- */
#define FUTEX_WAIT      0
#define FUTEX_WAKE      1
#define FUTEX_HASH_SIZE 64

i64 sys_futex(u32 *uaddr, int op, u32 val, u64 timeout);

#endif
