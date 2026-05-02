#ifndef _LIBC_SYS_TIME_H
#define _LIBC_SYS_TIME_H

#include <nexus/types.h>
#include "types.h"

struct timeval {
    time_t tv_sec;
    long tv_usec;
};

static inline int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (!tv) return -1;
    tv->tv_sec = 0;
    tv->tv_usec = 0;
    return 0;
}

#endif
