#ifndef _LIBC_TIME_H
#define _LIBC_TIME_H

#include <nexus/types.h>

typedef unsigned long size_t;
typedef long time_t;
typedef long clock_t;
typedef int clockid_t;

#ifndef _LIBC_TIMESPEC_DEFINED
#define _LIBC_TIMESPEC_DEFINED
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#endif

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
    long tm_gmtoff;
    const char *tm_zone;
};

#ifndef _LIBC_TMS_DEFINED
#define _LIBC_TMS_DEFINED
struct tms {
    clock_t tms_utime;
    clock_t tms_stime;
    clock_t tms_cutime;
    clock_t tms_cstime;
};
#endif

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCKS_PER_SEC 1000000
#define TIMER_ABSTIME 1

time_t time(time_t *tloc);
clock_t clock(void);
clock_t times(struct tms *buf);
int clock_gettime(int clk_id, struct timespec *tp);
int clock_getres(int clk_id, struct timespec *tp);
int clock_settime(int clk_id, const struct timespec *tp);
int clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *request, struct timespec *remain);
struct tm *localtime(const time_t *timep);
struct tm *localtime_r(const time_t *timep, struct tm *result);
struct tm *gmtime_r(const time_t *timep, struct tm *result);
time_t mktime(struct tm *tm);
time_t timegm(struct tm *tm);
void tzset(void);
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);

extern long timezone;
extern int daylight;
extern char *tzname[2];

#endif
