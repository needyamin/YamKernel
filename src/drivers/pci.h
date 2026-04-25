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

#endif /* _DRIVERS_PCI_H */
