#include "stdio.h"
#include "string.h"
#include "../libyam/syscall.h"
#include <stdarg.h>

static void itoa(unsigned long int n, char* buf, int base, int is_signed) {
    const char *chars = "0123456789abcdef";
    char tmp[64];
    int i = 0;
    
    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    if (is_signed && (long int)n < 0 && base == 10) {
        *buf++ = '-';
        n = (unsigned long int)(-(long int)n);
    }

    while (n > 0) {
        tmp[i++] = chars[n % base];
        n /= base;
    }

    while (i > 0) {
        *buf++ = tmp[--i];
    }
    *buf = '\0';
}

static int vsprintf(char *buf, const char *fmt, va_list args) {
    char *out = buf;
    char tmp[64];

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 'd') {
                int val = va_arg(args, int);
                itoa(val, tmp, 10, 1);
                strcpy(out, tmp);
                out += strlen(tmp);
            } else if (*fmt == 'u') {
                unsigned int val = va_arg(args, unsigned int);
                itoa(val, tmp, 10, 0);
                strcpy(out, tmp);
                out += strlen(tmp);
            } else if (*fmt == 'x') {
                unsigned int val = va_arg(args, unsigned int);
                itoa(val, tmp, 16, 0);
                strcpy(out, tmp);
                out += strlen(tmp);
            } else if (*fmt == 's') {
                char *s = va_arg(args, char *);
                if (!s) s = "(null)";
                strcpy(out, s);
                out += strlen(s);
            } else if (*fmt == 'c') {
                char c = (char)va_arg(args, int);
                *out++ = c;
            } else if (*fmt == 'p') {
                void *ptr = va_arg(args, void *);
                strcpy(out, "0x");
                out += 2;
                itoa((unsigned long)ptr, tmp, 16, 0);
                strcpy(out, tmp);
                out += strlen(tmp);
            } else if (*fmt == '%') {
                *out++ = '%';
            }
        } else {
            *out++ = *fmt;
        }
        fmt++;
    }
    *out = '\0';
    return out - buf;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int len = vsprintf(buf, fmt, args);
    va_end(args);
    return len;
}

int printf(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int len = vsprintf(buf, fmt, args);
    va_end(args);
    
    /* Syscall write to stdout (fd=1) */
    syscall3(SYS_WRITE, 1, (u64)buf, len);
    return len;
}
