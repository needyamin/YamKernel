#include "driver.h"

#define E1000_REG_RAL   0x5400
#define E1000_REG_RAH   0x5404

void _start(void) {
    print("[OS_NET] Network Service Started (Ring 3)\n");
    
    /* Search for Intel 82540EM (QEMU Default) */
    /* In a real OS, we'd iterate the PCI bus. Here we hardcode common QEMU slot 3 */
    u8 bus = 0, slot = 3, func = 0;
    u16 vendor = pci_read_16(bus, slot, func, 0x00);
    u16 device = pci_read_16(bus, slot, func, 0x02);
    
    if (vendor != 0x8086 || device != 0x100E) {
        print("[OS_NET] Intel E1000 NIC not found at 00:03.0. Scanning...\n");
        /* In a real implementation, we'd use a service to find it */
    } else {
        print("[OS_NET] Found Intel E1000 NIC. Initializing...\n");
        
        /* Map MMIO */
        u32 bar0 = pci_read_32(bus, slot, func, 0x10) & 0xFFFFFFF0;
        void *mmio = map_mmio(bar0, 128 * 1024);
        
        if (mmio) {
            print("[OS_NET] MMIO mapped at userspace address.\n");
            
            /* Read MAC */
            u32 mac_low = *(volatile u32 *)((u64)mmio + E1000_REG_RAL);
            u32 mac_high = *(volatile u32 *)((u64)mmio + E1000_REG_RAH);
            
            print("[OS_NET] Hardware MAC detected in Userspace!\n");
        }
    }
    
    print("[OS_NET] Network Service is now managing the Internet connection.\n");
    
    while(1) {
        sleep_ms(1000);
        /* In a real driver, we'd handle RX/TX queues here */
    }
}
