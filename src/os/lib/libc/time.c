#include "time.h"
#include "../libyam/syscall.h"
#include "errno.h"
#include "unistd.h"

long timezone = 0;
int daylight = 0;
char *tzname[2] = { "UTC", "UTC" };

clock_t clock(void) {
    return (clock_t)(time(NULL) * CLOCKS_PER_SEC);
}

clock_t times(struct tms *buf) {
    clock_t now = clock();
    if (buf) {
        buf->tms_utime = now;
        buf->tms_stime = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    return now;
}

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

int clock_getres(int clk_id, struct timespec *tp) {
    (void)clk_id;
    if (!tp) return -1;
    tp->tv_sec = 0;
    tp->tv_nsec = 1000000;
    return 0;
}

int clock_settime(int clk_id, const struct timespec *tp) {
    (void)clk_id;
    (void)tp;
    errno = ENOSYS;
    return -1;
}

int clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *request, struct timespec *remain) {
    (void)clock_id;
    (void)flags;
    if (remain) {
        remain->tv_sec = 0;
        remain->tv_nsec = 0;
    }
    if (!request) {
        errno = EINVAL;
        return EINVAL;
    }
    unsigned long ms = (unsigned long)(request->tv_sec * 1000);
    ms += (unsigned long)(request->tv_nsec / 1000000);
    syscall1(SYS_SLEEPMS, ms);
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
    tm.tm_gmtoff = 0;
    tm.tm_zone = "UTC";
    return &tm;
}

struct tm *localtime_r(const time_t *timep, struct tm *result) {
    if (!result) return NULL;
    struct tm *tmp = localtime(timep);
    *result = *tmp;
    return result;
}

struct tm *gmtime_r(const time_t *timep, struct tm *result) {
    return localtime_r(timep, result);
}

time_t mktime(struct tm *tm) {
    if (!tm) return (time_t)-1;
    return (time_t)tm->tm_sec +
           (time_t)tm->tm_min * 60 +
           (time_t)tm->tm_hour * 3600 +
           (time_t)(tm->tm_mday > 0 ? tm->tm_mday - 1 : 0) * 86400 +
           (time_t)tm->tm_yday * 86400;
}

time_t timegm(struct tm *tm) {
    return mktime(tm);
}

void tzset(void) {
    timezone = 0;
    daylight = 0;
    tzname[0] = "UTC";
    tzname[1] = "UTC";
}

static int append_char(char *s, size_t max, size_t *pos, char c) {
    if (*pos + 1 >= max) return 0;
    s[(*pos)++] = c;
    s[*pos] = 0;
    return 1;
}

static int append_int2(char *s, size_t max, size_t *pos, int value) {
    if (value < 0) value = 0;
    return append_char(s, max, pos, (char)('0' + ((value / 10) % 10))) &&
           append_char(s, max, pos, (char)('0' + (value % 10)));
}

static int append_int4(char *s, size_t max, size_t *pos, int value) {
    if (value < 0) value = 0;
    return append_char(s, max, pos, (char)('0' + ((value / 1000) % 10))) &&
           append_char(s, max, pos, (char)('0' + ((value / 100) % 10))) &&
           append_char(s, max, pos, (char)('0' + ((value / 10) % 10))) &&
           append_char(s, max, pos, (char)('0' + (value % 10)));
}

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm) {
    if (!s || max == 0 || !format || !tm) return 0;
    size_t pos = 0;
    s[0] = 0;
    for (size_t i = 0; format[i]; i++) {
        if (format[i] != '%') {
            if (!append_char(s, max, &pos, format[i])) return 0;
            continue;
        }
        char spec = format[++i];
        if (!spec) break;
        int ok = 1;
        switch (spec) {
            case '%': ok = append_char(s, max, &pos, '%'); break;
            case 'Y': ok = append_int4(s, max, &pos, tm->tm_year + 1900); break;
            case 'm': ok = append_int2(s, max, &pos, tm->tm_mon + 1); break;
            case 'd': ok = append_int2(s, max, &pos, tm->tm_mday); break;
            case 'H': ok = append_int2(s, max, &pos, tm->tm_hour); break;
            case 'M': ok = append_int2(s, max, &pos, tm->tm_min); break;
            case 'S': ok = append_int2(s, max, &pos, tm->tm_sec); break;
            case 'F':
                ok = append_int4(s, max, &pos, tm->tm_year + 1900) &&
                     append_char(s, max, &pos, '-') &&
                     append_int2(s, max, &pos, tm->tm_mon + 1) &&
                     append_char(s, max, &pos, '-') &&
                     append_int2(s, max, &pos, tm->tm_mday);
                break;
            case 'T':
                ok = append_int2(s, max, &pos, tm->tm_hour) &&
                     append_char(s, max, &pos, ':') &&
                     append_int2(s, max, &pos, tm->tm_min) &&
                     append_char(s, max, &pos, ':') &&
                     append_int2(s, max, &pos, tm->tm_sec);
                break;
            default:
                ok = append_char(s, max, &pos, '%') &&
                     append_char(s, max, &pos, spec);
                break;
        }
        if (!ok) return 0;
    }
    return pos;
}
