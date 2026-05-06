/* ============================================================================
 * YamKernel — POSIX pthread (User-Space libc) — Phase 1.1 hardening
 * Mutex: futex-backed spinlock.
 * Condvar: futex-backed wait/wake.
 * TLS keys: per-thread table stored in FS-segment TLS block.
 * ============================================================================ */
#include "pthread.h"
#include "stdlib.h"
#include "errno.h"
#include "unistd.h"
#include "../libyam/syscall.h"

/* ---- Internal futex helpers ------------------------------------------------ */

#define FUTEX_WAIT  0
#define FUTEX_WAKE  1

static inline void futex_wait(volatile int *addr, int expected) {
    syscall4(SYS_FUTEX, (u64)(usize)addr, FUTEX_WAIT, (u64)(unsigned)expected, 0);
}

static inline void futex_wake(volatile int *addr, int count) {
    syscall4(SYS_FUTEX, (u64)(usize)addr, FUTEX_WAKE, (u64)(unsigned)count, 0);
}

/* ---- TLS block (stored at FS base) ----------------------------------------
 * When a thread is created via pthread_create, we allocate a tls_block_t
 * and pass its address as tls_base to SYS_THREAD_CREATE.  The kernel writes
 * it to IA32_FS_BASE so that  __builtin_thread_pointer() / %fs:0  returns it.
 *
 * Layout keeps the self pointer at offset 0 (glibc-compatible).
 * -------------------------------------------------------------------------*/
#define PTHREAD_KEYS_MAX_IMPL 128

typedef struct {
    struct tls_block  *self;          /* offset 0: must be first */
    u64                tid;           /* kernel thread id */
    void              *specific[PTHREAD_KEYS_MAX_IMPL]; /* pthread_key values */
    int                cancelled;
    int                _pad;
} tls_block_t;

/* Global key destructor table */
static void (*key_dtors[PTHREAD_KEYS_MAX_IMPL])(void *);
static int   key_used[PTHREAD_KEYS_MAX_IMPL];

/* ---- Inline TLS read -------------------------------------------------------*/
static inline tls_block_t *tls_self(void) {
    tls_block_t *p;
    /* Read %fs:0 — the self pointer */
    __asm__ volatile("movq %%fs:0, %0" : "=r"(p));
    return p;
}

/* ---- Thread bootstrap -------------------------------------------------------*/
typedef struct {
    void *(*func)(void *);
    void *arg;
    tls_block_t *tls;
} thread_bootstrap_t;

static void thread_wrapper(thread_bootstrap_t *bs) {
    /* TLS is already live (kernel wrote FS base before first instruction) */
    void *(*func)(void *) = bs->func;
    void *arg             = bs->arg;
    free(bs);

    void *ret = func(arg);
    pthread_exit(ret);
}

/* ---- pthread_create --------------------------------------------------------*/
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
    (void)attr;

    /* Allocate stack */
    size_t stack_size = 256 * 1024; /* 256 KB */
    void *stack = malloc(stack_size);
    if (!stack) return ENOMEM;

    /* TLS block lives at top of the stack region (before the actual stack) */
    tls_block_t *tls = (tls_block_t *)malloc(sizeof(tls_block_t));
    if (!tls) { free(stack); return ENOMEM; }
    for (int i = 0; i < PTHREAD_KEYS_MAX_IMPL; i++) tls->specific[i] = NULL;
    tls->self      = tls;
    tls->cancelled = 0;

    /* Thread bootstrap args */
    thread_bootstrap_t *bs = (thread_bootstrap_t *)malloc(sizeof(thread_bootstrap_t));
    if (!bs) { free(tls); free(stack); return ENOMEM; }
    bs->func = start_routine;
    bs->arg  = arg;
    bs->tls  = tls;

    u64 stack_top = (u64)(usize)stack + stack_size;
    stack_top &= ~15ULL; /* 16-byte align */
    stack_top -= 8;      /* SysV ABI: RSP % 16 == 8 on function call */

    i64 tid = syscall4(SYS_THREAD_CREATE,
                       (u64)(usize)thread_wrapper,
                       stack_top,
                       (u64)(usize)bs,
                       (u64)(usize)tls);
    if (tid < 0) {
        free(bs); free(tls); free(stack);
        return EAGAIN;
    }

    tls->tid = (u64)tid;
    if (thread) *thread = (pthread_t)(usize)tid;
    return 0;
}

/* ---- pthread_join / detach -------------------------------------------------*/
int pthread_join(pthread_t thread, void **value_ptr) {
    (void)value_ptr;
    int status;
    syscall3(SYS_WAITPID, (u64)(usize)thread, (u64)(usize)&status, 0);
    return 0;
}

int pthread_detach(pthread_t thread) {
    (void)thread;
    return 0; /* threads are always detachable in current model */
}

