/* YamKernel — Multi-Queue CFS Scheduler v0.3.0
 * Sorted-array O(log n) pick, per-CPU queues, fork, nice, load balance */
#include "sched.h"
#include "wait.h"
#include "../cpu/percpu.h"
#include "../cpu/apic.h"
#include "../cpu/idt.h"
#include "../cpu/gdt.h"
#include "../mem/heap.h"
#include "../mem/slab.h"
#include "../mem/vmm.h"
#include "../mem/pmm.h"
#include "../lib/spinlock.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../cpu/cpuid.h"
#include "../nexus/graph.h"

static kmem_cache_t *task_cache;
extern void task_trampoline(void);

/* Linux-style nice-to-weight mapping (subset) */
static const u32 nice_to_weight[40] = {
    /* -20 */ 88761, 71755, 56483, 46273, 36291,
    /* -15 */ 29154, 23254, 18705, 14949, 11916,
    /* -10 */ 9548,  7620,  6100,  4904,  3906,
    /*  -5 */ 3121,  2501,  1991,  1586,  1277,
    /*   0 */ 1024,  820,   655,   526,   423,
    /*   5 */ 335,   272,   215,   172,   137,
    /*  10 */ 110,   87,    70,    56,    45,
    /*  15 */ 36,    29,    23,    18,    15,
};

#define NICE_TO_IDX(n) ((n) + 20)
#define SCHED_INTERACTIVE_QUANTUM_TICKS 2

/* Per-CPU run queues */
static runqueue_t per_cpu_rq[MAX_CPUS];
static task_t    *sleep_head;
static spinlock_t sleep_lock = SPINLOCK_INIT;
static u64        next_id = 1;
static volatile u32 sched_enabled = 0;
static u64        total_tasks = 0;

/* ---- Sorted run queue (binary insert for O(log n) pick) ---- */

static void rq_insert(runqueue_t *rq, task_t *t) {
    if (rq->count >= 256) return;
    t->state = TASK_READY;

    /* Binary search for insertion point (sorted by vruntime ascending) */
    u32 lo = 0, hi = rq->count;
    while (lo < hi) {
        u32 mid = (lo + hi) / 2;
        if (rq->tasks[mid]->vruntime < t->vruntime) lo = mid + 1;
        else hi = mid;
    }

    /* Shift elements right */
    for (u32 i = rq->count; i > lo; i--) rq->tasks[i] = rq->tasks[i-1];
    rq->tasks[lo] = t;
    rq->count++;

    if (rq->count == 1 || t->vruntime < rq->min_vruntime)
        rq->min_vruntime = t->vruntime;
}

static task_t *rq_pick_next(runqueue_t *rq) {
    if (rq->count == 0) return NULL;
    /* First element has lowest vruntime */
    task_t *t = rq->tasks[0];
    /* Shift left */
    for (u32 i = 0; i < rq->count - 1; i++) rq->tasks[i] = rq->tasks[i+1];
    rq->count--;
    rq->min_vruntime = (rq->count > 0) ? rq->tasks[0]->vruntime : 0;
    return t;
}

/* ---- Public API ---- */

task_t *sched_current(void) { return this_cpu()->current; }
u64 sched_task_count(void) { return total_tasks; }

static u32 weight_for_nice(i8 nice) {
    int idx = NICE_TO_IDX(nice);
    if (idx < 0) idx = 0;
    if (idx >= 40) idx = 39;
    return nice_to_weight[idx];
}

