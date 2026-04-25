/* ============================================================================
 * YamKernel — PCI Bus Enumeration Driver
 * ============================================================================ */

#ifndef _DRIVERS_PCI_H
#define _DRIVERS_PCI_H

#include <nexus/types.h>

/* PCI Device structure */
typedef struct {
    u16 bus;
    u16 slot;
    u16 func;
    u16 vendor_id;
    u16 device_id;
    u8  class_id;
    u8  subclass_id;
    u8  prog_if;
    u8  header_type;
} pci_device_t;

/* Initialize PCI subsystem (scans and populates graph nodes) */
void pci_init(void);

/* Dump all detected PCI devices to the screen */
void pci_dump(void);

/* Get a specific device by Vendor and Device ID */
pci_device_t* pci_get_device(u16 vendor_id, u16 device_id);

/* Raw PCI configuration space access */
u32 pci_read_32(u8 bus, u8 slot, u8 func, u8 offset);
u16 pci_read_16(u8 bus, u8 slot, u8 func, u8 offset);
u8  pci_read_8(u8 bus, u8 slot, u8 func, u8 offset);
void pci_write_32(u8 bus, u8 slot, u8 func, u8 offset, u32 value);
void pci_write_16(u8 bus, u8 slot, u8 func, u8 offset, u16 value);

#endif /* _DRIVERS_PCI_H */