void pthread_exit(void *retval) {
    (void)retval;
    syscall1(SYS_THREAD_EXIT, 0);
    __builtin_unreachable();
}

pthread_t pthread_self(void) {
    tls_block_t *tls = tls_self();
    if (tls) return (pthread_t)(usize)tls->tid;
    return (pthread_t)(usize)syscall0(SYS_GETPID);
}

int pthread_getname_np(pthread_t thread, char *name, size_t len) {
    (void)thread;
    if (name && len) name[0] = 0;
    return 0;
}

/* ---- pthread_attr ---------------------------------------------------------*/
int pthread_attr_init(pthread_attr_t *attr)               { (void)attr; return 0; }
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t sz) { (void)attr; (void)sz; return 0; }
int pthread_attr_destroy(pthread_attr_t *attr)            { (void)attr; return 0; }

/* ---- Mutex (futex-backed) --------------------------------------------------
 *  mutex->__x is used as the futex word:
 *    0 = unlocked
 *    1 = locked, no waiters
 *    2 = locked, waiters present
 * -------------------------------------------------------------------------*/
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    (void)attr;
    volatile int *word = (volatile int *)&mutex->__x;
    *word = 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    volatile int *word = (volatile int *)&mutex->__x;
    int expected = 0;
    if (__atomic_compare_exchange_n((int *)word, &expected, 1,
                                    0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return 0;
    return EBUSY;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    volatile int *word = (volatile int *)&mutex->__x;
    int c;
    /* Fast path: try CAS 0 -> 1 */
    int z = 0;
    if (__atomic_compare_exchange_n((int *)word, &z, 1,
                                    0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return 0;
    /* Contended: set to 2 (waiters), sleep */
    c = __atomic_exchange_n((int *)word, 2, __ATOMIC_ACQUIRE);
    while (c != 0) {
        futex_wait(word, 2);
        c = __atomic_exchange_n((int *)word, 2, __ATOMIC_ACQUIRE);
    }
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    volatile int *word = (volatile int *)&mutex->__x;
    /* If word was 2 (waiters), wake one */
    int prev = __atomic_fetch_sub((int *)word, 1, __ATOMIC_RELEASE);
    if (prev != 1) {
        __atomic_store_n((int *)word, 0, __ATOMIC_RELEASE);
        futex_wake(word, 1);
    }
    return 0;
}

/* ---- Condition variable (futex-backed) -------------------------------------
 *  cond->__x is a sequence counter incremented on every signal/broadcast.
 * -------------------------------------------------------------------------*/
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    (void)attr;
    volatile int *seq = (volatile int *)&cond->__x;
    *seq = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
    (void)cond;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    volatile int *seq = (volatile int *)&cond->__x;
    int val = __atomic_load_n((int *)seq, __ATOMIC_ACQUIRE);
    pthread_mutex_unlock(mutex);
    futex_wait(seq, val);
    pthread_mutex_lock(mutex);
    return 0;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                            const struct timespec *abstime) {
    (void)abstime;
    return pthread_cond_wait(cond, mutex); /* timeout not yet enforced */
}

int pthread_cond_signal(pthread_cond_t *cond) {
    volatile int *seq = (volatile int *)&cond->__x;
    __atomic_fetch_add((int *)seq, 1, __ATOMIC_RELEASE);
    futex_wake(seq, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    volatile int *seq = (volatile int *)&cond->__x;
    __atomic_fetch_add((int *)seq, 1, __ATOMIC_RELEASE);
    futex_wake(seq, 0x7fffffff);
    return 0;
}

int pthread_condattr_init(pthread_condattr_t *attr) { (void)attr; return 0; }
int pthread_condattr_setclock(pthread_condattr_t *attr, int clock_id) {
    (void)attr; (void)clock_id; return 0;
}

/* ---- TLS keys (pthread_key_t) ---------------------------------------------*/
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    for (int i = 0; i < PTHREAD_KEYS_MAX_IMPL; i++) {
        if (!key_used[i]) {
            key_used[i] = 1;
            key_dtors[i] = destructor;
            *key = (pthread_key_t)i;
            return 0;
        }
    }
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key) {
    if (key >= PTHREAD_KEYS_MAX_IMPL) return EINVAL;
    key_used[key] = 0;
    key_dtors[key] = NULL;
    return 0;
}

void *pthread_getspecific(pthread_key_t key) {
    if (key >= PTHREAD_KEYS_MAX_IMPL) return NULL;
    tls_block_t *tls = tls_self();
    if (!tls) return NULL;
    return tls->specific[key];
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    if (key >= PTHREAD_KEYS_MAX_IMPL) return EINVAL;
    tls_block_t *tls = tls_self();
    if (!tls) return EINVAL;
    tls->specific[key] = (void *)value;
    return 0;
}
