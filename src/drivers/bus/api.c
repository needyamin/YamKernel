/* ============================================================================
 * YamKernel — Advanced Device Bus Skeletons
 * ============================================================================ */
#include "api.h"
#include "../../lib/kprintf.h"

void usb_init(void) { kprintf_color(0xFF00DDFF, "[BUS] USB XHCI/EHCI framework ready\n"); }
void i2c_init(void) { kprintf_color(0xFF0A0A14, "[BUS] I2C core ready\n"); }
void spi_init(void) { kprintf_color(0xFF0A0A14, "[BUS] SPI core ready\n"); }

void i2c_read(u8 addr, u8 reg, u8 *data, u32 len) { (void)addr; (void)reg; (void)data; (void)len; }
void spi_transfer(u8 *tx_buf, u8 *rx_buf, u32 len) { (void)tx_buf; (void)rx_buf; (void)len; }
