/* ============================================================================
 * YamKernel — PIT (Programmable Interval Timer) Driver
 * Provides system tick counter, uptime, and millisecond-precision sleep.
 *
 * The PIT chip (Intel 8253/8254) has 3 channels:
 *   Channel 0 → IRQ 0 (system timer) ← we use this
 *   Channel 1 → DRAM refresh (legacy, unused)
 *   Channel 2 → PC speaker
 *
 * Base frequency: 1,193,182 Hz
 * We divide it down to get our desired tick rate.
 * ============================================================================ */

#include "pit.h"
#include "../../cpu/idt.h"
#include "../../lib/kprintf.h"

/* PIT I/O ports */
#define PIT_CHANNEL_0   0x40
#define PIT_CHANNEL_2   0x42
#define PIT_CMD         0x43

/* PIT base oscillator frequency */
#define PIT_BASE_FREQ   1193182

/* State */
static volatile u64 g_ticks = 0;
static u32 g_pit_hz = 100;

/* IRQ 0 handler — called on every PIT tick */
static void pit_isr(interrupt_frame_t *frame) {
    (void)frame;
    g_ticks++;
    /* EOI is sent by isr_dispatch — don't double-EOI here */
}

void pit_init(u32 frequency_hz) {
    g_pit_hz = frequency_hz;
    g_ticks = 0;

    /* Calculate the divisor */
    u32 divisor = PIT_BASE_FREQ / frequency_hz;
    if (divisor > 65535) divisor = 65535;
    if (divisor < 1) divisor = 1;

    /* Command: Channel 0, lo/hi byte, rate generator (mode 2), binary */
    outb(PIT_CMD, 0x34);

    /* Send divisor (low byte first, then high byte) */
    outb(PIT_CHANNEL_0, (u8)(divisor & 0xFF));
    outb(PIT_CHANNEL_0, (u8)((divisor >> 8) & 0xFF));

    /* Register IRQ 0 handler (vector 32 after PIC remap) */
    idt_register_handler(32, pit_isr);

    kprintf_color(0xFF00FF88, "[PIT] Timer initialized at %u Hz (divisor=%u)\n",
                  frequency_hz, divisor);
}

u64 pit_get_ticks(void) {
    return g_ticks;
}

u64 pit_uptime_seconds(void) {
    return g_ticks / g_pit_hz;
}

u64 pit_uptime_ms(void) {
    return (g_ticks * 1000) / g_pit_hz;
}

void pit_sleep_ms(u32 ms) {
    u64 target = g_ticks + ((u64)ms * g_pit_hz) / 1000;
    while (g_ticks < target) {
        __asm__ volatile ("sti; hlt");
    }
}

u32 pit_get_frequency(void) {
    return g_pit_hz;
}
