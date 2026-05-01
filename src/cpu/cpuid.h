/* ============================================================================
 * YamKernel — CPUID Driver
 * Detects CPU vendor, brand string, and feature flags
 * ============================================================================ */

#ifndef _CPU_CPUID_H
#define _CPU_CPUID_H

#include <nexus/types.h>

typedef struct {
    char vendor[16];         /* e.g. "GenuineIntel", "AuthenticAMD" */
    char brand[52];          /* e.g. "Intel(R) Core(TM) i7-12700K" */
    u32  family;
    u32  model;
    u32  stepping;
    /* Feature flags (EDX from leaf 1) */
    bool has_fpu;
    bool has_sse;
    bool has_sse2;
    bool has_avx;
    bool has_apic;
    bool has_msr;
    bool has_pae;
    bool has_tsc;
    bool has_cx8;
    bool has_pse;
    bool has_fxsr;
    /* ECX features from leaf 1 */
    bool has_sse3;
    bool has_sse41;
    bool has_sse42;
    bool has_aes;
    bool has_x2apic;
    bool has_tsc_deadline;
    bool has_xsave;
    bool has_invariant_tsc;
    /* Derived/Extended state */
    u32  xsave_size;
} cpuid_info_t;

/* Initialize CPUID detection (call once at boot) */
void cpuid_init(void);

/* Get the cached CPUID information */
const cpuid_info_t* cpuid_get_info(void);

#endif /* _CPU_CPUID_H */
