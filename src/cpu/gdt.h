/* ============================================================================
 * YamKernel — Global Descriptor Table
 * ============================================================================ */

#ifndef _CPU_GDT_H
#define _CPU_GDT_H

#include <nexus/types.h>

/* GDT entry structure */
typedef struct PACKED {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  granularity;
    u8  base_high;
} gdt_entry_t;

/* TSS structure for x86_64 */
typedef struct PACKED {
    u32 reserved0;
    u64 rsp0;          /* Stack pointer for ring 0 */
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist1;          /* Interrupt Stack Table entries */
    u64 ist2;
    u64 ist3;
    u64 ist4;
    u64 ist5;
    u64 ist6;
    u64 ist7;
    u64 reserved2;
    u16 reserved3;
    u16 iopb_offset;
} tss_t;

/* GDT pointer */
typedef struct PACKED {
    u16 limit;
    u64 base;
} gdt_ptr_t;

/* Initialize GDT with kernel/user segments and TSS */
void gdt_init(void);

/* Set the kernel stack pointer in the TSS (for ring transitions) */
void gdt_set_kernel_stack(u64 stack);

#endif /* _CPU_GDT_H */
