/* YamKernel — CPU Power Management v0.3.0
 * Idle governor using hlt/mwait C-states */
#include "power.h"
#include "cpuid.h"
#include "percpu.h"
#include "../lib/kprintf.h"

static power_state_t g_power = {0};

void power_init(void) {
    const cpuid_info_t *cpu = cpuid_get_info();
    /* Check MONITOR/MWAIT support (CPUID.01H:ECX bit 3) */
    g_power.mwait_supported = false; /* Conservative default */
    g_power.current_state = CSTATE_C0;
    kprintf_color(0xFF00FF88, "[POWER] CPU idle governor initialized (mwait=%s)\n",
                  g_power.mwait_supported ? "yes" : "no (using hlt)");
    (void)cpu;
}

void power_idle(void) {
    percpu_t *pc = this_cpu();
    u64 idle_start = pc->ticks;

    if (g_power.mwait_supported) {
        /* Use MWAIT for deeper C-states */
        volatile u64 monitor_addr = 0;
        __asm__ volatile("monitor" :: "a"(&monitor_addr), "c"(0), "d"(0));
        __asm__ volatile("mwait" :: "a"(0x10), "c"(0));  /* C1E hint */
        g_power.current_state = CSTATE_C1E;
        g_power.c1e_residency++;
    } else {
        /* Fallback to HLT (C1) */
        __asm__ volatile("hlt");
        g_power.current_state = CSTATE_C1;
        g_power.c1_residency++;
    }

    g_power.total_idle_ticks += (pc->ticks - idle_start);
    g_power.current_state = CSTATE_C0;
}

const power_state_t *power_get_state(void) { return &g_power; }

void power_print_stats(void) {
    kprintf_color(0xFF00DDFF, "[POWER] Idle stats: C1=%lu C1E=%lu C3=%lu total_idle=%lu\n",
                  g_power.c1_residency, g_power.c1e_residency,
                  g_power.c3_residency, g_power.total_idle_ticks);
}
