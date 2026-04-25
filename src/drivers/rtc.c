/* ============================================================================
 * YamKernel — RTC (Real-Time Clock) Driver
 * Reads date/time from the CMOS RTC chip.
 *
 * The CMOS is accessed through two ports:
 *   0x70 — Address port (write register index)
 *   0x71 — Data port (read/write register value)
 *
 * Registers: 0x00=sec, 0x02=min, 0x04=hour, 0x07=day, 0x08=month, 0x09=year
 * Register 0x0A bit 7 = update-in-progress flag
 * Register 0x0B bit 2 = binary mode (vs BCD), bit 1 = 24h mode
 * ============================================================================ */

#include "rtc.h"
#include "../lib/kprintf.h"

#define CMOS_ADDR  0x70
#define CMOS_DATA  0x71

static u8 cmos_read(u8 reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static int rtc_updating(void) {
    outb(CMOS_ADDR, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

static u8 bcd_to_bin(u8 bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

void rtc_read(rtc_time_t *t) {
    /* Wait until the update-in-progress flag is clear */
    while (rtc_updating());

    u8 sec   = cmos_read(0x00);
    u8 min   = cmos_read(0x02);
    u8 hour  = cmos_read(0x04);
    u8 day   = cmos_read(0x07);
    u8 month = cmos_read(0x08);
    u8 year  = cmos_read(0x09);

    /* Read a second time to ensure the values are consistent (no mid-update read) */
    u8 sec2, min2, hour2, day2, month2, year2;
    do {
        sec2   = sec;   min2  = min;   hour2  = hour;
        day2   = day;   month2= month; year2  = year;

        while (rtc_updating());

        sec   = cmos_read(0x00);
        min   = cmos_read(0x02);
        hour  = cmos_read(0x04);
        day   = cmos_read(0x07);
        month = cmos_read(0x08);
        year  = cmos_read(0x09);
    } while (sec != sec2 || min != min2 || hour != hour2 ||
             day != day2 || month != month2 || year != year2);

    /* Check if CMOS is in BCD mode (most common) */
    u8 regB = cmos_read(0x0B);
    if (!(regB & 0x04)) {
        /* BCD mode — convert to binary */
        sec   = bcd_to_bin(sec);
        min   = bcd_to_bin(min);
        hour  = bcd_to_bin(hour);
        day   = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year  = bcd_to_bin(year);
    }

    t->second = sec;
    t->minute = min;
    t->hour   = hour;
    t->day    = day;
    t->month  = month;
    t->year   = 2000 + year;  /* CMOS year is 0-99, assume 2000s */
}
