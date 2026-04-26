/* YamKernel — Enable NX, SMEP, SMAP, UMIP, WP */
#include "security.h"
#include "msr.h"
#include "../lib/kprintf.h"

static bool cpu_has(u32 leaf, u32 reg, u32 bit) {
    u32 a, b, c, d;
    cpuid_raw(leaf, 0, &a, &b, &c, &d);
    u32 r = (reg == 0) ? a : (reg == 1) ? b : (reg == 2) ? c : d;
    return (r >> bit) & 1;
}

void security_init(void) {
    u64 cr0 = read_cr0();
    cr0 |= (1ULL << 16);          /* WP: enforce R/W on kernel pages */
    write_cr0(cr0);

    u64 efer = rdmsr(MSR_EFER);
    if (cpu_has(0x80000001, 3, 20)) {
        efer |= EFER_NXE;
        wrmsr(MSR_EFER, efer);
        kprintf_color(0xFF00FF88, "[SEC] NX     enabled\n");
    }

    u64 cr4 = read_cr4();
    if (cpu_has(7, 1, 7))  { cr4 |= CR4_SMEP; kprintf_color(0xFF00FF88, "[SEC] SMEP   enabled\n"); }
    if (cpu_has(7, 1, 20)) { cr4 |= CR4_SMAP; kprintf_color(0xFF00FF88, "[SEC] SMAP   enabled\n"); }
    if (cpu_has(7, 2, 2))  { cr4 |= CR4_UMIP; kprintf_color(0xFF00FF88, "[SEC] UMIP   enabled\n"); }
    if (cpu_has(7, 1, 0))  { cr4 |= CR4_FSGSBASE; kprintf_color(0xFF00FF88, "[SEC] FSGSBASE enabled\n"); }
    write_cr4(cr4);
}
