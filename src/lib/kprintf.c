/* ============================================================================
 * YamKernel — Kernel Printf Implementation
 * Supports: %d, %u, %x, %X, %p, %s, %c, %%, %ld, %lu, %lx
 * ============================================================================ */

#include "kprintf.h"
#include "string.h"
#include "../drivers/serial.h"
#include "../drivers/framebuffer.h"

/* Variadic argument handling (GCC built-in) */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)

/* ---- Number to string conversion ---- */

static int uint_to_str(char *buf, u64 val, int base, int uppercase) {
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[20];
    int i = 0;

    if (val == 0) {
        buf[0] = '0';
        buf[1] = 0;
        return 1;
    }

    while (val > 0) {
        tmp[i++] = digits[val % base];
        val /= base;
    }

    int len = i;
    for (int j = 0; j < len; j++) {
        buf[j] = tmp[len - 1 - j];
    }
    buf[len] = 0;
    return len;
}

static int int_to_str(char *buf, i64 val) {
    if (val < 0) {
        buf[0] = '-';
        return 1 + uint_to_str(buf + 1, (u64)(-val), 10, 0);
    }
    return uint_to_str(buf, (u64)val, 10, 0);
}

/* ---- Core vsnprintf ---- */

static int kvsnprintf(char *buf, usize size, const char *fmt, va_list ap) {
    usize pos = 0;

    #define PUT(c) do { if (pos < size - 1) buf[pos] = (c); pos++; } while(0)

    while (*fmt) {
        if (*fmt != '%') {
            PUT(*fmt++);
            continue;
        }
        fmt++; /* skip '%' */

        /* Check for 'l' modifier */
        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        char tmp[24];
        int len;

        switch (*fmt) {
            case 'd':
            case 'i': {
                i64 val = is_long ? va_arg(ap, i64) : (i64)va_arg(ap, int);
                len = int_to_str(tmp, val);
                for (int i = 0; i < len; i++) PUT(tmp[i]);
                break;
            }
            case 'u': {
                u64 val = is_long ? va_arg(ap, u64) : (u64)va_arg(ap, unsigned);
                len = uint_to_str(tmp, val, 10, 0);
                for (int i = 0; i < len; i++) PUT(tmp[i]);
                break;
            }
            case 'x': {
                u64 val = is_long ? va_arg(ap, u64) : (u64)va_arg(ap, unsigned);
                len = uint_to_str(tmp, val, 16, 0);
                for (int i = 0; i < len; i++) PUT(tmp[i]);
                break;
            }
            case 'X': {
                u64 val = is_long ? va_arg(ap, u64) : (u64)va_arg(ap, unsigned);
                len = uint_to_str(tmp, val, 16, 1);
                for (int i = 0; i < len; i++) PUT(tmp[i]);
                break;
            }
            case 'p': {
                u64 val = (u64)va_arg(ap, void*);
                PUT('0'); PUT('x');
                len = uint_to_str(tmp, val, 16, 0);
                /* Pad to 16 hex digits */
                for (int i = 0; i < 16 - len; i++) PUT('0');
                for (int i = 0; i < len; i++) PUT(tmp[i]);
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                while (*s) PUT(*s++);
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                PUT(c);
                break;
            }
            case '%':
                PUT('%');
                break;
            default:
                PUT('%');
                PUT(*fmt);
                break;
        }
        fmt++;
    }

    #undef PUT

    if (pos < size) buf[pos] = 0;
    else if (size > 0) buf[size - 1] = 0;

    return (int)pos;
}

/* ---- Public API ---- */

static u32 g_kprintf_color = FB_COLOR_WHITE;

void kprintf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    serial_write(buf);
    fb_write(buf, g_kprintf_color);
}

void kprintf_color(u32 color, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    serial_write(buf);
    fb_write(buf, color);
}

int ksnprintf(char *buf, usize size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int result = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return result;
}
