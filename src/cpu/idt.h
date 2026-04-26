/* ============================================================================
 * YamKernel — Interrupt Descriptor Table
 * ============================================================================ */

#ifndef _CPU_IDT_H
#define _CPU_IDT_H

#include <nexus/types.h>

/* IDT entry (gate descriptor) for x86_64 */
typedef struct PACKED {
    u16 offset_low;     /* Offset bits 0-15 */
    u16 selector;       /* Code segment selector */
    u8  ist;            /* Interrupt Stack Table offset (bits 0-2) */
    u8  type_attr;      /* Type and attributes */
    u16 offset_mid;     /* Offset bits 16-31 */
    u32 offset_high;    /* Offset bits 32-63 */
    u32 reserved;
} idt_entry_t;

/* IDT pointer */
typedef struct PACKED {
    u16 limit;
    u64 base;
} idt_ptr_t;

/* Interrupt frame pushed by CPU + our ISR stub */
typedef struct PACKED {
    /* Pushed by our stub */
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 int_no, error_code;
    /* Pushed by CPU */
    u64 rip, cs, rflags, rsp, ss;
} interrupt_frame_t;

/* Initialize the IDT */
void idt_init(void);

/* Register an interrupt handler */
typedef void (*isr_handler_t)(interrupt_frame_t *frame);
void idt_register_handler(u8 vector, isr_handler_t handler);

/* Load the IDT on the current CPU (used by APs) */
void idt_load(void);

#endif /* _CPU_IDT_H */