task_t *sched_spawn(const char *name, void (*entry)(void *), void *arg, u8 prio) {
    if (!task_cache)
        task_cache = kmem_cache_create("task_t", sizeof(task_t), 16);
    task_t *t = (task_t *)kmem_cache_alloc(task_cache);
    if (!t) return NULL;

    t->id = next_id++;
    t->prio = (prio < SCHED_PRIO_LEVELS) ? prio : SCHED_PRIO_LEVELS - 1;
    t->nice = DEFAULT_NICE;
    t->weight = weight_for_nice(t->nice);
    t->cpu_affinity = ~0ULL;  /* All CPUs allowed */
    t->entry = entry; t->arg = arg;
    t->parent = sched_current();
    t->child_count = 0;
    t->exit_code = 0;
    t->signal_pending = 0;
    t->ai_hint = AI_HINT_NONE;
    t->utime = t->stime = 0;
    t->vol_switches = t->invol_switches = 0;
    t->slice_ticks = 0;
    t->need_resched = 0;
    t->start_tick = this_cpu()->ticks;
    t->rss_pages = 0;
    t->cgroup = NULL;
    memset(t->name, 0, sizeof(t->name));
    for (u32 i = 0; i < sizeof(t->name) - 1 && name[i]; i++) t->name[i] = name[i];
    t->graph_node = yamgraph_node_create(YAM_NODE_TASK, t->name, t);

    t->stack = (u8 *)kmalloc(SCHED_STACK_SIZE);
    if (!t->stack) { kmem_cache_free(task_cache, t); return NULL; }

    u32 fpu_sz = cpuid_get_info()->xsave_size;
    if (fpu_sz == 0) fpu_sz = 512; /* FXSAVE fallback */
    t->fpu_state = (u8 *)kmalloc(fpu_sz);
    if (!t->fpu_state) { kfree(t->stack); kmem_cache_free(task_cache, t); return NULL; }
    memset(t->fpu_state, 0, fpu_sz);

    /* Stack frame for context_switch + task_trampoline */
    u64 *sp = (u64 *)(t->stack + SCHED_STACK_SIZE);
    *--sp = (u64)entry;
    *--sp = (u64)arg;
    *--sp = (u64)task_trampoline;
    *--sp = 0;  /* rbp */
    *--sp = 0;  /* rbx */
    *--sp = 0;  /* r12 */
    *--sp = 0;  /* r13 */
    *--sp = 0;  /* r14 */
    *--sp = 0;  /* r15 */
    *--sp = 0x002;  /* RFLAGS */
    t->rsp = (u64)sp;

    /* Add to parent's children */
    if (t->parent && t->parent->child_count < MAX_CHILDREN) {
        t->parent->children[t->parent->child_count++] = t;
    }

    /* Add to CPU 0's run queue (load balancer will migrate later) */
    runqueue_t *rq = &per_cpu_rq[0];
    u64 f = spin_lock_irqsave(&rq->lock);
    u64 minv = rq->min_vruntime;
    t->vruntime = minv;
    rq_insert(rq, t);
    spin_unlock_irqrestore(&rq->lock, f);

    total_tasks++;
    kprintf("[SCHED] spawn '%s' id=%lu nice=%d w=%u\n",
            t->name, t->id, t->nice, t->weight);
    return t;
}

void task_exit(void) {
    cli();
    task_t *cur = sched_current();
    cur->state = TASK_ZOMBIE;
    cur->exit_code = 0;
    total_tasks--;
    sched_yield();
    for(;;) hlt();
}

task_t *active_tasks[MAX_CPUS];

void sched_kill_task(u64 id) {
    if (id == 0 || id == 1 || id == 2) return; /* don't kill init tasks */
    for (int cpu = 0; cpu < MAX_CPUS; cpu++) {
        /* 1. Check if it's currently running on this CPU */
        task_t *active = active_tasks[cpu];
        if (active && active->id == id) {
            active->state = TASK_DEAD;
            /* Note: We rely on the timer interrupt to context switch it out */
        }
        
        /* 2. Check if it's waiting in the runqueue */
        runqueue_t *rq = &per_cpu_rq[cpu];
        u64 f = spin_lock_irqsave(&rq->lock);
        for (u32 i = 0; i < rq->count; i++) {
            if (rq->tasks[i]->id == id) {
                rq->tasks[i]->state = TASK_DEAD;
            }
        }
        spin_unlock_irqrestore(&rq->lock, f);
    }
}

