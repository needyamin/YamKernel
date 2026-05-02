/* ============================================================================
 * YamKernel - Driver Manager
 * ============================================================================ */
#include "driver_manager.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"

#define MAX_KERNEL_DRIVERS 32

static kernel_driver_t g_drivers[MAX_KERNEL_DRIVERS];
static u32 g_driver_count;
static u32 g_bound_count;
static u32 g_unbound_pci_count;

static bool pci_driver_matches(const kernel_driver_t *drv, const pci_device_t *dev) {
    if (!drv || !dev) return false;
    if (drv->match_vendor_device &&
        drv->vendor_id == dev->vendor_id &&
        drv->device_id == dev->device_id) {
        return true;
    }
    if (drv->match_class &&
        drv->class_id == dev->class_id &&
        drv->subclass_id == dev->subclass_id) {
        return true;
    }
    return false;
}

static const char *bar_type_name(pci_bar_type_t type) {
    switch (type) {
        case PCI_BAR_IO:     return "io";
        case PCI_BAR_MMIO32: return "mmio32";
        case PCI_BAR_MMIO64: return "mmio64";
        default:             return "none";
    }
}

static void log_device_bars(const pci_device_t *dev) {
    if (!dev) return;
    kprintf("[DRIVER]  irq line=%u pin=%u command=0x%x caps:%s%s%s\n",
            dev->interrupt_line, dev->interrupt_pin, dev->command,
            dev->has_capabilities ? " list" : " none",
            pci_has_msi((pci_device_t *)dev) ? " msi" : "",
            pci_has_msix((pci_device_t *)dev) ? " msix" : "");
    for (u8 bar = 0; bar < 6; bar++) {
        pci_bar_t info;
        if (pci_read_bar((pci_device_t *)dev, bar, &info)) {
            kprintf("[DRIVER]  bar%u type=%s base=0x%lx size=0x%lx prefetch=%d\n",
                    bar, bar_type_name(info.type), info.base, info.size,
                    info.prefetchable ? 1 : 0);
            if (info.type == PCI_BAR_MMIO64) bar++;
        }
    }
}

void driver_manager_init(void) {
    memset(g_drivers, 0, sizeof(g_drivers));
    g_driver_count = 0;
    g_bound_count = 0;
    g_unbound_pci_count = 0;

    driver_register_pci((kernel_driver_t){
        .name = "e1000",
        .subsystem = "net",
        .vendor_id = 0x8086,
        .device_id = 0x100E,
        .match_vendor_device = true,
    });
    driver_register_pci((kernel_driver_t){
        .name = "xhci",
        .subsystem = "usb",
        .class_id = 0x0C,
        .subclass_id = 0x03,
        .match_class = true,
    });
    driver_register_pci((kernel_driver_t){
        .name = "vga-framebuffer",
        .subsystem = "display",
        .class_id = 0x03,
        .subclass_id = 0x00,
        .match_class = true,
    });
    driver_register_pci((kernel_driver_t){
        .name = "ahci",
        .subsystem = "block",
        .class_id = 0x01,
        .subclass_id = 0x06,
        .match_class = true,
    });
    driver_register_pci((kernel_driver_t){
        .name = "virtio-blk",
        .subsystem = "block",
        .vendor_id = 0x1AF4,
        .device_id = 0x1001,
        .match_vendor_device = true,
    });
    driver_register_pci((kernel_driver_t){
        .name = "virtio-blk-modern",
        .subsystem = "block",
        .vendor_id = 0x1AF4,
        .device_id = 0x1042,
        .match_vendor_device = true,
    });

    kprintf("[DRIVER] manager initialized: %u built-in driver records\n", g_driver_count);
}

int driver_register_pci(kernel_driver_t driver) {
    if (g_driver_count >= MAX_KERNEL_DRIVERS) return -1;
    driver.state = DRIVER_STATE_REGISTERED;
    driver.bound_count = 0;
    g_drivers[g_driver_count++] = driver;
    return 0;
}

void driver_bind_pci_inventory(void) {
    g_bound_count = 0;
    g_unbound_pci_count = 0;

    for (u32 i = 0; i < g_driver_count; i++) {
        g_drivers[i].state = DRIVER_STATE_REGISTERED;
        g_drivers[i].bound_count = 0;
    }

    for (u32 i = 0; i < pci_device_count(); i++) {
        pci_device_t *dev = pci_device_at(i);
        bool bound = false;
        for (u32 d = 0; d < g_driver_count; d++) {
            if (pci_driver_matches(&g_drivers[d], dev)) {
                g_drivers[d].state = DRIVER_STATE_BOUND;
                g_drivers[d].bound_count++;
                g_bound_count++;
                bound = true;
                kprintf("[DRIVER] bind pci %02x:%02x.%u %04x:%04x class=%02x:%02x -> %s/%s\n",
                        dev->bus, dev->slot, dev->func,
                        dev->vendor_id, dev->device_id,
                        dev->class_id, dev->subclass_id,
                        g_drivers[d].subsystem, g_drivers[d].name);
                if (dev->class_id == 0x01 || dev->class_id == 0x02 ||
                    dev->class_id == 0x03 || dev->class_id == 0x0C) {
                    log_device_bars(dev);
                }
                break;
            }
        }
        if (!bound) {
            g_unbound_pci_count++;
            kprintf("[DRIVER] unbound pci %02x:%02x.%u %04x:%04x class=%02x:%02x %s\n",
                    dev->bus, dev->slot, dev->func,
                    dev->vendor_id, dev->device_id,
                    dev->class_id, dev->subclass_id,
                    pci_class_name(dev->class_id));
        }
    }

    kprintf("[DRIVER] inventory: pci=%u bound=%u unbound=%u drivers=%u\n",
            pci_device_count(), g_bound_count, g_unbound_pci_count, g_driver_count);
}

void driver_manager_dump(void) {
    kprintf_color(0xFF00DDFF, "\n[DRIVER] === Driver Inventory ===\n");
    kprintf("  Registered: %u | Bound devices: %u | Unbound PCI: %u\n",
            g_driver_count, g_bound_count, g_unbound_pci_count);
    for (u32 i = 0; i < g_driver_count; i++) {
        kernel_driver_t *d = &g_drivers[i];
        kprintf("  %-16s subsystem=%-8s state=%s bound=%u\n",
                d->name ? d->name : "(null)",
                d->subsystem ? d->subsystem : "unknown",
                d->state == DRIVER_STATE_BOUND ? "bound" : "registered",
                d->bound_count);
    }
}

u32 driver_registered_count(void) { return g_driver_count; }
u32 driver_bound_count(void) { return g_bound_count; }
u32 driver_unbound_pci_count(void) { return g_unbound_pci_count; }
