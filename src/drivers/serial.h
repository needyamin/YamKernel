/* ============================================================================
 * YamKernel — Serial Port Driver (COM1)
 * Used for early debug output to QEMU console
 * ============================================================================ */

#ifndef _DRIVERS_SERIAL_H
#define _DRIVERS_SERIAL_H

#include <nexus/types.h>

#define SERIAL_COM1 0x3F8

void serial_init(void);
void serial_putchar(char c);
void serial_write(const char *str);

#endif /* _DRIVERS_SERIAL_H */
