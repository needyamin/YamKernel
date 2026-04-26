/* YamKernel — SMP topology (boot of APs deferred to next phase) */
#ifndef _CPU_SMP_H
#define _CPU_SMP_H

#include <nexus/types.h>

void smp_init(void);    /* enumerate CPUs from ACPI/MADT */
u32  smp_cpu_count(void);

#endif
