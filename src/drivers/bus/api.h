/* ============================================================================
 * YamKernel — Advanced Device Bus Skeletons
 * Interface APIs for USB, I2C, SPI.
 * ============================================================================ */
#ifndef _DRIVERS_BUS_API_H
#define _DRIVERS_BUS_API_H

#include <nexus/types.h>

/* USB */
typedef struct {
    u16 idVendor;
    u16 idProduct;
} usb_device_t;
void usb_init(void);

/* I2C */
void i2c_init(void);
void i2c_read(u8 addr, u8 reg, u8 *data, u32 len);

/* SPI */
void spi_init(void);
void spi_transfer(u8 *tx_buf, u8 *rx_buf, u32 len);

#endif
