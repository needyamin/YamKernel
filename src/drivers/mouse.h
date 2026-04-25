/* ============================================================================
 * YamKernel — PS/2 Mouse Driver
 * ============================================================================ */

#ifndef _DRIVERS_MOUSE_H
#define _DRIVERS_MOUSE_H

#include <nexus/types.h>

/* Initialize the PS/2 mouse driver (IRQ 12) */
void mouse_init(void);

#endif /* _DRIVERS_MOUSE_H */
