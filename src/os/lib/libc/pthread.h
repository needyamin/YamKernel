#ifndef _LIBC_PTHREAD_H
#define _LIBC_PTHREAD_H

#include <nexus/types.h>
#include "time.h"

typedef usize size_t;

#ifndef HAVE_PTHREAD_STUBS
#define _POSIX_THREADS 1
#define PTHREAD_KEYS_MAX 128

typedef struct { void *__x; } pthread_cond_t;
typedef struct { unsigned __attr; } pthread_condattr_t;
typedef struct { void *__x; } pthread_mutex_t;
typedef struct { unsigned __attr; } pthread_mutexattr_t;
typedef unsigned pthread_key_t;
typedef unsigned long pthread_t;
typedef struct { unsigned __attr; } pthread_attr_t;

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);
int pthread_condattr_init(pthread_condattr_t *attr);
int pthread_condattr_setclock(pthread_condattr_t *attr, int clock_id);

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg);
int pthread_detach(pthread_t thread);
int pthread_join(pthread_t thread, void **value_ptr);
pthread_t pthread_self(void);
int pthread_getname_np(pthread_t thread, char *name, size_t len);
void pthread_exit(void *retval) __attribute__((noreturn));
int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
int pthread_attr_destroy(pthread_attr_t *attr);

int pthread_key_create(pthread_key_t *key, void (*destr_function)(void *));
int pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);
#else
typedef unsigned pthread_t;
typedef unsigned pthread_key_t;
static inline int pthread_getname_np(pthread_t thread, char *name, size_t len) {
    (void)thread;
    if (name && len) name[0] = 0;
    return 0;
}
#endif

#endif
