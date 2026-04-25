/* ============================================================================
 * YamKernel — Serial Port Driver (COM1)
 * ============================================================================ */

#include "serial.h"

void serial_init(void) {
    outb(SERIAL_COM1 + 1, 0x00);    /* Disable interrupts */
    outb(SERIAL_COM1 + 3, 0x80);    /* Enable DLAB (set baud rate divisor) */
    outb(SERIAL_COM1 + 0, 0x01);    /* Divisor lo: 115200 baud */
    outb(SERIAL_COM1 + 1, 0x00);    /* Divisor hi */
    outb(SERIAL_COM1 + 3, 0x03);    /* 8 bits, no parity, one stop bit */
    outb(SERIAL_COM1 + 2, 0xC7);    /* Enable FIFO, clear, 14-byte threshold */
    outb(SERIAL_COM1 + 4, 0x0B);    /* IRQs enabled, RTS/DSR set */
    outb(SERIAL_COM1 + 4, 0x0F);    /* Set in normal operation mode */
}

static int serial_transmit_ready(void) {
    return inb(SERIAL_COM1 + 5) & 0x20;
}

void serial_putchar(char c) {
    while (!serial_transmit_ready());
    outb(SERIAL_COM1, (u8)c);
}

void serial_write(const char *str) {
    while (*str) {
        if (*str == '\n')
            serial_putchar('\r');
        serial_putchar(*str++);
    }
}
