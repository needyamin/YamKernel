/* ============================================================================
 * YamKernel - Kernel Panic Implementation
 * ============================================================================ */

#include <nexus/panic.h>
#include "../cpu/idt.h"
#include "../lib/kprintf.h"

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)

static void panic_vformat(char *buf, usize cap, const char *fmt, va_list ap) {
    int pos = 0;
    while (*fmt && pos < (int)cap - 1) {
        if (*fmt == '%' && *(fmt + 1) == 's') {
            const char *s = va_arg(ap, const char *);
            while (s && *s && pos < (int)cap - 1) buf[pos++] = *s++;
            fmt += 2;
        } else if (*fmt == '%' && *(fmt + 1) == 'l' && *(fmt + 2) == 'u') {
            u64 val = va_arg(ap, u64);
            char tmp[20];
            int ti = 0;
            if (val == 0) tmp[ti++] = '0';
            while (val) {
                tmp[ti++] = '0' + (val % 10);
                val /= 10;
            }
            for (int j = ti - 1; j >= 0 && pos < (int)cap - 1; j--) buf[pos++] = tmp[j];
            fmt += 3;
        } else if (*fmt == '%' && *(fmt + 1) == 'l' && *(fmt + 2) == 'x') {
            u64 val = va_arg(ap, u64);
            char tmp[16];
            int ti = 0;
            if (val == 0) tmp[ti++] = '0';
            while (val) {
                tmp[ti++] = "0123456789abcdef"[val & 0xF];
                val >>= 4;
            }
            for (int j = ti - 1; j >= 0 && pos < (int)cap - 1; j--) buf[pos++] = tmp[j];
            fmt += 3;
        } else if (*fmt == '%' && *(fmt + 1) == 'd') {
            int val = va_arg(ap, int);
            if (val < 0 && pos < (int)cap - 1) {
                buf[pos++] = '-';
                val = -val;
            }
            char tmp[12];
            int ti = 0;
            if (val == 0) tmp[ti++] = '0';
            while (val) {
                tmp[ti++] = '0' + (val % 10);
                val /= 10;
            }
            for (int j = ti - 1; j >= 0 && pos < (int)cap - 1; j--) buf[pos++] = tmp[j];
            fmt += 2;
        } else {
            buf[pos++] = *fmt++;
        }
    }
    buf[pos] = 0;
}

static void panic_banner(void) {
    kprintf_color(0xFFFF3333,
        "\n"
        "  +--------------------------------------------------+\n"
        "  |              !!! YAMKERNEL PANIC !!!             |\n"
        "  +--------------------------------------------------+\n"
        "\n  ");
}

static void panic_print_frame(const interrupt_frame_t *f) {
    if (!f) return;
    kprintf_color(0xFFFF8833,
        "\n  Register dump:\n"
        "    RIP=0x%lx CS=0x%lx RFLAGS=0x%lx\n"
        "    RSP=0x%lx SS=0x%lx INT=%lu ERR=0x%lx\n"
        "    RAX=0x%lx RBX=0x%lx RCX=0x%lx RDX=0x%lx\n"
        "    RSI=0x%lx RDI=0x%lx RBP=0x%lx\n"
        "    R8 =0x%lx R9 =0x%lx R10=0x%lx R11=0x%lx\n"
        "    R12=0x%lx R13=0x%lx R14=0x%lx R15=0x%lx\n",
        f->rip, f->cs, f->rflags,
        f->rsp, f->ss, f->int_no, f->error_code,
        f->rax, f->rbx, f->rcx, f->rdx,
        f->rsi, f->rdi, f->rbp,
        f->r8, f->r9, f->r10, f->r11,
        f->r12, f->r13, f->r14, f->r15);
}

NORETURN void kpanic(const char *fmt, ...) {
    cli();
    panic_banner();

    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    panic_vformat(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    kprintf_color(0xFFFF3333, "%s\n\n", buf);
    kprintf_color(0xFFFF8833, "  System halted. Please restart your machine.\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

NORETURN void kpanic_with_frame(const interrupt_frame_t *frame, const char *fmt, ...) {
    cli();
    panic_banner();

    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    panic_vformat(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    kprintf_color(0xFFFF3333, "%s\n", buf);
    panic_print_frame(frame);
    kprintf_color(0xFFFF8833, "\n  System halted. Please restart your machine.\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
