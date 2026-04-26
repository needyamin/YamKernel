/* ============================================================================
 * YamKernel — Kernel Debug Logging Implementation
 *
 * All output goes ONLY to the serial port so it works even when the
 * framebuffer is disabled, corrupted, or not yet initialized.
 * ============================================================================ */

#include "kdebug.h"
#include "../drivers/serial/serial.h"
#include "../drivers/timer/pit.h"

/* Variadic args */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)

/* Level prefix strings */
static const char *level_prefix[] = {
    "[TRACE]",
    "[DEBUG]",
    "[INFO ]",
    "[WARN ]",
    "[ERROR]",
};

/* ---- Simple serial number printer ---- */
static void serial_put_u64(u64 val) {
    if (val == 0) { serial_putchar('0'); return; }
    char buf[20];
    int i = 0;
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (--i >= 0) serial_putchar(buf[i]);
}

static void serial_put_hex(u64 val, int width) {
    const char *hex = "0123456789abcdef";
    serial_write("0x");
    /* Print at least `width` hex digits */
    char buf[16];
    int i = 0;
    if (val == 0) { buf[i++] = '0'; }
    else { while (val) { buf[i++] = hex[val & 0xf]; val >>= 4; } }
    /* Pad with leading zeros */
    for (int p = i; p < width; p++) serial_putchar('0');
    while (--i >= 0) serial_putchar(buf[i]);
}

static void serial_put_i64(i64 val) {
    if (val < 0) { serial_putchar('-'); val = -val; }
    serial_put_u64((u64)val);
}

/* ---- Core log function ---- */
void kdebug_log(int level, const char *tag, const char *fmt, ...) {
    if (level < 0) level = 0;
    if (level > 4) level = 4;

    /* Print: [LEVEL] [TAG] message\n */
    serial_write(level_prefix[level]);
    serial_write(" [");
    serial_write(tag);
    serial_write("] ");

    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            serial_putchar(*fmt++);
            continue;
        }
        fmt++; /* skip '%' */

        /* Long modifier: handle %l, %ll, %llu, %lld etc. */
        int is_long = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; }
        if (*fmt == 'l') { fmt++; } /* skip second 'l' in %llu/%lld */

        switch (*fmt) {
            case 'd':
            case 'i': {
                i64 v = is_long ? va_arg(ap, i64) : (i64)va_arg(ap, int);
                serial_put_i64(v);
                break;
            }
            case 'u': {
                u64 v = is_long ? va_arg(ap, u64) : (u64)va_arg(ap, unsigned);
                serial_put_u64(v);
                break;
            }
            case 'x':
            case 'X': {
                u64 v = is_long ? va_arg(ap, u64) : (u64)va_arg(ap, unsigned);
                serial_put_hex(v, is_long ? 16 : 1);
                break;
            }
            case 'p': {
                void *ptr = va_arg(ap, void *);
                u64 v = (u64)ptr;
                serial_put_hex(v, 16);
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                serial_write(s ? s : "(null)");
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                serial_putchar(c);
                break;
            }
            case '%':
                serial_putchar('%');
                break;
            default:
                serial_putchar('%');
                serial_putchar(*fmt);
                break;
        }
        fmt++;
    }
    va_end(ap);

    serial_write("\r\n");
}

/* ---- Hex dump utility ---- */
void kdebug_hexdump(const char *tag, const void *addr, u32 len) {
    const u8 *p = (const u8 *)addr;
    const char *hex = "0123456789abcdef";

    KINFO(tag, "hexdump at %p, %u bytes:", addr, len);

    for (u32 off = 0; off < len; off += 16) {
        /* Offset */
        serial_write("  ");
        serial_put_hex(off, 4);
        serial_write(": ");

        /* Hex bytes */
        for (u32 i = 0; i < 16; i++) {
            if (off + i < len) {
                serial_putchar(hex[p[off + i] >> 4]);
                serial_putchar(hex[p[off + i] & 0xf]);
                serial_putchar(' ');
            } else {
                serial_write("   ");
            }
            if (i == 7) serial_putchar(' ');
        }

        /* ASCII */
        serial_write(" |");
        for (u32 i = 0; i < 16 && off + i < len; i++) {
            u8 c = p[off + i];
            serial_putchar((c >= 0x20 && c <= 0x7e) ? c : '.');
        }
        serial_write("|\r\n");
    }
}
