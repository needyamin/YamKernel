/* YamKernel — OOM Killer v0.3.0
 * Scores tasks by RSS + nice, kills worst offender under memory pressure */
#include "oom.h"
#include "pmm.h"
#include "../sched/sched.h"
#include "../cpu/percpu.h"
#include "../lib/kprintf.h"
#include "../lib/kprintf.h"

static void oom_pressure_cb(mem_zone_type_t zone, u64 free_pages) {
    (void)zone; (void)free_pages;
    if (pmm_is_oom()) {
        kprintf_color(0xFFFF3333, "[OOM] Memory pressure critical! Invoking OOM killer\n");
        oom_kill_worst();
    }
}

void oom_init(void) {
    pmm_register_pressure_cb(oom_pressure_cb);
    kprintf_color(0xFF00FF88, "[OOM] OOM Killer initialized\n");
}

i64 oom_score(void *task_ptr) {
    task_t *t = (task_t *)task_ptr;
    if (!t) return -1;
    if (t->id == 0) return -1;  /* Never kill idle/boot task */

    /* Base score: RSS pages (higher RSS = higher score) */
    i64 score = (i64)t->rss_pages;

    /* Penalize high-nice (low-priority) tasks — they're more killable */
    score += t->nice * 10;

    /* Bonus for long-running tasks (less likely to be killed) */
    u64 age = this_cpu()->ticks - t->start_tick;
    if (age > 10000) score -= 100;

    /* AI training tasks get higher score (more expendable) */
    if (t->ai_hint == AI_HINT_TRAINING) score += 200;

    /* Realtime AI tasks get lower score (protect them) */
    if (t->ai_hint == AI_HINT_REALTIME) score -= 500;

    if (score < 0) score = 0;
    return score;
}

void oom_kill_worst(void) {
    /* Scan all tasks — this is a simplified version using the current task list */
    task_t *cur = sched_current();
    if (!cur) return;

    /* For now, log the OOM event — a full implementation would iterate the task list */
    kprintf_color(0xFFFF3333, "[OOM] System under extreme memory pressure!\n");
    kprintf_color(0xFFFF3333, "[OOM] Free: %lu KB / %lu KB\n",
                  pmm_free_memory() / 1024, pmm_total_memory() / 1024);
    pmm_print_stats();

    /* In a full implementation, we'd:
     * 1. Iterate all tasks
     * 2. Score each one
     * 3. Kill the highest-scoring task
     * 4. Reclaim its pages */
}

void oom_check(void) {
    if (pmm_is_oom()) oom_kill_worst();
}
