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
    u8  revision_id;
    u8  interrupt_line;
    u8  interrupt_pin;
    u8  secondary_bus;
    u16 command;
    u16 status;
    u8  cap_ptr;
    u8  msi_cap;
    u8  msix_cap;
    bool has_capabilities;
    bool has_msi;
    bool has_msix;
} pci_device_t;

typedef enum {
    PCI_BAR_NONE = 0,
    PCI_BAR_IO,
    PCI_BAR_MMIO32,
    PCI_BAR_MMIO64,
} pci_bar_type_t;

typedef struct {
    pci_bar_type_t type;
    u8  index;
    u64 base;
    u64 size;
    bool prefetchable;
} pci_bar_t;

/* Initialize PCI subsystem (scans and populates graph nodes) */
void pci_init(void);

/* Dump all detected PCI devices to the screen */
void pci_dump(void);

/* Inventory access for driver core */
u32 pci_device_count(void);
pci_device_t *pci_device_at(u32 index);
const char *pci_class_name(u8 class_id);

/* Get a specific device by Vendor and Device ID */
pci_device_t* pci_get_device(u16 vendor_id, u16 device_id);

/* Raw PCI configuration space access */
u32 pci_read_32(u8 bus, u8 slot, u8 func, u8 offset);
u16 pci_read_16(u8 bus, u8 slot, u8 func, u8 offset);
u8  pci_read_8(u8 bus, u8 slot, u8 func, u8 offset);
void pci_write_32(u8 bus, u8 slot, u8 func, u8 offset, u32 value);
void pci_write_16(u8 bus, u8 slot, u8 func, u8 offset, u16 value);

/* Device enablement helpers used by real drivers */
u16 pci_command(pci_device_t *dev);
u16 pci_status(pci_device_t *dev);
void pci_set_command_bits(pci_device_t *dev, u16 bits);
void pci_clear_command_bits(pci_device_t *dev, u16 bits);
void pci_enable_io(pci_device_t *dev);
void pci_enable_mmio(pci_device_t *dev);
void pci_enable_bus_master(pci_device_t *dev);
void pci_disable_legacy_interrupt(pci_device_t *dev);
void pci_enable_legacy_interrupt(pci_device_t *dev);

/* PCI capability list helpers. Returns 0 when the capability is absent. */
u8 pci_find_capability(pci_device_t *dev, u8 cap_id);
bool pci_has_msi(pci_device_t *dev);
bool pci_has_msix(pci_device_t *dev);

/* BAR decoder. Returns false when the BAR is absent or invalid. */
bool pci_read_bar(pci_device_t *dev, u8 bar_index, pci_bar_t *out);

#endif /* _DRIVERS_PCI_H */
