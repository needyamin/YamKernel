/* YamKernel — demo: preemption + sleep_ms + mutex */
#include "sched.h"
#include "wait.h"
#include "../lib/kprintf.h"

static mutex_t print_mu = MUTEX_INIT;

static void heartbeat(void *arg) {
    const char *name = (const char *)arg;
    for (;;) {
        mutex_lock(&print_mu);
        kprintf_color(0xFF888888, "[%s] tick (vruntime-driven sleep)\n", name);
        mutex_unlock(&print_mu);
        task_sleep_ms(500);     /* blocks task instead of busy-spinning */
    }
}

void sched_demo_spawn(void) {
    sched_spawn("hb-A", heartbeat, (void *)"hb-A", 2);
    sched_spawn("hb-B", heartbeat, (void *)"hb-B", 2);
}
