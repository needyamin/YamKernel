/* YamKernel — Per-CPU storage (GS-relative) */
#include "percpu.h"
#include "msr.h"
#include "../mem/heap.h"
#include "../lib/string.h"

#define MAX_CPUS 64
static percpu_t pool[MAX_CPUS];

void percpu_init(u32 cpu_id, u32 apic_id) {
    percpu_t *p = &pool[cpu_id];
    memset(p, 0, sizeof(*p));
    p->cpu_id = cpu_id;
    p->apic_id = apic_id;
    wrmsr(MSR_GS_BASE, (u64)p);
    wrmsr(MSR_KERNEL_GS_BASE, 0);   /* user GS = 0; SWAPGS flips kernel↔user */
}

void percpu_set_kernel_rsp(u64 rsp) { ((percpu_t *)rdmsr(MSR_GS_BASE))->kernel_rsp = rsp; }

percpu_t *this_cpu(void) {
    return (percpu_t *)rdmsr(MSR_GS_BASE);
}
