#ifndef _OS_DRIVERS_DRIVER_H
#define _OS_DRIVERS_DRIVER_H

#include "../apps/yam.h"

/* Port I/O Wrappers */
static inline u8 inb(u16 port) { return (u8)syscall2(SYS_IOPORT_READ, port, 1); }
static inline void outb(u16 port, u8 val) { syscall3(SYS_IOPORT_WRITE, port, 1, val); }

static inline u16 inw(u16 port) { return (u16)syscall2(SYS_IOPORT_READ, port, 2); }
static inline void outw(u16 port, u16 val) { syscall3(SYS_IOPORT_WRITE, port, 2, val); }

static inline u32 inl(u16 port) { return (u32)syscall2(SYS_IOPORT_READ, port, 4); }
static inline void outl(u16 port, u32 val) { syscall3(SYS_IOPORT_WRITE, port, 4, val); }

/* PCI Config Access */
static inline u32 pci_read_32(u8 b, u8 s, u8 f, u8 o) { return (u32)syscall5(SYS_PCI_CONFIG_READ, b, s, f, o, 4); }
static inline u16 pci_read_16(u8 b, u8 s, u8 f, u8 o) { return (u16)syscall5(SYS_PCI_CONFIG_READ, b, s, f, o, 2); }

/* MMIO Mapping */
static inline void* map_mmio(u64 phys, u64 size) {
    void *virt = (void *)0xB0000000; /* Example fixed base for drivers */
    static u64 offset = 0;
    void *target = (void *)((u64)virt + offset);
    if (syscall3(SYS_MAP_MMIO, phys, (u64)target, size) == 0) {
        offset += (size + 0xFFF) & ~0xFFF;
        return target;
    }
    return NULL;
}

#endif
