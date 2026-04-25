/* ============================================================================
 * YamKernel — CPUID Driver
 * Uses the x86 CPUID instruction to detect CPU vendor, brand, and features.
 * ============================================================================ */

#include "cpuid.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

static cpuid_info_t g_cpuid;

/* Execute CPUID instruction */
static void cpuid(u32 leaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    __asm__ volatile ("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0));
}

void cpuid_init(void) {
    memset(&g_cpuid, 0, sizeof(g_cpuid));

    u32 eax, ebx, ecx, edx;

    /* ---- Leaf 0: Vendor String ---- */
    cpuid(0, &eax, &ebx, &ecx, &edx);
    u32 max_leaf = eax;
    /* Vendor is stored as EBX-EDX-ECX (yes, that order) */
    *(u32*)&g_cpuid.vendor[0] = ebx;
    *(u32*)&g_cpuid.vendor[4] = edx;
    *(u32*)&g_cpuid.vendor[8] = ecx;
    g_cpuid.vendor[12] = 0;

    /* ---- Leaf 1: Version & Feature Flags ---- */
    if (max_leaf >= 1) {
        cpuid(1, &eax, &ebx, &ecx, &edx);

        g_cpuid.stepping = eax & 0x0F;
        g_cpuid.model    = (eax >> 4) & 0x0F;
        g_cpuid.family   = (eax >> 8) & 0x0F;

        /* Extended model/family for family >= 6 or 15 */
        if (g_cpuid.family == 6 || g_cpuid.family == 15) {
            g_cpuid.model  += ((eax >> 16) & 0x0F) << 4;
        }
        if (g_cpuid.family == 15) {
            g_cpuid.family += (eax >> 20) & 0xFF;
        }

        /* EDX feature flags */
        g_cpuid.has_fpu  = (edx >> 0) & 1;
        g_cpuid.has_tsc  = (edx >> 4) & 1;
        g_cpuid.has_msr  = (edx >> 5) & 1;
        g_cpuid.has_pae  = (edx >> 6) & 1;
        g_cpuid.has_cx8  = (edx >> 8) & 1;
        g_cpuid.has_apic = (edx >> 9) & 1;
        g_cpuid.has_pse  = (edx >> 3) & 1;
        g_cpuid.has_sse  = (edx >> 25) & 1;
        g_cpuid.has_sse2 = (edx >> 26) & 1;

        /* ECX feature flags */
        g_cpuid.has_sse3   = (ecx >> 0) & 1;
        g_cpuid.has_sse41  = (ecx >> 19) & 1;
        g_cpuid.has_sse42  = (ecx >> 20) & 1;
        g_cpuid.has_aes    = (ecx >> 25) & 1;
        g_cpuid.has_avx    = (ecx >> 28) & 1;
        g_cpuid.has_x2apic = (ecx >> 21) & 1;
    }

    /* ---- Extended Leaf 0x80000002-4: Brand String ---- */
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    u32 max_ext = eax;

    if (max_ext >= 0x80000004) {
        u32 *brand = (u32 *)g_cpuid.brand;
        cpuid(0x80000002, &brand[0], &brand[1], &brand[2], &brand[3]);
        cpuid(0x80000003, &brand[4], &brand[5], &brand[6], &brand[7]);
        cpuid(0x80000004, &brand[8], &brand[9], &brand[10], &brand[11]);
        g_cpuid.brand[48] = 0;
    } else {
        strcpy(g_cpuid.brand, "(unknown)");
    }

    kprintf_color(0xFF00FF88, "[CPU] %s — %s\n", g_cpuid.vendor, g_cpuid.brand);
    kprintf_color(0xFF00FF88, "[CPU] Family %u, Model %u, Stepping %u\n",
                  g_cpuid.family, g_cpuid.model, g_cpuid.stepping);
}

const cpuid_info_t* cpuid_get_info(void) {
    return &g_cpuid;
}
