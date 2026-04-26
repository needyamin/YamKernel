/* YamKernel — APIC, APIC timer, IO-APIC */
#include "apic.h"
#include "acpi.h"
#include "msr.h"
#include "../mem/vmm.h"
#include "../lib/kprintf.h"

/* APIC MMIO sits in reserved phys; map it explicitly.
 * We try a few high virtual windows and verify translation, because a window
 * may already be occupied by preexisting huge mappings. */
static volatile u8 *mmio_map(u64 phys, u64 hhdm) {
    static const u64 win_base[] = {
        0xFFFFD00000000000ULL,
        0xFFFFC00000000000ULL,
        0xFFFFFF0000000000ULL
    };
    u64 *pml4 = vmm_get_kernel_pml4();
    u64 page = phys & ~0xFFFULL;
    u64 off  = phys & 0xFFFULL;

    /* 1) Try dedicated windows first. */
    for (u32 i = 0; i < (u32)(sizeof(win_base)/sizeof(win_base[0])); i++) {
        u64 virt_page = win_base[i] + page;
        if (vmm_map_page(pml4, virt_page, page, VMM_FLAG_WRITE | VMM_FLAG_NOCACHE)) {
            u64 chk = vmm_virt_to_phys(pml4, virt_page);
            if ((chk & ~0xFFFULL) == page)
                return (volatile u8 *)(virt_page + off);
        }
    }

    /* 2) Fallback to HHDM if already mapped by bootloader. */
    u64 hhdm_virt = hhdm + page;
    u64 chk = vmm_virt_to_phys(pml4, hhdm_virt);
    if ((chk & ~0xFFFULL) == page)
        return (volatile u8 *)(hhdm_virt + off);

    return NULL;
}

#define LAPIC_ID         0x020
#define LAPIC_EOI        0x0B0
#define LAPIC_SVR        0x0F0
#define LAPIC_LVT_TIMER  0x320
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_CUR  0x390
#define LAPIC_TIMER_DIV  0x3E0

static volatile u8 *g_lapic = NULL;
static volatile u8 *g_ioapic = NULL;
static u32 g_ticks_per_ms = 0;

static u32 lapic_read(u32 r)            { return *(volatile u32 *)(g_lapic + r); }
static void lapic_write(u32 r, u32 v)   { *(volatile u32 *)(g_lapic + r) = v; }

void apic_eoi(void) { if (g_lapic) lapic_write(LAPIC_EOI, 0); }
bool apic_active(void) { return g_lapic != NULL; }

/* Mask 8259 PIC fully (we use APIC now) */
static void pic_disable(void) {
    outb(0x21, 0xFF); outb(0xA1, 0xFF);
}

void apic_init(u64 hhdm) {
    const acpi_info_t *a = acpi_get();
    if (!a->lapic_addr) { kprintf_color(0xFFFF3333, "[APIC] no LAPIC\n"); return; }

    g_lapic = mmio_map(a->lapic_addr, hhdm);
    if (!g_lapic) { kprintf_color(0xFFFF3333, "[APIC] LAPIC map failed\n"); return; }

    /* Enable LAPIC via MSR + spurious vector, mask legacy 8259 PIC. */
    wrmsr(MSR_APIC_BASE, rdmsr(MSR_APIC_BASE) | (1ULL << 11));
    lapic_write(LAPIC_SVR, 0x100 | APIC_VEC_SPURIOUS);
    pic_disable();

    kprintf_color(0xFF00FF88, "[APIC] LAPIC @ %p  id=%u\n",
                  (void *)g_lapic, lapic_read(LAPIC_ID) >> 24);
}

/* Calibrate APIC timer by polling PIT channel 2 for a 10 ms gate.
 * IRQ-free, works regardless of PIC mask state. */
static void calibrate(void) {
    /* PIT ch2: gate via 0x61 bit0, OUT readable on bit5.
     * 1193182 Hz / 100 Hz = 11932 → ~10 ms one-shot. */
    u8 v = (inb(0x61) & 0xFD) | 0x01;     /* gate hi, speaker off */
    outb(0x61, v);
    outb(0x43, 0xB2);                      /* ch2, lobyte/hibyte, mode 0 */
    outb(0x42, 11932 & 0xFF);
    outb(0x42, (11932 >> 8) & 0xFF);
    /* Restart by toggling gate */
    v = inb(0x61) & 0xFE;
    outb(0x61, v);
    outb(0x61, v | 1);

    lapic_write(LAPIC_TIMER_DIV, 0x3);
    lapic_write(LAPIC_TIMER_INIT, 0xFFFFFFFF);

    while ((inb(0x61) & 0x20) == 0) { }    /* wait for ch2 OUT high */

    u32 elapsed = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CUR);
    g_ticks_per_ms = elapsed / 10;
    if (g_ticks_per_ms == 0) g_ticks_per_ms = 100000;
}

void apic_timer_start(u32 hz) {
    if (!g_lapic) return;
    if (g_ticks_per_ms == 0) calibrate();

    u32 count = (g_ticks_per_ms * 1000) / hz;
    lapic_write(LAPIC_TIMER_DIV, 0x3);
    lapic_write(LAPIC_LVT_TIMER, APIC_VEC_TIMER | (1 << 17));   /* periodic */
    lapic_write(LAPIC_TIMER_INIT, count);

    kprintf_color(0xFF00FF88, "[APIC] timer %u Hz (%u ticks/ms)\n", hz, g_ticks_per_ms);
}

/* ---- IO-APIC ---- */

static u32 ioapic_read(u32 reg) {
    *(volatile u32 *)(g_ioapic + 0x00) = reg;
    return *(volatile u32 *)(g_ioapic + 0x10);
}
static void ioapic_write(u32 reg, u32 val) {
    *(volatile u32 *)(g_ioapic + 0x00) = reg;
    *(volatile u32 *)(g_ioapic + 0x10) = val;
}

void ioapic_init(u64 hhdm) {
    const acpi_info_t *a = acpi_get();
    if (a->ioapic_count == 0) return;
    g_ioapic = mmio_map(a->ioapics[0].mmio_addr, hhdm);
    if (!g_ioapic) { kprintf_color(0xFFFF3333, "[IOAPIC] map failed\n"); return; }

    /* Mask all entries */
    u32 max = (ioapic_read(1) >> 16) & 0xFF;
    for (u32 i = 0; i <= max; i++) {
        ioapic_write(0x10 + i * 2, 0x10000);   /* masked */
        ioapic_write(0x11 + i * 2, 0);
    }
    kprintf_color(0xFF00FF88, "[IOAPIC] @ %p  max-redir=%u\n", (void *)g_ioapic, max);
}

void ioapic_set_irq(u8 irq, u8 vector, u8 lapic_id) {
    if (!g_ioapic) return;
    u32 lo = vector;                          /* fixed delivery, edge, unmasked */
    u32 hi = (u32)lapic_id << 24;
    ioapic_write(0x10 + irq * 2, lo);
    ioapic_write(0x11 + irq * 2, hi);
}
