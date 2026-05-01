#ifndef _LIBC_TIME_H
#define _LIBC_TIME_H

#include <nexus/types.h>

typedef long time_t;
typedef long clock_t;

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

time_t time(time_t *tloc);
int clock_gettime(int clk_id, struct timespec *tp);
struct tm *localtime(const time_t *timep);

#endif
