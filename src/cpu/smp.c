/* YamKernel — SMP initialization using Limine's SMP feature */
#include "smp.h"
#include "gdt.h"
#include "idt.h"
#include "percpu.h"
#include "acpi.h"
#include "apic.h"
#include "msr.h"
#include "security.h"
#include "../lib/kprintf.h"
#include "../mem/vmm.h"
#include "../sched/sched.h"
#include "../kernel/api/syscall.h"
#include <limine.h>

static u32 g_smp_cpu_count = 1;
static volatile u32 g_aps_booted = 0;

/* Entry point for Application Processors (APs) provided by Limine.
 * APs enter here in 64-bit long mode, paging enabled (Limine's page table),
 * interrupts disabled. */
static void ap_trampoline(struct limine_smp_info *info) {
    u32 cpu_id = info->processor_id;
    u32 apic_id = info->lapic_id;

    /* 1. Load per-CPU GDT */
    gdt_init(cpu_id);

    /* 2. Load shared IDT */
    idt_load();

    /* 3. Setup per-CPU data (GS_BASE) */
    percpu_init(cpu_id, apic_id);

    /* 4. Enable security features (NX, SMEP, SMAP).
     * Must be done BEFORE loading CR3, because the kernel page tables
     * use the NX bit. If EFER.NXE=0, loading a page table with NX=1 causes #PF. */
    security_init();

    /* 5. Switch to Kernel's Page Table (from Limine's) */
    __asm__ volatile ("mov %0, %%cr3" :: "r"(vmm_virt_hhdm_to_phys(vmm_get_kernel_pml4())));

    /* 6. Enable LAPIC for this core */
    apic_init_local();
    
    /* 6.5. Enable SYSCALL instruction (EFER.SCE) on this core */
    syscall_init();
    
    /* Signal BSP that we are up */
    __sync_fetch_and_add(&g_aps_booted, 1);

    /* 7. Start the scheduler on this core */
    sched_init();
    apic_timer_start(100);
    sched_start();
}

void smp_init(struct limine_smp_response *smp_resp) {
    if (!smp_resp) {
        kprintf_color(0xFFFF3333, "[SMP] No Limine SMP response! Single-core fallback.\n");
        return;
    }

    g_smp_cpu_count = smp_resp->cpu_count;
    kprintf_color(0xFF00FF88, "[SMP] Limine detected %u CPUs. BSP LAPIC ID: %u\n", 
                  smp_resp->cpu_count, smp_resp->bsp_lapic_id);

    /* The BSP is CPU 0 in our logic, Limine's `cpus` array includes the BSP.
     * We iterate through all CPUs and start the APs. */
    for (u32 i = 0; i < smp_resp->cpu_count; i++) {
        struct limine_smp_info *cpu = smp_resp->cpus[i];

        /* Skip the BSP, it's already running! */
        if (cpu->lapic_id == smp_resp->bsp_lapic_id) {
            /* We can assign CPU ID 0 to BSP */
            cpu->processor_id = 0;
            continue;
        }

        /* Assign logical ID based on index */
        cpu->processor_id = i;

        /* Tell Limine to wake up the AP */
        cpu->goto_address = ap_trampoline;
    }

    /* Wait for all APs to boot */
    u32 expected_aps = smp_resp->cpu_count - 1;
    while (g_aps_booted < expected_aps) {
        __asm__ volatile ("pause");
    }

    kprintf_color(0xFF00FF88, "[SMP] All %u Application Processors booted and ready.\n", g_aps_booted);
}

u32 smp_cpu_count(void) { 
    return g_smp_cpu_count; 
}
