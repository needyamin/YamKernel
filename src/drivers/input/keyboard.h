/* ============================================================================
 * YamKernel — PS/2 Keyboard Driver
 * ============================================================================ */

#ifndef _DRIVERS_KEYBOARD_H
#define _DRIVERS_KEYBOARD_H

#include <nexus/types.h>

/* Initialize the PS/2 keyboard driver (registers IRQ 1) */
void keyboard_init(void);

/* Check if a character is available in the keyboard buffer */
bool keyboard_has_key(void);

/* Block until a character is available and return it */
char keyboard_get_char(void);

#endif /* _DRIVERS_KEYBOARD_H */