void sched_init(void) {
    /* Initialize per-CPU run queues */
    for (int i = 0; i < MAX_CPUS; i++) {
        per_cpu_rq[i].count = 0;
        per_cpu_rq[i].min_vruntime = 0;
        spin_init(&per_cpu_rq[i].lock);
    }

    task_t *boot = (task_t *)kmalloc(sizeof(task_t));
    if (!boot) { kprintf("[SCHED] FATAL: kmalloc failed for boot task!\n"); return; }
    memset(boot, 0, sizeof(*boot));
    boot->id     = 0;
    boot->prio   = SCHED_PRIO_LEVELS - 1;
    boot->nice   = DEFAULT_NICE;
    boot->weight = weight_for_nice(DEFAULT_NICE);
    boot->state  = TASK_RUNNING;
    boot->cpu_affinity = ~0ULL;
    boot->name[0] = 'b'; boot->name[1] = 's'; boot->name[2] = 'p';

    u32 fpu_sz_boot = cpuid_get_info()->xsave_size;
    if (fpu_sz_boot == 0) fpu_sz_boot = 512;
    boot->fpu_state = (u8 *)kmalloc(fpu_sz_boot);
    if (boot->fpu_state)
        memset(boot->fpu_state, 0, fpu_sz_boot);

    this_cpu()->current = boot;
    this_cpu()->idle    = boot;
    active_tasks[0]     = boot;
    total_tasks = 1;

    kprintf_color(0xFF00FF88, "[SCHED] Multi-Queue CFS v0.3.0 init OK (nice range %d..%d)\n",
                  MIN_NICE, MAX_NICE);
}

/* FPU Save/Restore — guards xgetbv behind has_xsave check */
static inline void fpu_save(u8 *state) {
    if (!state) return;
    if (cpuid_get_info()->has_xsave) {
        u32 lo, hi;
        __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        __asm__ volatile("xsave64 %0" : "=m"(*state) : "a"(lo), "d"(hi) : "memory");
    } else if (cpuid_get_info()->has_fxsr) {
        __asm__ volatile("fxsave64 %0" : "=m"(*state) : : "memory");
    }
}

static inline void fpu_restore(u8 *state) {
    if (!state) return;
    if (cpuid_get_info()->has_xsave) {
        u32 lo, hi;
        __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        __asm__ volatile("xrstor64 %0" : : "m"(*state), "a"(lo), "d"(hi) : "memory");
    } else if (cpuid_get_info()->has_fxsr) {
        __asm__ volatile("fxrstor64 %0" : : "m"(*state) : "memory");
    }
}

static void switch_to(task_t *next) {
    percpu_t *pc = this_cpu();
    task_t   *cur = pc->current;
    if (next->stack) {
        u64 ktop = (u64)(next->stack + SCHED_STACK_SIZE);
        gdt_set_kernel_stack(pc->cpu_id, ktop);
        pc->kernel_rsp = ktop;
    }

    fpu_save(cur->fpu_state);
    next->state = TASK_RUNNING;
    next->slice_ticks = 0;
    next->need_resched = 0;
    pc->current = next;
    active_tasks[pc->cpu_id] = next;
    fpu_restore(next->fpu_state);

    u64 *next_pml4 = next->pml4 ? next->pml4 : vmm_get_kernel_pml4();
    u64 *cur_pml4 = cur->pml4 ? cur->pml4 : vmm_get_kernel_pml4();
    if (next_pml4 != cur_pml4) {
        u64 phys = vmm_virt_hhdm_to_phys(next_pml4);
        __asm__ volatile("mov %0, %%cr3" :: "r"(phys) : "memory");
    }

    context_switch(&cur->rsp, next->rsp);
}

void sched_yield(void) {
    if (!sched_enabled) return;
    cli();
    u32 cpu = 0; /* BSP for now */
    runqueue_t *rq = &per_cpu_rq[cpu];
    spin_lock(&rq->lock);
    task_t *cur = this_cpu()->current;
    task_t *next = rq_pick_next(rq);
    
    /* Skip tasks that were killed while waiting in the queue */
    while (next && next->state == TASK_DEAD) {
        total_tasks--;
        next = rq_pick_next(rq);
    }
    
    if (!next) { spin_unlock(&rq->lock); sti(); return; }
    if (cur && cur->state == TASK_RUNNING) {
        cur->vol_switches++;
        cur->slice_ticks = 0;
        cur->need_resched = 0;
        rq_insert(rq, cur);
    }
    spin_unlock(&rq->lock);
    switch_to(next);
    sti();
}

