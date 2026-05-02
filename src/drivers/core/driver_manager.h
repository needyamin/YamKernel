/* ============================================================================
 * YamKernel - Driver Manager
 * Tracks kernel driver registrations and PCI binding state.
 * ============================================================================ */
#ifndef _DRIVERS_CORE_DRIVER_MANAGER_H
#define _DRIVERS_CORE_DRIVER_MANAGER_H

#include <nexus/types.h>
#include "../bus/pci.h"

typedef enum {
    DRIVER_STATE_EMPTY = 0,
    DRIVER_STATE_REGISTERED,
    DRIVER_STATE_BOUND,
    DRIVER_STATE_MISSING,
} driver_state_t;

typedef struct {
    const char *name;
    const char *subsystem;
    u16 vendor_id;
    u16 device_id;
    u8 class_id;
    u8 subclass_id;
    bool match_vendor_device;
    bool match_class;
    driver_state_t state;
    u32 bound_count;
} kernel_driver_t;

void driver_manager_init(void);
int driver_register_pci(kernel_driver_t driver);
void driver_bind_pci_inventory(void);
void driver_manager_dump(void);
u32 driver_registered_count(void);
u32 driver_bound_count(void);
u32 driver_unbound_pci_count(void);

#endif
