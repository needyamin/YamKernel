/* ============================================================================
 * YamKernel — PCI Bus Enumeration Driver Implementation
 * ============================================================================ */

#include "pci.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"

/* Port 0xCF8: CONFIG_ADDRESS
   Port 0xCFC: CONFIG_DATA */

/* Because our outw/inw are 16 bit, let's just do 32 bit port I/O directly here using asm */
u32 pci_read_32(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 address = (u32)((1U << 31) | ((u32)bus << 16) | ((u32)slot << 11) | ((u32)func << 8) | (offset & 0xFC));
    __asm__ volatile ("outl %0, %1" : : "a"(address), "Nd"(0xCF8));
    u32 ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(0xCFC));
    return ret;
}

u16 pci_read_16(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 dword = pci_read_32(bus, slot, func, offset);
    return (u16)((dword >> ((offset & 2) * 8)) & 0xFFFF);
}

u8 pci_read_8(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 dword = pci_read_32(bus, slot, func, offset);
    return (u8)((dword >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write_32(u8 bus, u8 slot, u8 func, u8 offset, u32 value) {
    u32 address = (u32)((1U << 31) | ((u32)bus << 16) | ((u32)slot << 11) | ((u32)func << 8) | (offset & 0xFC));
    __asm__ volatile ("outl %0, %1" : : "a"(address), "Nd"(0xCF8));
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(0xCFC));
}

void pci_write_16(u8 bus, u8 slot, u8 func, u8 offset, u16 value) {
    /* Read modify write to avoid clobbering other 16 bits */
    u32 dword = pci_read_32(bus, slot, func, offset);
    u32 shift = (offset & 2) * 8;
    u32 mask = ~(0xFFFF << shift);
    dword = (dword & mask) | ((u32)value << shift);
    pci_write_32(bus, slot, func, offset, dword);
}

u16 pci_command(pci_device_t *dev) {
    if (!dev) return 0;
    return pci_read_16((u8)dev->bus, (u8)dev->slot, (u8)dev->func, 0x04);
}

u16 pci_status(pci_device_t *dev) {
    if (!dev) return 0;
    return pci_read_16((u8)dev->bus, (u8)dev->slot, (u8)dev->func, 0x06);
}

void pci_set_command_bits(pci_device_t *dev, u16 bits) {
    if (!dev) return;
    u16 cmd = pci_command(dev);
    cmd |= bits;
    pci_write_16((u8)dev->bus, (u8)dev->slot, (u8)dev->func, 0x04, cmd);
    dev->command = cmd;
}

void pci_clear_command_bits(pci_device_t *dev, u16 bits) {
    if (!dev) return;
    u16 cmd = pci_command(dev);
    cmd &= (u16)~bits;
    pci_write_16((u8)dev->bus, (u8)dev->slot, (u8)dev->func, 0x04, cmd);
    dev->command = cmd;
}

void pci_enable_io(pci_device_t *dev) {
    pci_set_command_bits(dev, 1u << 0);
}

void pci_enable_mmio(pci_device_t *dev) {
    pci_set_command_bits(dev, 1u << 1);
}

void pci_enable_bus_master(pci_device_t *dev) {
    pci_set_command_bits(dev, 1u << 2);
}

void pci_disable_legacy_interrupt(pci_device_t *dev) {
    pci_set_command_bits(dev, 1u << 10);
}

void pci_enable_legacy_interrupt(pci_device_t *dev) {
    pci_clear_command_bits(dev, 1u << 10);
}

u8 pci_find_capability(pci_device_t *dev, u8 cap_id) {
    if (!dev || !dev->has_capabilities) return 0;

    u8 ptr = dev->cap_ptr & 0xFC;
    for (u32 guard = 0; ptr >= 0x40 && guard < 48; guard++) {
        u8 id = pci_read_8((u8)dev->bus, (u8)dev->slot, (u8)dev->func, ptr);
        u8 next = pci_read_8((u8)dev->bus, (u8)dev->slot, (u8)dev->func, ptr + 1);
        if (id == cap_id) return ptr;
        ptr = next & 0xFC;
    }
    return 0;
}

bool pci_has_msi(pci_device_t *dev) {
    return dev && dev->has_msi;
}

bool pci_has_msix(pci_device_t *dev) {
    return dev && dev->has_msix;
}

static u64 pci_bar_size_mask(pci_bar_type_t type) {
    return type == PCI_BAR_IO ? ~0x3ULL : ~0xFULL;
}

bool pci_read_bar(pci_device_t *dev, u8 bar_index, pci_bar_t *out) {
    if (!dev || !out || bar_index >= 6) return false;
    memset(out, 0, sizeof(*out));

    u8 off = (u8)(0x10 + bar_index * 4);
    u32 raw = pci_read_32((u8)dev->bus, (u8)dev->slot, (u8)dev->func, off);
    if (raw == 0 || raw == 0xFFFFFFFF) return false;

    out->index = bar_index;
    if (raw & 1) {
        out->type = PCI_BAR_IO;
        out->base = raw & ~0x3ULL;
    } else {
        u32 mem_type = (raw >> 1) & 0x3;
        out->prefetchable = (raw & 0x8) != 0;
        if (mem_type == 0x2 && bar_index < 5) {
            u32 raw_hi = pci_read_32((u8)dev->bus, (u8)dev->slot, (u8)dev->func, off + 4);
            out->type = PCI_BAR_MMIO64;
            out->base = ((u64)raw_hi << 32) | (raw & ~0xFULL);
        } else {
            out->type = PCI_BAR_MMIO32;
            out->base = raw & ~0xFULL;
        }
    }

    u16 command = pci_command(dev);
    pci_write_16((u8)dev->bus, (u8)dev->slot, (u8)dev->func, 0x04,
                 command & (u16)~0x0003);

    if (out->type == PCI_BAR_MMIO64 && bar_index < 5) {
        u32 raw_hi = pci_read_32((u8)dev->bus, (u8)dev->slot, (u8)dev->func, off + 4);
        pci_write_32((u8)dev->bus, (u8)dev->slot, (u8)dev->func, off, 0xFFFFFFFF);
        pci_write_32((u8)dev->bus, (u8)dev->slot, (u8)dev->func, off + 4, 0xFFFFFFFF);
        u32 size_raw = pci_read_32((u8)dev->bus, (u8)dev->slot, (u8)dev->func, off);
        u32 size_hi = pci_read_32((u8)dev->bus, (u8)dev->slot, (u8)dev->func, off + 4);
        u64 mask = ((u64)size_hi << 32) | (size_raw & pci_bar_size_mask(out->type));
        out->size = mask ? (~mask + 1) : 0;
        pci_write_32((u8)dev->bus, (u8)dev->slot, (u8)dev->func, off + 4, raw_hi);
    } else {
        pci_write_32((u8)dev->bus, (u8)dev->slot, (u8)dev->func, off, 0xFFFFFFFF);
        u32 size_raw = pci_read_32((u8)dev->bus, (u8)dev->slot, (u8)dev->func, off);
        u32 mask = size_raw & (u32)pci_bar_size_mask(out->type);
        out->size = mask ? (~(u64)mask + 1) & 0xFFFFFFFFULL : 0;
    }
    pci_write_32((u8)dev->bus, (u8)dev->slot, (u8)dev->func, off, raw);

    pci_write_16((u8)dev->bus, (u8)dev->slot, (u8)dev->func, 0x04, command);
    dev->command = command;

    return out->base != 0;
}

const char* pci_class_name(u8 class_id) {
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
#define MAX_PCI_DEVICES 128
static pci_device_t pci_devices[MAX_PCI_DEVICES];
static u32 pci_dev_count = 0;
static bool pci_bus_scanned[256];

static void pci_scan_bus(u8 bus);

static bool pci_is_bridge(const pci_device_t *dev) {
    return dev && dev->class_id == 0x06 && dev->subclass_id == 0x04;
}

static void pci_fill_capabilities(pci_device_t *dev) {
    dev->status = pci_status(dev);
    dev->has_capabilities = (dev->status & (1u << 4)) != 0;
    if (!dev->has_capabilities) return;

    u8 header = dev->header_type & 0x7F;
    if (header != 0x00 && header != 0x01) return;

    dev->cap_ptr = pci_read_8((u8)dev->bus, (u8)dev->slot, (u8)dev->func, 0x34) & 0xFC;
    dev->msi_cap = pci_find_capability(dev, 0x05);
    dev->msix_cap = pci_find_capability(dev, 0x11);
    dev->has_msi = dev->msi_cap != 0;
    dev->has_msix = dev->msix_cap != 0;
}

static void check_function(u8 bus, u8 slot, u8 func) {
    u16 vendor = pci_read_16(bus, slot, func, 0);
    if (vendor == 0xFFFF) return; /* Device doesn't exist */

    if (pci_dev_count >= MAX_PCI_DEVICES) return;

    pci_device_t *dev = &pci_devices[pci_dev_count++];
    memset(dev, 0, sizeof(*dev));
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = vendor;
    dev->device_id = pci_read_16(bus, slot, func, 2);
    dev->revision_id = pci_read_8(bus, slot, func, 8);
    dev->class_id = pci_read_8(bus, slot, func, 11);
    dev->subclass_id = pci_read_8(bus, slot, func, 10);
    dev->prog_if = pci_read_8(bus, slot, func, 9);
    dev->header_type = pci_read_8(bus, slot, func, 14);
    dev->command = pci_command(dev);
    dev->status = pci_status(dev);
    dev->interrupt_line = pci_read_8(bus, slot, func, 0x3C);
    dev->interrupt_pin = pci_read_8(bus, slot, func, 0x3D);
    dev->secondary_bus = pci_is_bridge(dev) ? pci_read_8(bus, slot, func, 0x19) : 0;
    pci_fill_capabilities(dev);

    if (pci_is_bridge(dev) && dev->secondary_bus != 0 && dev->secondary_bus != bus) {
        pci_scan_bus(dev->secondary_bus);
    }
}

static void pci_scan_slot(u8 bus, u8 slot) {
    u16 vendor = pci_read_16(bus, slot, 0, 0);
    if (vendor == 0xFFFF) return;

    check_function(bus, slot, 0);

    u8 header_type = pci_read_8(bus, slot, 0, 14);
    if ((header_type & 0x80) != 0) {
        for (u8 func = 1; func < 8; func++) {
            check_function(bus, slot, func);
        }
    }
}

static void pci_scan_bus(u8 bus) {
    if (pci_bus_scanned[bus]) return;
    pci_bus_scanned[bus] = true;

    for (u8 slot = 0; slot < 32; slot++) {
        pci_scan_slot(bus, slot);
    }
}

void pci_init(void) {
    pci_dev_count = 0;
    memset(pci_bus_scanned, 0, sizeof(pci_bus_scanned));

    u8 header_type = pci_read_8(0, 0, 0, 14);
    if ((header_type & 0x80) == 0) {
        pci_scan_bus(0);
    } else {
        for (u8 func = 0; func < 8; func++) {
            if (pci_read_16(0, 0, func, 0) != 0xFFFF) {
                pci_scan_bus(func);
            }
        }
    }

    u32 bus_count = 0;
    for (u32 i = 0; i < 256; i++) {
        if (pci_bus_scanned[i]) bus_count++;
    }
    kprintf_color(0xFF00FF88, "[PCI] Bridge-aware scan: %u bus(es), %u device(s).\n",
                  bus_count, pci_dev_count);
}

pci_device_t* pci_get_device(u16 vendor_id, u16 device_id) {
    for (u32 i = 0; i < pci_dev_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id && pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

u32 pci_device_count(void) {
    return pci_dev_count;
}

pci_device_t *pci_device_at(u32 index) {
    if (index >= pci_dev_count) return NULL;
    return &pci_devices[index];
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
        if (d->has_msi || d->has_msix) {
            kprintf_color(0xFF888888, " caps:%s%s",
                          d->has_msi ? " msi" : "",
                          d->has_msix ? " msix" : "");
        }
        if (pci_is_bridge(d)) {
            kprintf_color(0xFF888888, " secbus=%u", d->secondary_bus);
        }
        kprintf("\n");
    }
    if (pci_dev_count == 0) {
        kprintf_color(0xFF00FF88, "  ║   ");
        kprintf_color(0xFF666666, "(no devices found)\n");
    }
}
