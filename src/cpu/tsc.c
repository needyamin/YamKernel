/* YamKernel - TSC capability reporting */
#include "tsc.h"
#include "cpuid.h"
#include "../lib/kprintf.h"

static bool g_tsc_deadline;

u64 tsc_read(void) {
    u32 lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

bool tsc_deadline_available(void) {
    return g_tsc_deadline;
}

void tsc_init(void) {
    const cpuid_info_t *c = cpuid_get_info();
    g_tsc_deadline = c->has_tsc && c->has_apic && c->has_msr && c->has_tsc_deadline;
    kprintf_color(0xFF00FF88, "[TSC] rdtsc=%u invariant=%u deadline=%u sample=0x%lx\n",
                  c->has_tsc, c->has_invariant_tsc, g_tsc_deadline, tsc_read());
}
