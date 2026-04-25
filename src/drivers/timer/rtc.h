/* ============================================================================
 * YamKernel — RTC (Real-Time Clock) Driver
 * Reads date/time from CMOS chip via ports 0x70/0x71
 * ============================================================================ */

#ifndef _DRIVERS_RTC_H
#define _DRIVERS_RTC_H

#include <nexus/types.h>

typedef struct {
    u8  second;
    u8  minute;
    u8  hour;
    u8  day;
    u8  month;
    u16 year;
} rtc_time_t;

/* Read the current date/time from the CMOS RTC */
void rtc_read(rtc_time_t *t);

#endif /* _DRIVERS_RTC_H */
