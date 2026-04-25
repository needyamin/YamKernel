/* ============================================================================
 * YamKernel — PS/2 Mouse Driver Implementation
 * ============================================================================ */

#include "mouse.h"
#include "../../cpu/idt.h"
#include "../../lib/kprintf.h"

#define PS2_DATA_PORT 0x60
#define PS2_CMD_PORT  0x64

static volatile u8 mouse_cycle = 0;
static volatile i8 mouse_byte[3];
static volatile i32 mouse_x = 0;
static volatile i32 mouse_y = 0;
static volatile bool mouse_left = false;
static volatile bool mouse_right = false;

/* Wait for PS/2 controller — input buffer clear (ready for write) */
static void mouse_wait_write(void) {
    int timeout = 100000;
    while ((inb(PS2_CMD_PORT) & 0x02) && --timeout > 0);
}

/* Wait for PS/2 controller — output buffer full (data ready for read) */
static void mouse_wait_read(void) {
    int timeout = 100000;
    while (!(inb(PS2_CMD_PORT) & 0x01) && --timeout > 0);
}

/* Send command to mouse (via PS/2 controller) and wait for ACK.
   Returns false if no ACK arrives within the timeout (mouse not present). */
static bool mouse_write(u8 data) {
    mouse_wait_write();
    outb(PS2_CMD_PORT, 0xD4);
    mouse_wait_write();
    outb(PS2_DATA_PORT, data);
    int timeout = 100000;
    while (!(inb(PS2_CMD_PORT) & 0x01) && --timeout > 0) io_wait();
    if (timeout <= 0) return false;
    inb(PS2_DATA_PORT);
    return true;
}

/* Mouse IRQ 12 Handler (vector 44) */
static void mouse_isr(interrupt_frame_t *frame) {
    (void)frame;

    /* Only read if data is actually available */
    u8 status = inb(PS2_CMD_PORT);
    if (!(status & 0x01)) return;       /* No data */
    if (!(status & 0x20)) return;       /* Not from mouse (from keyboard) */

    u8 data = inb(PS2_DATA_PORT);

    switch (mouse_cycle) {
        case 0:
            /* First byte: buttons + sign + overflow flags */
            if (!(data & 0x08)) break;  /* Bit 3 must be 1 — sync check */
            mouse_byte[0] = (i8)data;
            mouse_cycle = 1;
            break;
        case 1:
            /* Second byte: X movement */
            mouse_byte[1] = (i8)data;
            mouse_cycle = 2;
            break;
        case 2:
            /* Third byte: Y movement — complete packet */
            mouse_byte[2] = (i8)data;
            mouse_cycle = 0;

            /* Update position */
            mouse_x += mouse_byte[1];
            mouse_y -= mouse_byte[2]; /* Y is inverted in PS/2 protocol */

            /* Clamp to safe range */
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;

            /* Button states */
            mouse_left  = (mouse_byte[0] & 0x01) ? true : false;
            mouse_right = (mouse_byte[0] & 0x02) ? true : false;
            break;
    }
}

void mouse_init(void) {
    mouse_cycle = 0;
    mouse_x = 0;
    mouse_y = 0;

    /* Enable auxiliary device (port 2) */
    mouse_wait_write();
    outb(PS2_CMD_PORT, 0xA8);

    /* Get Controller Configuration Byte */
    mouse_wait_write();
    outb(PS2_CMD_PORT, 0x20);
    mouse_wait_read();
    u8 config = inb(PS2_DATA_PORT);
    
    /* Enable IRQ12 (bit 1) and port 2 clock (clear bit 5) */
    config |= 0x02;        /* Enable IRQ12 */
    config &= ~(1 << 5);   /* Enable port 2 clock */

    mouse_wait_write();
    outb(PS2_CMD_PORT, 0x60);
    mouse_wait_write();
    outb(PS2_DATA_PORT, config);

    /* Tell mouse to use default settings + enable streaming.
       If no PS/2 mouse is attached, we just skip registration cleanly. */
    if (!mouse_write(0xF6) || !mouse_write(0xF4)) {
        kprintf_color(0xFFFF8833, "[MOUSE] No PS/2 mouse detected (skipped)\n");
        return;
    }

    idt_register_handler(44, mouse_isr);
    kprintf_color(0xFF00FF88, "[MOUSE] PS/2 Mouse initialized (IRQ 12)\n");
}
