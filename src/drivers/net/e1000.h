/* ============================================================================
 * YamKernel — Intel e1000 Gigabit Ethernet Driver
 * ============================================================================ */
#ifndef _DRIVERS_E1000_H
#define _DRIVERS_E1000_H

#include <nexus/types.h>

#define E1000_REG_CTRL     0x00000
#define E1000_REG_STATUS   0x00008
#define E1000_REG_EEPROM   0x00014
#define E1000_REG_CTRL_EXT 0x00018
#define E1000_REG_IMASK    0x000D0
#define E1000_REG_RCTRL    0x00100
#define E1000_REG_RXDESCLO 0x02800
#define E1000_REG_RXDESCHI 0x02804
#define E1000_REG_RXDESCLEN 0x02808
#define E1000_REG_RXDESCHEAD 0x02810
#define E1000_REG_RXDESCTAIL 0x02818

#define E1000_REG_TCTRL    0x00400
#define E1000_REG_TXDESCLO 0x03800
#define E1000_REG_TXDESCHI 0x03804
#define E1000_REG_TXDESCLEN 0x03808
#define E1000_REG_TXDESCHEAD 0x03810
#define E1000_REG_TXDESCTAIL 0x03818

#define E1000_REG_MTA      0x05200
#define E1000_REG_RAL      0x05400
#define E1000_REG_RAH      0x05404

/* Direct Memory Access Rings */
typedef struct {
    u64 addr;
    u16 length;
    u16 checksum;
    u8 status;
    u8 errors;
    u16 special;
} PACKED e1000_rx_desc_t;

typedef struct {
    u64 addr;
    u16 length;
    u8 cso;
    u8 cmd;
    u8 status;
    u8 css;
    u16 special;
} PACKED e1000_tx_desc_t;

void e1000_init(void);

#endif