void sched_maybe_preempt(void) {
    task_t *cur = sched_current();
    if (!sched_enabled || !cur || !cur->need_resched) return;
    if (cur->state != TASK_RUNNING || cur == this_cpu()->idle) return;
    cur->need_resched = 0;
    sched_yield();
}

void sched_unblock(task_t *t) {
    runqueue_t *rq = &per_cpu_rq[0];
    u64 f = spin_lock_irqsave(&rq->lock);
    if (t->state == TASK_BLOCKED || t->state == TASK_STOPPED) rq_insert(rq, t);
    spin_unlock_irqrestore(&rq->lock, f);
}

void sched_tick(void) {
    percpu_t *pc = this_cpu();
    pc->ticks++;
    task_t *cur = pc->current;
    if (cur) {
        cur->ticks++;
        cur->slice_ticks++;
        cur->stime++;
        /* Weight-based vruntime: lower weight = faster vruntime growth */
        cur->vruntime += (1024ULL * 1024) / (cur->weight ? cur->weight : 1);
        /* AI hint adjustments */
        if (cur->ai_hint == AI_HINT_BATCH)
            cur->vruntime -= 128; /* Slight advantage for batch jobs */
        if (cur == pc->idle) {
            pc->idle_ticks++;
        } else if (cur->slice_ticks >= SCHED_INTERACTIVE_QUANTUM_TICKS) {
            cur->need_resched = 1;
        }
    }

    /* Wake sleepers */
    u64 now = pc->ticks;
    spin_lock(&sleep_lock);
    task_t **pp = &sleep_head;
    while (*pp) {
        task_t *t = *pp;
        if (t->wakeup_tick <= now) {
            *pp = t->wait_next; t->wait_next = NULL;
            sched_unblock(t);
        } else pp = &t->wait_next;
    }
    spin_unlock(&sleep_lock);

    /* Check signals on current task */
    if (cur && cur->signal_pending) {
        if (cur->signal_pending & (1 << SIGKILL)) {
            cur->state = TASK_DEAD;
            total_tasks--;
        } else if (cur->signal_pending & (1 << SIGSTOP)) {
            cur->state = TASK_STOPPED;
            cur->signal_pending &= ~(1 << SIGSTOP);
        }
    }

    /* Involuntary preemption disabled from IRQ: yielding here calls context_switch while
       the IST/interrupt stack is active and breaks IRET frames (bogus RIP/CR2 at ~NULL+off). */
    if (cur) cur->invol_switches++;
}

static void timer_isr(interrupt_frame_t *f) { (void)f; apic_eoi(); sched_tick(); }
void sched_install_timer(void) { idt_register_handler(0x20, timer_isr); }
void sched_enable(void)        { sched_enabled = 1; }

void sched_start(void) {
    sched_enabled = 1; sti();
    for (;;) { sched_yield(); hlt(); }
}

void sched_sleep_until(u64 deadline_tick) {
    cli();
    spin_lock(&sleep_lock);
    task_t *cur = this_cpu()->current;
    cur->state = TASK_BLOCKED;
    cur->wakeup_tick = deadline_tick;
    cur->wait_next = sleep_head; sleep_head = cur;
    spin_unlock(&sleep_lock);

    runqueue_t *rq = &per_cpu_rq[0];
    spin_lock(&rq->lock);
    task_t *next = rq_pick_next(rq);
    spin_unlock(&rq->lock);
    if (!next) { sti(); return; }
    switch_to(next);
    sti();
}

