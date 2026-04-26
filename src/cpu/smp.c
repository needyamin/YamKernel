/* YamKernel — SMP enumeration (CPU count from MADT). AP boot: TODO. */
#include "smp.h"
#include "acpi.h"
#include "../lib/kprintf.h"

void smp_init(void) {
    const acpi_info_t *a = acpi_get();
    u32 enabled = 0;
    for (u32 i = 0; i < a->cpu_count; i++)
        if (a->cpus[i].flags & 1) enabled++;
    kprintf_color(0xFF00FF88,
        "[SMP] %u logical CPUs (%u enabled). BSP running. APs: pending.\n",
        a->cpu_count, enabled);
}

u32 smp_cpu_count(void) { return acpi_get()->cpu_count; }
