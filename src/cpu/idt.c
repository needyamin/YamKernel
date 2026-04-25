/* ============================================================================
 * YamKernel — Interrupt Descriptor Table Implementation
 * ============================================================================ */

#include "idt.h"
#include "../lib/kprintf.h"
#include <nexus/panic.h>

#define IDT_ENTRIES 256

static idt_entry_t idt[IDT_ENTRIES] ALIGNED(16);
static idt_ptr_t   idt_ptr;
static isr_handler_t handlers[IDT_ENTRIES];

/* Exception names for debugging */
static const char *exception_names[] = {
    "Division By Zero",           /* 0 */
    "Debug",                      /* 1 */
    "Non-Maskable Interrupt",     /* 2 */
    "Breakpoint",                 /* 3 */
    "Overflow",                   /* 4 */
    "Bound Range Exceeded",       /* 5 */
    "Invalid Opcode",             /* 6 */
    "Device Not Available",       /* 7 */
    "Double Fault",               /* 8 */
    "Coprocessor Segment Overrun",/* 9 */
    "Invalid TSS",                /* 10 */
    "Segment Not Present",        /* 11 */
    "Stack-Segment Fault",        /* 12 */
    "General Protection Fault",   /* 13 */
    "Page Fault",                 /* 14 */
    "Reserved",                   /* 15 */
    "x87 FP Exception",           /* 16 */
    "Alignment Check",            /* 17 */
    "Machine Check",              /* 18 */
    "SIMD FP Exception",          /* 19 */
    "Virtualization Exception",   /* 20 */
    "Control Protection",         /* 21 */
};

/* External ISR stubs defined in isr.asm */
extern void isr_stub_0(void);
extern void isr_stub_1(void);
extern void isr_stub_2(void);
extern void isr_stub_3(void);
extern void isr_stub_4(void);
extern void isr_stub_5(void);
extern void isr_stub_6(void);
extern void isr_stub_7(void);
extern void isr_stub_8(void);
extern void isr_stub_9(void);
extern void isr_stub_10(void);
extern void isr_stub_11(void);
extern void isr_stub_12(void);
extern void isr_stub_13(void);
extern void isr_stub_14(void);
extern void isr_stub_15(void);
extern void isr_stub_16(void);
extern void isr_stub_17(void);
extern void isr_stub_18(void);
extern void isr_stub_19(void);
extern void isr_stub_20(void);
extern void isr_stub_21(void);
extern void isr_stub_22(void);
extern void isr_stub_23(void);
extern void isr_stub_24(void);
extern void isr_stub_25(void);
extern void isr_stub_26(void);
extern void isr_stub_27(void);
extern void isr_stub_28(void);
extern void isr_stub_29(void);
extern void isr_stub_30(void);
extern void isr_stub_31(void);

/* PIC IRQs (32-47) */
extern void isr_stub_32(void);
extern void isr_stub_33(void);
extern void isr_stub_34(void);
extern void isr_stub_35(void);
extern void isr_stub_36(void);
extern void isr_stub_37(void);
extern void isr_stub_38(void);
extern void isr_stub_39(void);
extern void isr_stub_40(void);
extern void isr_stub_41(void);
extern void isr_stub_42(void);
extern void isr_stub_43(void);
extern void isr_stub_44(void);
extern void isr_stub_45(void);
extern void isr_stub_46(void);
extern void isr_stub_47(void);

static void idt_set_gate(u8 vector, void (*handler)(void), u8 ist, u8 type_attr) {
    u64 addr = (u64)handler;
    idt[vector].offset_low  = (u16)(addr & 0xFFFF);
    idt[vector].selector    = 0x08; /* Kernel code segment */
    idt[vector].ist         = ist;
    idt[vector].type_attr   = type_attr;
    idt[vector].offset_mid  = (u16)((addr >> 16) & 0xFFFF);
    idt[vector].offset_high = (u32)(addr >> 32);
    idt[vector].reserved    = 0;
}

