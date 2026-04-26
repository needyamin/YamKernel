/* YamKernel — MSR / CR helpers */
#ifndef _CPU_MSR_H
#define _CPU_MSR_H

#include <nexus/types.h>

#define MSR_APIC_BASE       0x1B
#define MSR_EFER            0xC0000080
#define MSR_STAR            0xC0000081
#define MSR_LSTAR           0xC0000082
#define MSR_CSTAR           0xC0000083
#define MSR_SFMASK          0xC0000084
#define MSR_FS_BASE         0xC0000100
#define MSR_GS_BASE         0xC0000101
#define MSR_KERNEL_GS_BASE  0xC0000102

#define EFER_SCE  (1ULL << 0)
#define EFER_LME  (1ULL << 8)
#define EFER_LMA  (1ULL << 10)
#define EFER_NXE  (1ULL << 11)

#define CR4_PSE   (1ULL << 4)
#define CR4_PAE   (1ULL << 5)
#define CR4_PGE   (1ULL << 7)
#define CR4_OSFXSR     (1ULL << 9)
#define CR4_OSXMMEXCPT (1ULL << 10)
#define CR4_UMIP  (1ULL << 11)
#define CR4_FSGSBASE   (1ULL << 16)
#define CR4_OSXSAVE    (1ULL << 18)
#define CR4_SMEP  (1ULL << 20)
#define CR4_SMAP  (1ULL << 21)

ALWAYS_INLINE u64 rdmsr(u32 msr) {
    u32 lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

ALWAYS_INLINE void wrmsr(u32 msr, u64 val) {
    u32 lo = (u32)val, hi = (u32)(val >> 32);
    __asm__ volatile ("wrmsr" :: "a"(lo), "d"(hi), "c"(msr));
}

ALWAYS_INLINE u64 read_cr0(void) { u64 v; __asm__ volatile("mov %%cr0,%0":"=r"(v)); return v; }
ALWAYS_INLINE u64 read_cr3(void) { u64 v; __asm__ volatile("mov %%cr3,%0":"=r"(v)); return v; }
ALWAYS_INLINE u64 read_cr4(void) { u64 v; __asm__ volatile("mov %%cr4,%0":"=r"(v)); return v; }
ALWAYS_INLINE void write_cr0(u64 v) { __asm__ volatile("mov %0,%%cr0"::"r"(v)); }
ALWAYS_INLINE void write_cr3(u64 v) { __asm__ volatile("mov %0,%%cr3"::"r"(v)); }
ALWAYS_INLINE void write_cr4(u64 v) { __asm__ volatile("mov %0,%%cr4"::"r"(v)); }

ALWAYS_INLINE void cpuid_raw(u32 leaf, u32 sub, u32 *a, u32 *b, u32 *c, u32 *d) {
    __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                              : "a"(leaf), "c"(sub));
}

#endif
