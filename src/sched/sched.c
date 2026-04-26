/* YamKernel — CFS-lite scheduler.
 *  - Single ready list, pick task with lowest vruntime.
 *  - Per-task kernel stack -> updates TSS.rsp0 + percpu.kernel_rsp on switch.
 *  - Sleeping list for timed wake-ups (driven by APIC timer tick). */
#include "sched.h"
#include "wait.h"
#include "../cpu/percpu.h"
#include "../cpu/apic.h"
#include "../cpu/idt.h"
#include "../cpu/gdt.h"
#include "../mem/heap.h"
#include "../mem/slab.h"
#include "../mem/vmm.h"
#include "../lib/spinlock.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../cpu/cpuid.h"
static kmem_cache_t *task_cache;

extern void task_trampoline(void);

/* prio (0=hi..3=lo) -> weight (higher = more CPU share) */
static const u32 prio_weight[SCHED_PRIO_LEVELS] = { 8, 4, 2, 1 };
#define NICE_BASE 1024U

static task_t   *ready_head;          /* unsorted; pick min vruntime in O(n) */
static task_t   *sleep_head;          /* tasks waiting on wakeup_tick */
static spinlock_t rq_lock = SPINLOCK_INIT;
static u64       next_id = 1;
static volatile u32 sched_enabled = 0;

/* ---- runqueue helpers (lock held) ---- */
static void rq_add(task_t *t) {
    t->state = TASK_READY;
    t->next  = ready_head;
    ready_head = t;
}
static task_t *rq_pick(void) {
    task_t *best = NULL, *prev_best = NULL, *prev = NULL;
    for (task_t *p = ready_head; p; prev = p, p = p->next) {
        if (!best || p->vruntime < best->vruntime) {
            best = p; prev_best = prev;
        }
    }
    if (!best) return NULL;
    if (prev_best) prev_best->next = best->next;
    else           ready_head      = best->next;
    best->next = NULL;
    return best;
}

/* ---- public API ---- */

task_t *sched_current(void) { return this_cpu()->current; }

task_t *sched_spawn(const char *name, void (*entry)(void *), void *arg, u8 prio) {
    if (!task_cache)
        task_cache = kmem_cache_create("task_t", sizeof(task_t), 16);
    task_t *t = (task_t *)kmem_cache_alloc(task_cache);
    if (!t) return NULL;
    t->id = next_id++;
    t->prio = (prio < SCHED_PRIO_LEVELS) ? prio : SCHED_PRIO_LEVELS - 1;
    t->weight = prio_weight[t->prio];
    t->entry = entry; t->arg = arg;
    t->stack = (u8 *)kmalloc(SCHED_STACK_SIZE);
    if (!t->stack) { kfree(t); return NULL; }
    
    t->fpu_state = (u8 *)kmalloc(cpuid_get_info()->xsave_size);
    if (!t->fpu_state) { kfree(t->stack); kfree(t); return NULL; }
    /* Ensure buffer is initialized (avoids XSAVE faulting on garbage reserved bits) */
    memset(t->fpu_state, 0, cpuid_get_info()->xsave_size);

    for (u32 i = 0; i < sizeof(t->name) - 1 && name[i]; i++) t->name[i] = name[i];

    /* Stack frame matching context_switch resume + task_trampoline */
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
    *--sp = 0x002;  /* RFLAGS (IF=0; trampoline does sti) */
    t->rsp = (u64)sp;

    /* Inherit min vruntime so new tasks don't immediately monopolize */
    u64 f = spin_lock_irqsave(&rq_lock);
    u64 minv = (task_t *)0 == ready_head ? 0 : ready_head->vruntime;
    for (task_t *p = ready_head; p; p = p->next)
        if (p->vruntime < minv) minv = p->vruntime;
    t->vruntime = minv;
    rq_add(t);
    spin_unlock_irqrestore(&rq_lock, f);

    kprintf("[SCHED] spawn '%s' id=%lu prio=%u w=%u\n",
            t->name, t->id, t->prio, t->weight);
    return t;
}

void task_exit(void) {
    cli();
    task_t *cur = sched_current();
    cur->state = TASK_DEAD;
    /* In a real kernel we'd free the stack here, but for now we just stop scheduling it */
    sched_yield();
    /* Should never reach here */
    for(;;) hlt();
}

void sched_init(void) {
    task_t *boot = (task_t *)kmalloc(sizeof(task_t));
    memset(boot, 0, sizeof(*boot));
    boot->id     = 0;
    boot->prio   = SCHED_PRIO_LEVELS - 1;
    boot->weight = prio_weight[boot->prio];
    boot->state  = TASK_RUNNING;
    boot->name[0] = 'b'; boot->name[1] = 's'; boot->name[2] = 'p';
    
    boot->fpu_state = (u8 *)kmalloc(cpuid_get_info()->xsave_size);
    memset(boot->fpu_state, 0, cpuid_get_info()->xsave_size);

    /* Boot uses the existing Limine-supplied stack; stack=NULL means
     * "do not touch TSS.rsp0/percpu.kernel_rsp on switch from this task". */
    this_cpu()->current = boot;
    this_cpu()->idle    = boot;
    kprintf_color(0xFF00FF88, "[SCHED] init OK (CFS-lite, weights=%u/%u/%u/%u)\n",
                  prio_weight[0], prio_weight[1], prio_weight[2], prio_weight[3]);
}

