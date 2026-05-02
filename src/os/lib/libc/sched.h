#ifndef _LIBC_SCHED_H
#define _LIBC_SCHED_H

#include "../libyam/syscall.h"

#define SCHED_OTHER 0
#define SCHED_FIFO 1
#define SCHED_RR 2

struct sched_param {
    int sched_priority;
};

static inline int sched_yield(void) {
    syscall0(SYS_YIELD);
    return 0;
}

static inline int sched_get_priority_min(int policy) {
    (void)policy;
    return 0;
}

static inline int sched_get_priority_max(int policy) {
    (void)policy;
    return 0;
}

#endif
