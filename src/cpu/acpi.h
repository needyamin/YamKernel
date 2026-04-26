/* YamKernel — ACPI table parser (RSDP/XSDT/MADT) */
#ifndef _CPU_ACPI_H
#define _CPU_ACPI_H

#include <nexus/types.h>

#define ACPI_MAX_CPUS    64
#define ACPI_MAX_IOAPICS 8

typedef struct {
    u8  apic_id;
    u8  acpi_id;
    u32 flags;          /* bit0 = enabled */
} acpi_cpu_t;

typedef struct {
    u8  id;
    u32 mmio_addr;
    u32 gsi_base;
} acpi_ioapic_t;

typedef struct {
    u64 lapic_addr;
    u32 cpu_count;
    u32 ioapic_count;
    acpi_cpu_t    cpus[ACPI_MAX_CPUS];
    acpi_ioapic_t ioapics[ACPI_MAX_IOAPICS];
} acpi_info_t;

/* rsdp_addr = pointer from limine RSDP request */
void               acpi_init(void *rsdp_addr, u64 hhdm_offset);
const acpi_info_t *acpi_get(void);

#endif
