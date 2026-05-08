/* Kernel libc / OS shims for statically linked mbed TLS (built on Linux glibc). */

#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/platform_time.h"

#include "../drivers/timer/pit.h"
#include "../drivers/timer/rtc.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../mem/heap.h"
#include <nexus/types.h>

#ifndef AF_INET
#define AF_INET 2
#endif

void *calloc(size_t nmemb, size_t size) {
    return kcalloc((usize)nmemb, (usize)size);
}

void free(void *ptr) {
    kfree(ptr);
}

int snprintf(char *s, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = kvsnprintf(s, (usize)n, fmt, ap);
    va_end(ap);
    return r;
}

int vsnprintf(char *s, size_t n, const char *fmt, va_list ap) {
    return kvsnprintf(s, (usize)n, fmt, ap);
}

int printf(const char *fmt, ...) {
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    kprintf("%s", buf);
    return 0;
}

typedef long time_ret_t;

static int rtc_year_is_leap(int y) {
    return ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0));
}

static i64 rtc_to_unix_seconds(const rtc_time_t *r) {
    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    i64 days = 0;
    for (int y = 1970; y < (int)r->year; y++)
        days += rtc_year_is_leap(y) ? 366 : 365;
    int leap = rtc_year_is_leap((int)r->year);
    for (unsigned mo = 1; mo < r->month; mo++) {
        int dim = mdays[mo - 1];
        if (mo == 2 && leap)
            dim = 29;
        days += dim;
    }
    days += (int)r->day - 1;
    return days * 86400LL + (i64)r->hour * 3600LL + (i64)r->minute * 60LL +
           (i64)r->second;
}

struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt, struct tm *tm_buf) {
    if (!tt || !tm_buf)
        return NULL;

    long long t = (long long)*tt;
    long long days = t / 86400LL;
    long long rem = t % 86400LL;
    if (rem < 0) {
        rem += 86400LL;
        days--;
    }

    int sec = (int)(rem % 60LL);
    rem /= 60;
    int min = (int)(rem % 60LL);
    int hour = (int)(rem / 60LL);

    int year = 1970;
    for (;;) {
        int diy = rtc_year_is_leap(year) ? 366 : 365;
        if (days >= diy) {
            days -= diy;
            year++;
            if (year > 2400)
                return NULL;
        } else
            break;
    }

    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int month = 1;
    int leap = rtc_year_is_leap(year);
    while (month <= 12) {
        int dim = mdays[month - 1];
        if (month == 2 && leap)
            dim = 29;
        if (days >= dim) {
            days -= dim;
            month++;
        } else
            break;
    }
    int day = (int)days + 1;

    memset(tm_buf, 0, sizeof(*tm_buf));
    tm_buf->tm_year = year - 1900;
    tm_buf->tm_mon = month - 1;
    tm_buf->tm_mday = day;
    tm_buf->tm_hour = hour;
    tm_buf->tm_min = min;
    tm_buf->tm_sec = sec;
    tm_buf->tm_isdst = 0;
    return tm_buf;
}

int putchar(int c) {
    char b[2] = {(char)c, 0};
    kprintf("%s", b);
    return (unsigned char)c;
}

int puts(const char *s) {
    if (!s)
        s = "(null)";
    kprintf("%s\n", s);
    return 0;
}

int rand(void) {
    return (int)(pit_get_ticks() & 0x7fffffffu);
}

time_ret_t time(time_ret_t *tloc) {
    rtc_time_t rt;
    rtc_read(&rt);
    i64 v64 = rtc_to_unix_seconds(&rt);
    time_ret_t v = (time_ret_t)v64;
    if (tloc)
        *tloc = v;
    return v;
}

typedef long kernel_clockid_t;
struct kernel_timespec {
    long tv_sec;
    long tv_nsec;
};

int clock_gettime(kernel_clockid_t clk_id, struct kernel_timespec *tp) {
    (void)clk_id;
    if (!tp)
        return -1;
    u64 ms = pit_uptime_ms();
    tp->tv_sec = (long)(ms / 1000);
    tp->tv_nsec = (long)((ms % 1000) * 1000000ULL);
    return 0;
}

void explicit_bzero(void *buf, size_t len) {
    memset(buf, 0, (usize)len);
}

/*
 * mbed TLS X.509 uses inet_pton(AF_INET, ...) for Subject Alternative Name IPs.
 * IPv4 text only.
 */
int inet_pton(int af, const char *src, void *dst) {
    if (!src || !dst || af != AF_INET)
        return -1;

    u32 parts[4];
    int idx = 0;
    u32 acc = 0;
    int ndig = 0;

    for (const char *p = src;; p++) {
        if (*p >= '0' && *p <= '9') {
            acc = acc * 10u + (u32)(*p - '0');
            if (acc > 255u)
                return 0;
            ndig++;
        } else if (*p == '.' || *p == '\0') {
            if (ndig == 0)
                return 0;
            if (idx >= 4)
                return 0;
            parts[idx++] = acc;
            acc = 0;
            ndig = 0;
            if (*p == '\0')
                break;
        } else {
            return 0;
        }
    }

    if (idx != 4)
        return 0;

    u8 *b = (u8 *)dst;
    b[0] = (u8)parts[0];
    b[1] = (u8)parts[1];
    b[2] = (u8)parts[2];
    b[3] = (u8)parts[3];
    return 1;
}

static int rdrand_ok(unsigned long long *out) {
    unsigned char ok;
    __asm__ volatile("rdrand %0; setc %1" : "=r"(*out), "=qm"(ok) : : "cc");
    return ok ? 0 : -1;
}

static bool cpu_has_rdrand(void) {
    u32 ecx = 0;
    __asm__ volatile("cpuid" : "=c"(ecx) : "a"(1) : "ebx", "edx");
    return (ecx >> 30) & 1u;
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len,
                            size_t *olen) {
    (void)data;
    if (!olen || !output || len == 0)
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = 0;

    if (cpu_has_rdrand()) {
        size_t done = 0;
        while (done < len) {
            unsigned long long v = 0;
            if (rdrand_ok(&v) != 0)
                return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
            size_t chunk = len - done >= 8 ? 8 : len - done;
            for (size_t i = 0; i < chunk; i++)
                output[done++] = (unsigned char)(v >> (i * 8));
        }
        *olen = len;
        return 0;
    }

    /* Weak fallback if RDRAND unavailable (should not happen on x86-64 targets). */
    for (size_t i = 0; i < len; i++)
        output[i] = (unsigned char)(pit_get_ticks() >> ((i & 7u) * 8)) ^ (unsigned char)i;
    *olen = len;
    return 0;
}
