/* YamKernel — Enable NX, SMEP, SMAP, UMIP, WP */
#include "security.h"
#include "msr.h"
#include "../lib/kprintf.h"
#include "cpuid.h"

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

    /* ---- FPU & SIMD Setup ---- */
    const cpuid_info_t* info = cpuid_get_info();
    
    /* Clear EM (bit 2) and set MP (bit 1) in CR0 */
    cr0 &= ~(1ULL << 2);
    cr0 |= (1ULL << 1);
    write_cr0(cr0);

    /* Enable FXSAVE/FXRSTOR and unmasked SIMD exceptions */
    if (info->has_fxsr) { cr4 |= CR4_OSFXSR; }
    if (info->has_sse)  { cr4 |= CR4_OSXMMEXCPT; }

    /* Enable XSAVE/XRSTOR if supported */
    if (info->has_xsave) {
        cr4 |= CR4_OSXSAVE;
    }
    
    write_cr4(cr4);

    if (info->has_xsave) {
        /* Enable AVX in XCR0 if XSAVE is available and AVX supported */
        if (info->has_avx) {
            /* XCR0 bits: 0=x87, 1=SSE, 2=AVX */
            u32 xcr0_lo = 7; /* Enable x87 + SSE + AVX */
            u32 xcr0_hi = 0;
            __asm__ volatile("xsetbv" :: "a"(xcr0_lo), "d"(xcr0_hi), "c"(0));
            kprintf_color(0xFF00FF88, "[SEC] AVX & XSAVE enabled\n");
        } else {
            /* XCR0 bits: 0=x87, 1=SSE */
            u32 xcr0_lo = 3; /* Enable x87 + SSE */
            u32 xcr0_hi = 0;
            __asm__ volatile("xsetbv" :: "a"(xcr0_lo), "d"(xcr0_hi), "c"(0));
            kprintf_color(0xFF00FF88, "[SEC] XSAVE enabled (No AVX)\n");
        }
    } else if (info->has_fxsr) {
        kprintf_color(0xFF00FF88, "[SEC] FXSAVE enabled\n");
    }
}
