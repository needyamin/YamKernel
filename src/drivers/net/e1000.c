/* ============================================================================
 * YamKernel — Intel e1000 Gigabit Ethernet Driver
 * ============================================================================ */
#include "e1000.h"
#include "../../lib/kprintf.h"
#include "../pci.h"
#include "../../mem/pmm.h"
#include "../../mem/vmm.h"

static void *e1000_mmio_base;
static u8 e1000_mac[6];

/*
static void e1000_write(u16 offset, u32 val) {
    *(volatile u32 *)((u64)e1000_mmio_base + offset) = val;
}
*/

static u32 e1000_read(u16 offset) {
    return *(volatile u32 *)((u64)e1000_mmio_base + offset);
}

void e1000_init(void) {
    /* Intel PRO/1000 MT Desktop (82540EM) is commonly 0x100E inside QEMU/VirtualBox */
    pci_device_t *nic = pci_get_device(0x8086, 0x100E);
    if (!nic) {
        kprintf_color(0xFF00DDFF, "[e1000] Intel Gigabit NIC not found.\n");
        return;
    }

    /* Enable Bus Mastering, Memory Write and Invalidate, and MMIO accesses */
    u16 command = pci_read_16(nic->bus, nic->slot, nic->func, 0x04);
    pci_write_16(nic->bus, nic->slot, nic->func, 0x04, command | 0x0006);

    /* Read BAR0 for MMIO Base */
    u32 bar0 = pci_read_32(nic->bus, nic->slot, nic->func, 0x10);
    u32 phys_addr = bar0 & 0xFFFFFFF0;
    
    /* Map to Virtual Memory (Mock identity mapping for now) */
    e1000_mmio_base = (void *)(u64)phys_addr;

    /* Read MAC Address from EEPROM/Registers */
    u32 mac_low = e1000_read(E1000_REG_RAL);
    u32 mac_high = e1000_read(E1000_REG_RAH);
    
    e1000_mac[0] = mac_low & 0xFF;
    e1000_mac[1] = (mac_low >> 8) & 0xFF;
    e1000_mac[2] = (mac_low >> 16) & 0xFF;
    e1000_mac[3] = (mac_low >> 24) & 0xFF;
    e1000_mac[4] = mac_high & 0xFF;
    e1000_mac[5] = (mac_high >> 8) & 0xFF;

    kprintf_color(0xFF00FF88, "[e1000] Intel Gigabit NIC Initialized.\n");
    kprintf_color(0xFF00FF88, "[e1000] Hardware MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n", 
                  e1000_mac[0], e1000_mac[1], e1000_mac[2], e1000_mac[3], e1000_mac[4], e1000_mac[5]);
}
