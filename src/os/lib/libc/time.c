#include "time.h"
#include "../libyam/syscall.h"

time_t time(time_t *tloc) {
    time_t t = (time_t)syscall0(SYS_CLOCK_GETTIME);
    if (tloc) *tloc = t;
    return t;
}

int clock_gettime(int clk_id, struct timespec *tp) {
    (void)clk_id;
    if (!tp) return -1;
    tp->tv_sec = time(NULL);
    tp->tv_nsec = 0;
    return 0;
}

struct tm *localtime(const time_t *timep) {
    static struct tm tm;
    time_t t = timep ? *timep : time(NULL);
    tm.tm_sec = (int)(t % 60);
    tm.tm_min = (int)((t / 60) % 60);
    tm.tm_hour = (int)((t / 3600) % 24);
    tm.tm_mday = 1;
    tm.tm_mon = 0;
    tm.tm_year = 70;
    tm.tm_wday = 4;
    tm.tm_yday = 0;
    tm.tm_isdst = 0;
    return &tm;
}
