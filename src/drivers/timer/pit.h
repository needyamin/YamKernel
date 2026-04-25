/* ============================================================================
 * YamKernel — PIT (Programmable Interval Timer) Driver
 * Channel 0 at configurable frequency, IRQ 0 → vector 32
 * ============================================================================ */

#ifndef _DRIVERS_PIT_H
#define _DRIVERS_PIT_H

#include <nexus/types.h>

/* Initialize the PIT at the given frequency (Hz) and register IRQ0 handler */
void pit_init(u32 frequency_hz);

/* Get the number of ticks since boot */
u64 pit_get_ticks(void);

/* Get uptime in seconds */
u64 pit_uptime_seconds(void);

/* Get uptime in milliseconds */
u64 pit_uptime_ms(void);

/* Blocking sleep for the given number of milliseconds */
void pit_sleep_ms(u32 ms);

/* Get configured frequency */
u32 pit_get_frequency(void);

#endif /* _DRIVERS_PIT_H */
