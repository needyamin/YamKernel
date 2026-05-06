/* ============================================================================
 * YamKernel — POSIX pthread (User-Space libc)
 * ============================================================================ */
#include "pthread.h"
#include "stdlib.h"
#include "errno.h"
#include "unistd.h"
#include "../libyam/syscall.h"

typedef struct {
    void *(*func)(void *);
    void *arg;
} thread_args_t;

static void thread_wrapper(thread_args_t *targs) {
    void *(*func)(void *) = targs->func;
    void *arg = targs->arg;
    free(targs);
    
    void *ret = func(arg);
    pthread_exit(ret);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
    (void)attr;
    size_t stack_size = 64 * 1024;
    void *stack = malloc(stack_size);
    if (!stack) return ENOMEM;
    
    u64 stack_top = (u64)stack + stack_size;
    stack_top &= ~15ULL; /* 16-byte align */
    stack_top -= 8;      /* SysV ABI expects RSP % 16 == 8 on function entry */
    
    thread_args_t *targs = malloc(sizeof(thread_args_t));
    if (!targs) {
        free(stack);
        return ENOMEM;
    }
    targs->func = start_routine;
    targs->arg = arg;
    
    i64 tid = syscall4(SYS_THREAD_CREATE, (u64)thread_wrapper, stack_top, (u64)targs, 0);
    if (tid < 0) {
        free(targs);
        free(stack);
        return EAGAIN;
    }
    
    if (thread) *thread = (pthread_t)tid;
    return 0;
}

int pthread_join(pthread_t thread, void **value_ptr) {
    (void)value_ptr;
    int status;
    syscall3(SYS_WAITPID, (u64)thread, (u64)&status, 0);
    return 0;
}

void pthread_exit(void *retval) {
    (void)retval;
    syscall1(SYS_THREAD_EXIT, 0);
    __builtin_unreachable();
}

pthread_t pthread_self(void) {
    return (pthread_t)syscall0(SYS_GETPID);
}

/* Dummy implementations for mutex and cond for now, since futex is not fully wired in libc yet */
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) { (void)mutex; (void)attr; return 0; }
int pthread_mutex_destroy(pthread_mutex_t *mutex) { (void)mutex; return 0; }
int pthread_mutex_lock(pthread_mutex_t *mutex) { (void)mutex; return 0; }
int pthread_mutex_unlock(pthread_mutex_t *mutex) { (void)mutex; return 0; }
int pthread_mutex_trylock(pthread_mutex_t *mutex) { (void)mutex; return 0; }
