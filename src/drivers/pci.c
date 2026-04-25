/* ============================================================================
 * YamKernel — PCI Bus Enumeration Driver Implementation
 * ============================================================================ */

#include "pci.h"
#include "../lib/kprintf.h"

/* Port 0xCF8: CONFIG_ADDRESS
   Port 0xCFC: CONFIG_DATA */

/* Because our outw/inw are 16 bit, let's just do 32 bit port I/O directly here using asm */
static u32 pci_read_32(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 address = (u32)((1U << 31) | ((u32)bus << 16) | ((u32)slot << 11) | ((u32)func << 8) | (offset & 0xFC));
    __asm__ volatile ("outl %0, %1" : : "a"(address), "Nd"(0xCF8));
    u32 ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(0xCFC));
    return ret;
}

static u16 pci_read_16(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 dword = pci_read_32(bus, slot, func, offset);
    return (u16)((dword >> ((offset & 2) * 8)) & 0xFFFF);
}

static u8 pci_read_8(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 dword = pci_read_32(bus, slot, func, offset);
    return (u8)((dword >> ((offset & 3) * 8)) & 0xFF);
}

/* Helper to get class name */
static const char* pci_class_name(u8 class_id) {
    switch (class_id) {
        case 0x00: return "Unclassified";
        case 0x01: return "Mass Storage Controller";
        case 0x02: return "Network Controller";
        case 0x03: return "Display Controller";
        case 0x04: return "Multimedia Controller";
        case 0x05: return "Memory Controller";
        case 0x06: return "Bridge Device";
        case 0x07: return "Simple Communication";
        case 0x08: return "Base System Peripheral";
        case 0x09: return "Input Device Controller";
        case 0x0A: return "Docking Station";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial Bus Controller";
        case 0x0D: return "Wireless Controller";
        default:   return "Unknown";
    }
}

/* Storage for devices found */
#define MAX_PCI_DEVICES 64
static pci_device_t pci_devices[MAX_PCI_DEVICES];
static u32 pci_dev_count = 0;

static void check_function(u8 bus, u8 slot, u8 func) {
    u16 vendor = pci_read_16(bus, slot, func, 0);
    if (vendor == 0xFFFF) return; /* Device doesn't exist */

    if (pci_dev_count >= MAX_PCI_DEVICES) return;

    pci_device_t *dev = &pci_devices[pci_dev_count++];
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = vendor;
    dev->device_id = pci_read_16(bus, slot, func, 2);
    dev->class_id = pci_read_8(bus, slot, func, 11);
    dev->subclass_id = pci_read_8(bus, slot, func, 10);
    dev->prog_if = pci_read_8(bus, slot, func, 9);
    dev->header_type = pci_read_8(bus, slot, func, 14);
}

void pci_init(void) {
    pci_dev_count = 0;
    
    /* Brute-force scan (Buses 0-255) */
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            u16 vendor = pci_read_16((u8)bus, (u8)slot, 0, 0);
            if (vendor == 0xFFFF) continue;

            check_function((u8)bus, (u8)slot, 0);

            /* Check if multi-function device */
            u8 header_type = pci_read_8((u8)bus, (u8)slot, 0, 14);
            if ((header_type & 0x80) != 0) {
                for (int func = 1; func < 8; func++) {
                    check_function((u8)bus, (u8)slot, (u8)func);
                }
            }
        }
    }
    kprintf_color(0xFF00FF88, "[PCI] Scanned bus. Found %u devices.\n", pci_dev_count);
}

void pci_dump(void) {
    for (u32 i = 0; i < pci_dev_count; i++) {
        pci_device_t *d = &pci_devices[i];
        kprintf_color(0xFF00FF88, "  ║   ");
        kprintf_color(0xFF00FF88, "●");
        kprintf_color(0xFF00DDFF, " %02x:%02x.%x ", d->bus, d->slot, d->func);
        kprintf_color(0xFF666666, "[");
        kprintf_color(0xFFFFDD00, "%04x:%04x", d->vendor_id, d->device_id);
        kprintf_color(0xFF666666, "] ");
        kprintf_color(0xFFCCCCCC, "%s", pci_class_name(d->class_id));
        kprintf("\n");
    }
    if (pci_dev_count == 0) {
        kprintf_color(0xFF00FF88, "  ║   ");
        kprintf_color(0xFF666666, "(no devices found)\n");
    }
}
