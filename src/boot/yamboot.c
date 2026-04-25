/* ============================================================================
 * YamBoot — Custom Boot Manager
 * Runs immediately after Limine hands control to the kernel and the framebuffer
 * is up. Polls the PS/2 keyboard directly (no IDT yet), draws a branded menu,
 * and lets the user pick a boot mode.
 * ============================================================================ */

#include "yamboot.h"
#include "../lib/kprintf.h"
#include "../drivers/video/framebuffer.h"
#include <nexus/types.h>

int g_yamboot_safe = 0;

#define C_BG      0xFF0A0A14
#define C_BORDER  0xFF00FF88
#define C_TITLE   0xFFFFDD00
#define C_HEAD    0xFF00DDFF
#define C_TEXT    0xFFCCCCCC
#define C_DIM     0xFF666666
#define C_OK      0xFF00FF88

/* Non-blocking PS/2 scancode poll. -1 if no key in buffer. */
static int kbd_poll(void) {
    if (!(inb(0x64) & 0x01)) return -1;
    return inb(0x60);
}

static void draw(void) {
    fb_clear(C_BG);
    kprintf("\n");
    kprintf_color(C_BORDER, "  +==================================================+\n");
    kprintf_color(C_TITLE,  "                YamBoot - Boot Manager\n");
    kprintf_color(C_HEAD,   "       YamKernel v0.2.0 | Graph-Based Adaptive OS\n");
    kprintf_color(C_BORDER, "  +==================================================+\n\n");
    kprintf_color(C_OK,     "    [1] "); kprintf_color(C_TEXT, "Normal Boot       (default)\n");
    kprintf_color(C_OK,     "    [2] "); kprintf_color(C_TEXT, "Safe Mode         (skip non-essential drivers)\n");
    kprintf_color(C_OK,     "    [3] "); kprintf_color(C_TEXT, "Reboot\n\n");
    kprintf_color(C_DIM,    "    Press 1, 2, 3 or Enter to continue...\n");
    kprintf_color(C_BORDER, "  +==================================================+\n");
}

yamboot_choice_t yamboot_show(void) {
    /* Drain any stale scancodes from the PS/2 buffer */
    while (inb(0x64) & 0x01) inb(0x60);

    draw();

    for (;;) {
        int sc = kbd_poll();
        if (sc < 0) continue;
        if (sc & 0x80) continue;          /* ignore key-release events */
        switch (sc) {
            case 0x02:                    /* '1' */
            case 0x1C: return YAMBOOT_NORMAL;   /* Enter */
            case 0x03:                    /* '2' */
                g_yamboot_safe = 1;
                return YAMBOOT_SAFE;
            case 0x04: return YAMBOOT_REBOOT;   /* '3' */
        }
    }
}
