/* YamKernel - HPET discovery */
#ifndef _DRIVERS_TIMER_HPET_H
#define _DRIVERS_TIMER_HPET_H

#include <nexus/types.h>

void hpet_init(u64 hhdm_offset);
bool hpet_available(void);
u64  hpet_frequency_hz(void);

#endif
