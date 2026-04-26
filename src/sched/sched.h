/* YamKernel — Preemptive MLFQ scheduler (kernel threads) */
#ifndef _SCHED_SCHED_H
#define _SCHED_SCHED_H

#include <nexus/types.h>

#define SCHED_PRIO_LEVELS 4
#define SCHED_STACK_SIZE  (16 * 1024)

typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_DEAD } task_state_t;

struct file;
struct vma;

typedef struct task {
    u64           rsp;          /* saved kernel stack ptr (must be first) */
    u64           id;
    char          name[24];
    task_state_t  state;
    u8            prio;         /* 0..SCHED_PRIO_LEVELS-1 (0=highest) */
    u32           cpu;
    u64           ticks;
    u64           vruntime;     /* CFS virtual runtime */
    u32           weight;       /* derived from prio (higher = more CPU) */
    u64           wakeup_tick;  /* for task_sleep_ms */
    void        (*entry)(void *);
    void         *arg;
    u8           *stack;        /* per-task kernel stack base */
    u8           *fpu_state;    /* dynamically allocated FXSAVE/XSAVE buffer */
    struct file  *fd_table[128];/* POSIX File Descriptor table */
    struct vma   *vma_head;     /* Virtual Memory Area list for mmap */
    u64          *pml4;         /* User page table (NULL for kernel threads) */
    struct task  *next;         /* runqueue link */
    struct task  *wait_next;    /* wait-queue link */
} task_t;

task_t *sched_current(void);
void    sched_unblock(task_t *t);    /* used by wait queues / wake-up */
void    sched_sleep_until(u64 deadline_tick);   /* block until tick reached */

void    sched_init(void);
task_t *sched_spawn(const char *name, void (*entry)(void *), void *arg, u8 prio);
void    sched_yield(void);
void    sched_tick(void);              /* internal */
void    sched_install_timer(void);     /* register APIC timer ISR on vec 0x20 */
void    sched_enable(void);            /* turn on preemption */
void    sched_start(void) NORETURN;    /* never returns; runs tasks forever */

void    context_switch(u64 *old_rsp, u64 new_rsp);   /* in context.asm */

#endif
