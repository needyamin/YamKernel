/* ============================================================================
 * YamKernel — Global Descriptor Table Implementation
 * Sets up kernel/user code & data segments + TSS for x86_64
 * ============================================================================ */

#include "gdt.h"
#include "../lib/string.h"
#include "../lib/kprintf.h"

/* GDT entries:
 * 0: Null descriptor
 * 1: Kernel Code (0x08)
 * 2: Kernel Data (0x10)
 * 3: User Code   (0x18) -- for future userspace
 * 4: User Data   (0x20) -- for future userspace
 * 5-6: TSS (takes 2 entries in 64-bit mode)
 */
#define GDT_ENTRIES 7

static gdt_entry_t gdt[GDT_ENTRIES] ALIGNED(16);
static tss_t tss ALIGNED(16);
static gdt_ptr_t gdt_ptr;

/* Kernel interrupt stack (16KB) */
static u8 kernel_stack[16384] ALIGNED(16);
static u8 ist_stack[16384] ALIGNED(16);

static void gdt_set_entry(int idx, u16 limit, u16 base_low, u8 base_mid,
                           u8 access, u8 granularity, u8 base_high) {
    gdt[idx].limit_low   = limit;
    gdt[idx].base_low    = base_low;
    gdt[idx].base_mid    = base_mid;
    gdt[idx].access      = access;
    gdt[idx].granularity = granularity;
    gdt[idx].base_high   = base_high;
}

/* External assembly function to load GDT and reload segments */
extern void gdt_flush(u64 gdt_ptr_addr);

void gdt_init(void) {
    /* Zero everything */
    memset(&gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    /* 0: Null descriptor */
    gdt_set_entry(0, 0, 0, 0, 0, 0, 0);

    /* 1: Kernel Code Segment (0x08) — 64-bit, ring 0 */
    gdt_set_entry(1, 0xFFFF, 0, 0, 0x9A, 0xAF, 0);

    /* 2: Kernel Data Segment (0x10) — ring 0 */
    gdt_set_entry(2, 0xFFFF, 0, 0, 0x92, 0xCF, 0);

    /* 3: User Data (0x18) — ring 3.   ORDER MATTERS for SYSRET:
     *    SYSRET sets CS=STAR.user_base+16, SS=STAR.user_base+8 (RPL=3). With
     *    STAR.user_base=0x10, we get SS=0x1B (this entry) + CS=0x23 (next). */
    gdt_set_entry(3, 0xFFFF, 0, 0, 0xF2, 0xCF, 0);

    /* 4: User Code Segment (0x20) — 64-bit, ring 3 */
    gdt_set_entry(4, 0xFFFF, 0, 0, 0xFA, 0xAF, 0);

    /* 5-6: TSS — 16-byte entry in 64-bit mode */
    u64 tss_addr = (u64)&tss;
    u32 tss_limit = sizeof(tss) - 1;

    /* TSS low entry */
    gdt[5].limit_low   = (u16)(tss_limit & 0xFFFF);
    gdt[5].base_low    = (u16)(tss_addr & 0xFFFF);
    gdt[5].base_mid    = (u8)((tss_addr >> 16) & 0xFF);
    gdt[5].access      = 0x89;  /* Present, TSS (Available) */
    gdt[5].granularity = (u8)((tss_limit >> 16) & 0x0F);
    gdt[5].base_high   = (u8)((tss_addr >> 24) & 0xFF);

    /* TSS high entry (upper 32 bits of base address) */
    u32 *tss_high = (u32 *)&gdt[6];
    tss_high[0] = (u32)(tss_addr >> 32);
    tss_high[1] = 0;

    /* Setup TSS */
    tss.rsp0 = (u64)&kernel_stack[sizeof(kernel_stack)];
    tss.ist1 = (u64)&ist_stack[sizeof(ist_stack)];
    tss.iopb_offset = sizeof(tss);

    /* Load GDT */
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (u64)&gdt;
    gdt_flush((u64)&gdt_ptr);

    /* Load TSS (selector 0x28 = entry 5) */
    __asm__ volatile (
        "mov $0x28, %%ax\n\t"
        "ltr %%ax"
        ::: "ax"
    );

    kprintf_color(0xFF00FF88, "[GDT] Loaded: 5 segments + TSS\n");
}

void gdt_set_kernel_stack(u64 stack) {
    tss.rsp0 = stack;
}
