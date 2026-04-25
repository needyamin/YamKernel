/* ============================================================================
 * YamKernel — Kernel Panic Implementation
 * ============================================================================ */

#include <nexus/panic.h>
#include "../lib/kprintf.h"
#include "../drivers/framebuffer.h"

/* Variadic args */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)

NORETURN void kpanic(const char *fmt, ...) {
    cli(); /* Disable interrupts immediately */

    /* Print panic banner */
    kprintf_color(0xFFFF3333,
        "\n"
        "  ╔══════════════════════════════════════════════════╗\n"
        "  ║          !!! YAMKERNEL PANIC !!!                 ║\n"
        "  ╚══════════════════════════════════════════════════╝\n"
        "\n  ");

    /* Print the actual message */
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    /* Simple inline format since we can't use kprintf variadic here easily */
    int pos = 0;
    while (*fmt && pos < 500) {
        if (*fmt == '%' && *(fmt + 1) == 's') {
            const char *s = va_arg(ap, const char *);
            while (*s && pos < 500) buf[pos++] = *s++;
            fmt += 2;
        } else if (*fmt == '%' && *(fmt + 1) == 'l' && *(fmt + 2) == 'u') {
            u64 val = va_arg(ap, u64);
            char tmp[20];
            int ti = 0;
            if (val == 0) { tmp[ti++] = '0'; }
            else { u64 v = val; while (v) { tmp[ti++] = '0' + (v % 10); v /= 10; } }
            for (int j = ti - 1; j >= 0; j--) buf[pos++] = tmp[j];
            fmt += 3;
        } else if (*fmt == '%' && *(fmt + 1) == 'd') {
            int val = va_arg(ap, int);
            if (val < 0) { buf[pos++] = '-'; val = -val; }
            char tmp[12]; int ti = 0;
            if (val == 0) tmp[ti++] = '0';
            else { while (val) { tmp[ti++] = '0' + (val % 10); val /= 10; } }
            for (int j = ti - 1; j >= 0; j--) buf[pos++] = tmp[j];
            fmt += 2;
        } else {
            buf[pos++] = *fmt++;
        }
    }
    buf[pos] = 0;
    va_end(ap);

    kprintf_color(0xFFFF3333, "%s\n\n", buf);
    kprintf_color(0xFFFF8833, "  System halted. Please restart your machine.\n");

    /* Halt forever */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
