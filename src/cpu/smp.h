/* YamKernel — SMP topology and AP boot via Limine */
#ifndef _CPU_SMP_H
#define _CPU_SMP_H

#include <nexus/types.h>

struct limine_smp_response;

void smp_init(struct limine_smp_response *smp_resp);
u32  smp_cpu_count(void);
u32  smp_sched_cpu_count(void);
bool smp_scheduler_active(void);

#endif
