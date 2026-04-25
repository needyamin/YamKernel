/* ============================================================================
 * YamKernel — Intel e1000 Gigabit Ethernet Driver
 * ============================================================================ */
#include "e1000.h"
#include "../../lib/kprintf.h"
#include "../bus/pci.h"
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
    kprintf_color(0xFF00DDFF, "[e1000] Scanning PCI bus for Intel 82540EM...\n");
    /* Intel PRO/1000 MT Desktop (82540EM) is commonly 0x100E inside QEMU/VirtualBox */
    pci_device_t *nic = pci_get_device(0x8086, 0x100E);
    if (!nic) {
        kprintf_color(0xFF00DDFF, "[e1000] Intel Gigabit NIC not found.\n");
        return;
    }
    kprintf_color(0xFF00FF88, "[e1000] NIC Found at %02x:%02x.%u\n", nic->bus, nic->slot, nic->func);

    /* Enable Bus Mastering, Memory Write and Invalidate, and MMIO accesses */
    u16 command = pci_read_16(nic->bus, nic->slot, nic->func, 0x04);
    pci_write_16(nic->bus, nic->slot, nic->func, 0x04, command | 0x0006);
    kprintf_color(0xFF00DDFF, "[e1000] Bus Mastering enabled. Command: 0x%04x\n", command | 0x0006);

    /* Read BAR0 for MMIO Base */
    u32 bar0 = pci_read_32(nic->bus, nic->slot, nic->func, 0x10);
    u32 phys_addr = bar0 & 0xFFFFFFF0;
    kprintf_color(0xFF00DDFF, "[e1000] BAR0 Read: 0x%08x -> Physical MMIO: 0x%08x\n", bar0, phys_addr);
    
    if (phys_addr == 0) {
        kprintf_color(0xFFFF0000, "[e1000] ERROR: Invalid Physical Address 0x0!\n");
        return;
    }

    /* Limine's HHDM does NOT necessarily cover PCI MMIO BARs (it maps only up
       to the highest RAM region). Manually map 128 KB at the HHDM virtual.
       vmm_map_page now safely skips ranges already covered by huge pages. */
    e1000_mmio_base = vmm_phys_to_virt((u64)phys_addr);
    u64 *pml4 = vmm_get_kernel_pml4();
    for (u64 i = 0; i < 128 * 1024; i += 4096) {
        vmm_map_page(pml4, (u64)e1000_mmio_base + i, phys_addr + i,
                     VMM_FLAG_WRITE | VMM_FLAG_NOCACHE);
    }
    /* Flush TLB so the new mappings take effect */
    __asm__ volatile ("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
    kprintf_color(0xFF00DDFF, "[e1000] MMIO mapped at 0x%lx (128KB)\n", (u64)e1000_mmio_base);

    /* Read MAC Address from EEPROM/Registers */
    kprintf_color(0xFF00DDFF, "[e1000] Reading MAC Address (EEPROM offset 0x5400)...\n");
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