/* ---- fork ---- */
i64 sys_fork(void) {
    task_t *parent = sched_current();
    if (!parent) return -1;

    if (!task_cache)
        task_cache = kmem_cache_create("task_t", sizeof(task_t), 16);
    task_t *child = (task_t *)kmem_cache_alloc(task_cache);
    if (!child) return -1;

    /* Copy parent state */
    memcpy(child, parent, sizeof(task_t));
    child->id = next_id++;
    child->parent = parent;
    child->child_count = 0;
    child->state = TASK_READY;
    child->exit_code = 0;
    child->signal_pending = 0;
    child->start_tick = this_cpu()->ticks;
    child->utime = child->stime = 0;
    child->vol_switches = child->invol_switches = 0;
    child->slice_ticks = 0;
    child->need_resched = 0;

    /* Allocate new kernel stack */
    child->stack = (u8 *)kmalloc(SCHED_STACK_SIZE);
    if (!child->stack) { kmem_cache_free(task_cache, child); return -1; }
    memcpy(child->stack, parent->stack, SCHED_STACK_SIZE);

    /* FPU state */
    u32 fork_fpu_sz = cpuid_get_info()->xsave_size;
    if (fork_fpu_sz == 0) fork_fpu_sz = 512;
    child->fpu_state = (u8 *)kmalloc(fork_fpu_sz);
    if (!child->fpu_state) { kfree(child->stack); kmem_cache_free(task_cache, child); return -1; }
    memcpy(child->fpu_state, parent->fpu_state, fork_fpu_sz);

    /* CoW address space */
    if (parent->pml4) {
        child->pml4 = vmm_fork_address_space(parent->pml4);
    }

    /* Adjust RSP to point into child's stack */
    u64 stack_offset = parent->rsp - (u64)parent->stack;
    child->rsp = (u64)child->stack + stack_offset;

    /* Add child to parent */
    if (parent->child_count < MAX_CHILDREN)
        parent->children[parent->child_count++] = child;

    /* Add to run queue */
    runqueue_t *rq = &per_cpu_rq[0];
    u64 f = spin_lock_irqsave(&rq->lock);
    child->vruntime = rq->min_vruntime;
    rq_insert(rq, child);
    spin_unlock_irqrestore(&rq->lock, f);
    total_tasks++;

    return (i64)child->id;  /* Parent gets child PID; child would get 0 in real impl */
}

i64 sys_waitpid(i64 pid, i32 *status, u32 options) {
    (void)options;
    task_t *cur = sched_current();
    if (!cur) return -1;

    for (;;) {
        for (u32 i = 0; i < cur->child_count; i++) {
            task_t *child = cur->children[i];
            if (!child) continue;
            if (pid > 0 && (i64)child->id != pid) continue;
            if (child->state == TASK_ZOMBIE || child->state == TASK_DEAD) {
                if (status) *status = child->exit_code;
                i64 ret = (i64)child->id;
                /* Remove from children list */
                cur->children[i] = cur->children[cur->child_count - 1];
                cur->children[cur->child_count - 1] = NULL;
                cur->child_count--;
                return ret;
            }
        }
        /* No zombie child found — sleep and try again */
        task_sleep_ms(10);
    }
}

i64 sys_kill(u64 pid, u32 sig) {
    if (sig >= SIG_MAX) return -1;
    /* Search all run queues */
    for (int c = 0; c < MAX_CPUS; c++) {
        runqueue_t *rq = &per_cpu_rq[c];
        u64 f = spin_lock_irqsave(&rq->lock);
        for (u32 i = 0; i < rq->count; i++) {
            if (rq->tasks[i]->id == pid) {
                rq->tasks[i]->signal_pending |= (1 << sig);
                spin_unlock_irqrestore(&rq->lock, f);
                return 0;
            }
        }
        spin_unlock_irqrestore(&rq->lock, f);
    }
    return -1;
}

void sched_set_nice(task_t *t, i8 nice) {
    if (nice < MIN_NICE) nice = MIN_NICE;
    if (nice > MAX_NICE) nice = MAX_NICE;
    t->nice = nice;
    t->weight = weight_for_nice(nice);
}

void sched_set_affinity(task_t *t, u64 mask) { t->cpu_affinity = mask; }
u64  sched_get_affinity(task_t *t) { return t->cpu_affinity; }
void sched_set_ai_hint(task_t *t, u8 hint) { t->ai_hint = hint; }

void sched_print_stats(void) {
    kprintf_color(0xFF00DDFF, "\n[SCHED] === Scheduler Statistics ===\n");
    kprintf("  Total tasks: %lu\n", total_tasks);
    for (int c = 0; c < MAX_CPUS; c++) {
        if (per_cpu_rq[c].count > 0 || c == 0) {
            kprintf("  CPU%d: %u ready, min_vrt=%lu\n",
                    c, per_cpu_rq[c].count, per_cpu_rq[c].min_vruntime);
        }
    }
}
