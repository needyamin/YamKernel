/* ============================================================================
 * YamKernel — PS/2 Keyboard Driver Implementation
 * ============================================================================ */

#include "keyboard.h"
#include "evdev.h"
#include "../../cpu/idt.h"
#include "../../lib/kprintf.h"

/* PS/2 Ports */
#define PS2_DATA_PORT 0x60
#define PS2_CMD_PORT  0x64

/* Ring buffer size (must be power of 2) */
#define KBD_BUF_SIZE 256

/* Scancode map (Set 1) — unshifted */
static const char scancode_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b', /* 0x00 - 0x0E */
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',    /* 0x0F - 0x1C */
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',  0,     /* 0x1D - 0x2A */
    '\\','z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ', /* 0x2B - 0x39 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,               /* F1-F10 etc */
    '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0            /* Keypad */
};

/* Scancode map (Set 1) — shifted */
static const char scancode_ascii_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',  0,
    '|','Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' '
};

/* State */
static volatile bool shift_pressed = false;
static volatile bool caps_pressed  = false;
static volatile bool expecting_extended = false;

/* Ring Buffer */
static volatile char kbd_buffer[KBD_BUF_SIZE];
static volatile u32 kbd_head = 0;
static volatile u32 kbd_tail = 0;

static void kbd_push(char c) {
    u32 next = (kbd_tail + 1) % KBD_BUF_SIZE;
    if (next != kbd_head) { /* Don't overwrite if full */
        kbd_buffer[kbd_tail] = c;
        kbd_tail = next;
    }
}

/* Interrupt handler (IRQ 1 mapped to vector 33) */
static void keyboard_isr(interrupt_frame_t *frame) {
    (void)frame;
    
    u8 scancode = inb(PS2_DATA_PORT);

    if (scancode == 0xE0) {
        expecting_extended = true;
        return;
    }

    /* Check released keys (bit 7 set = key released) */
    if (scancode & 0x80) {
        u8 released = scancode & 0x7F;
        evdev_push_event(EV_KEY, released, KEY_RELEASED);
        if (expecting_extended) {
            expecting_extended = false;
            return;
        }
        if (released == 0x2A || released == 0x36) { /* L-Shift or R-Shift */
            shift_pressed = false;
        }
        return;
    }

    /* Handle extended keys (Arrows) */
    if (expecting_extended) {
        expecting_extended = false;
        switch (scancode) {
            case 0x48: kbd_push('\x11'); break; /* Up Arrow -> DC1 */
            case 0x50: kbd_push('\x12'); break; /* Down Arrow -> DC2 */
            case 0x4B: kbd_push('\x13'); break; /* Left Arrow -> DC3 */
            case 0x4D: kbd_push('\x14'); break; /* Right Arrow -> DC4 */
        }
        return;
    }

    /* Handle modifier keys */
    if (scancode == 0x2A || scancode == 0x36) {
        evdev_push_event(EV_KEY, scancode, KEY_PRESSED);
        shift_pressed = true;
        return;
    }
    if (scancode == 0x3A) { /* Caps lock toggle */
        caps_pressed = !caps_pressed;
        return;
    }

    /* Push evdev key-press event */
    evdev_push_event(EV_KEY, scancode, KEY_PRESSED);
    evdev_push_event(EV_SYN, 0, 0);  /* Sync event */

    /* Translate to ASCII */
    if (scancode < 128) {
        char c;
        bool use_shift = shift_pressed;
        
        /* Caps lock applies shift to letters only */
        char base = scancode_ascii[scancode];
        if (caps_pressed && base >= 'a' && base <= 'z') {
            use_shift = !use_shift;
        }

        if (use_shift) {
            c = scancode_ascii_shift[scancode];
        } else {
            c = base;
        }

        if (c) kbd_push(c);
    }
}

/* Wait for PS/2 controller input buffer to be clear */
static void ps2_wait_write(void) {
    int timeout = 10000;
    while ((inb(PS2_CMD_PORT) & 0x02) && --timeout > 0) {
        io_wait();
    }
}

/* Wait for PS/2 controller output buffer to be full */
static void ps2_wait_read(void) {
    int timeout = 10000;
    while (!(inb(PS2_CMD_PORT) & 0x01) && --timeout > 0) {
        io_wait();
    }
}

void keyboard_init(void) {
    /* Flush the PS/2 output buffer — discard any stale data */
    while (inb(PS2_CMD_PORT) & 0x01) {
        inb(PS2_DATA_PORT);
    }

    /* Disable both PS/2 device ports during setup */
    ps2_wait_write();
    outb(PS2_CMD_PORT, 0xAD);  /* Disable port 1 */
    ps2_wait_write();
    outb(PS2_CMD_PORT, 0xA7);  /* Disable port 2 */

    /* Flush again */
    while (inb(PS2_CMD_PORT) & 0x01) {
        inb(PS2_DATA_PORT);
    }

    /* Read the PS/2 Controller Configuration Byte */
    ps2_wait_write();
    outb(PS2_CMD_PORT, 0x20);
    ps2_wait_read();
    u8 config = inb(PS2_DATA_PORT);

    /* Enable IRQ1 (bit 0), keep IRQ12 (bit 1), enable clocks, KEEP translation */
    config |= 0x01;         /* Enable IRQ1 for keyboard */
    config |= 0x02;         /* Enable IRQ12 for mouse */
    config &= ~(1 << 4);    /* Enable clock for port 1 */
    config &= ~(1 << 5);    /* Enable clock for port 2 */
    config |= (1 << 6);     /* ENABLE translation (Set 2→Set 1) — our tables are Set 1! */

    ps2_wait_write();
    outb(PS2_CMD_PORT, 0x60);  /* Write config command */
    ps2_wait_write();
    outb(PS2_DATA_PORT, config);

    /* Re-enable port 1 (keyboard) */
    ps2_wait_write();
    outb(PS2_CMD_PORT, 0xAE);

    /* Re-enable port 2 (mouse) */
    ps2_wait_write();
    outb(PS2_CMD_PORT, 0xA8);

    /* Register ISR for IRQ 1 (vector 33) */
    idt_register_handler(33, keyboard_isr);

    kprintf_color(0xFF00FF88, "[KBD] PS/2 Keyboard initialized (IRQ 1)\n");
}

bool keyboard_has_key(void) {
    return kbd_head != kbd_tail;
}

char keyboard_get_char(void) {
    /* Block until key is pressed.
     * sti;hlt is critical: if interrupts were disabled (cli) before
     * calling this, plain hlt would deadlock forever. sti enables
     * interrupts, then hlt waits for the next one atomically. */
    while (kbd_head == kbd_tail) {
        __asm__ volatile ("sti; hlt");
    }

    /* Fetch key from ring buffer safely */
    cli();
    char c = kbd_buffer[kbd_head];
    kbd_head = (kbd_head + 1) % KBD_BUF_SIZE;
    sti();

    return c;
}