/* PIC (8259) initialization — remap IRQs to vectors 32-47 */
static void pic_remap(void) {
    /* Save masks */
    u8 mask1 = inb(0x21);
    u8 mask2 = inb(0xA1);

    /* Start initialization (ICW1) */
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();

    /* ICW2: Vector offsets */
    outb(0x21, 0x20); io_wait();  /* Master: IRQ 0-7 → vectors 32-39 */
    outb(0xA1, 0x28); io_wait();  /* Slave:  IRQ 8-15 → vectors 40-47 */

    /* ICW3: Cascade */
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();

    /* ICW4: 8086 mode */
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    /* Restore masks */
    outb(0x21, mask1);
    outb(0xA1, mask2);
}

/* Send End of Interrupt */
static void pic_eoi(u8 irq) {
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

/* Default exception handler */
static void default_exception_handler(interrupt_frame_t *frame) {
    const char *name = "Unknown";
    if (frame->int_no < 22)
        name = exception_names[frame->int_no];

    kprintf_color(0xFFFF3333,
        "\n!!! YamKernel EXCEPTION !!!\n"
        "  Exception: %s (#%lu)\n"
        "  Error Code: 0x%lx\n"
        "  RIP: 0x%lx  CS: 0x%lx\n"
        "  RSP: 0x%lx  SS: 0x%lx\n"
        "  RFLAGS: 0x%lx\n"
        "  RAX: 0x%lx  RBX: 0x%lx\n"
        "  RCX: 0x%lx  RDX: 0x%lx\n",
        name, frame->int_no, frame->error_code,
        frame->rip, frame->cs,
        frame->rsp, frame->ss,
        frame->rflags,
        frame->rax, frame->rbx,
        frame->rcx, frame->rdx
    );

    /* Halt on unrecoverable exceptions */
    if (frame->int_no < 32) {
        kpanic("Unrecoverable CPU exception #%lu", frame->int_no);
    }
}

/* C-level interrupt dispatcher (called from assembly stub) */
void isr_dispatch(interrupt_frame_t *frame) {
    u8 vector = (u8)frame->int_no;

    if (handlers[vector]) {
        handlers[vector](frame);
    } else if (vector < 32) {
        default_exception_handler(frame);
    }

    /* Send EOI for hardware IRQs (vectors 32-47) */
    if (vector >= 32 && vector < 48) {
        pic_eoi(vector - 32);
    }
}

void idt_register_handler(u8 vector, isr_handler_t handler) {
    handlers[vector] = handler;
}

void idt_init(void) {
    /* Zero everything */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        handlers[i] = NULL;
    }

    /* Remap PIC */
    pic_remap();

    /* Set up ISR gates for exceptions (0-31) */
    /* Type: 0x8E = Present | DPL0 | 64-bit Interrupt Gate */
    void (*isr_stubs[])(void) = {
        isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3,
        isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7,
        isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11,
        isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15,
        isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19,
        isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23,
        isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27,
        isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31,
        isr_stub_32, isr_stub_33, isr_stub_34, isr_stub_35,
        isr_stub_36, isr_stub_37, isr_stub_38, isr_stub_39,
        isr_stub_40, isr_stub_41, isr_stub_42, isr_stub_43,
        isr_stub_44, isr_stub_45, isr_stub_46, isr_stub_47,
    };

    for (int i = 0; i < 48; i++) {
        u8 ist = (i == 8 || i == 14) ? 1 : 0;  /* IST for double fault & page fault */
        idt_set_gate(i, isr_stubs[i], ist, 0x8E);
    }

    /* Load IDT */
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (u64)&idt;
    __asm__ volatile ("lidt %0" :: "m"(idt_ptr));

    /* Enable interrupts */
    sti();

    kprintf_color(0xFF00FF88, "[IDT] Loaded: 48 vectors (32 exceptions + 16 IRQs)\n");
}