/* FPU/SIMD Save/Restore helpers */
static inline u64 read_xcr0(void) {
    u32 lo, hi;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return ((u64)hi << 32) | lo;
}

static inline void fpu_save(u8 *state) {
    if (!state) return;
    if (cpuid_get_info()->has_xsave) {
        u64 xcr0 = read_xcr0();
        __asm__ volatile("xsave64 %0" : "=m"(*state) : "a"((u32)xcr0), "d"((u32)(xcr0 >> 32)) : "memory");
    } else if (cpuid_get_info()->has_fxsr) {
        __asm__ volatile("fxsave64 %0" : "=m"(*state) : : "memory");
    }
}

static inline void fpu_restore(u8 *state) {
    if (!state) return;
    if (cpuid_get_info()->has_xsave) {
        u64 xcr0 = read_xcr0();
        __asm__ volatile("xrstor64 %0" : : "m"(*state), "a"((u32)xcr0), "d"((u32)(xcr0 >> 32)) : "memory");
    } else if (cpuid_get_info()->has_fxsr) {
        __asm__ volatile("fxrstor64 %0" : : "m"(*state) : "memory");
    }
}

/* Switch helper — also rebinds per-CPU kernel stack pointers */
static void switch_to(task_t *next) {
    percpu_t *pc = this_cpu();
    task_t   *cur = pc->current;
    if (next->stack) {
        u64 ktop = (u64)(next->stack + SCHED_STACK_SIZE);
        gdt_set_kernel_stack(pc->cpu_id, ktop);     /* TSS.rsp0 — IRQs from ring 3 */
        pc->kernel_rsp = ktop;          /* SYSCALL entry stack         */
    }
    
    /* Save current task's FPU state */
    fpu_save(cur->fpu_state);
    
    next->state = TASK_RUNNING;
    pc->current = next;
    
    /* Restore next task's FPU state */
    fpu_restore(next->fpu_state);
    
    /* Switch address space if necessary */
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
    spin_lock(&rq_lock);
    task_t *cur = this_cpu()->current;
    task_t *next = rq_pick();
    if (!next) { spin_unlock(&rq_lock); sti(); return; }
    if (cur && cur->state == TASK_RUNNING) rq_add(cur);
    spin_unlock(&rq_lock);
    switch_to(next);
    sti();   /* re-enable IF after returning from context_switch */
}

void sched_unblock(task_t *t) {
    u64 f = spin_lock_irqsave(&rq_lock);
    if (t->state == TASK_BLOCKED) rq_add(t);
    spin_unlock_irqrestore(&rq_lock, f);
}

/* Charge runtime + scan sleepers. Called from APIC timer ISR. */
void sched_tick(void) {
    percpu_t *pc = this_cpu();
    pc->ticks++;
    task_t *cur = pc->current;
    if (cur) {
        cur->ticks++;
        cur->vruntime += NICE_BASE / (cur->weight ? cur->weight : 1);
    }
    /* Wake any sleepers whose deadline hit */
    u64 now = pc->ticks;
    spin_lock(&rq_lock);
    task_t **pp = &sleep_head;
    while (*pp) {
        task_t *t = *pp;
        if (t->wakeup_tick <= now) {
            *pp = t->wait_next; t->wait_next = NULL;
            rq_add(t);
        } else pp = &t->wait_next;
    }
    spin_unlock(&rq_lock);
    sched_yield();
}

static void timer_isr(interrupt_frame_t *f) { (void)f; apic_eoi(); sched_tick(); }
void sched_install_timer(void) { idt_register_handler(0x20, timer_isr); }
void sched_enable(void)        { sched_enabled = 1; }

void sched_start(void) {
    sched_enabled = 1; sti();
    for (;;) { sched_yield(); hlt(); }
}

/* Used by wait.c — block current on a sleeper list with a deadline */
void sched_sleep_until(u64 deadline_tick) {
    cli();
    spin_lock(&rq_lock);
    task_t *cur = this_cpu()->current;
    cur->state = TASK_BLOCKED;
    cur->wakeup_tick = deadline_tick;
    cur->wait_next = sleep_head; sleep_head = cur;
    task_t *next = rq_pick();
    spin_unlock(&rq_lock);
    if (!next) { sti(); return; }   /* shouldn't happen if idle exists */
    switch_to(next);
    sti();
}
