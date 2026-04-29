/* YamKernel — Multi-Queue CFS Scheduler v0.3.0
 * O(log n) sorted pick, per-CPU queues, fork, nice, affinity, cgroups */
#ifndef _SCHED_SCHED_H
#define _SCHED_SCHED_H

#include <nexus/types.h>
#include "../lib/spinlock.h"

#define SCHED_PRIO_LEVELS 4
#define SCHED_STACK_SIZE  (16 * 1024)
#define MAX_CPUS          8
#define MAX_CHILDREN      32
#define MAX_NICE          19
#define MIN_NICE          (-20)
#define DEFAULT_NICE      0

/* Task states */
typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_ZOMBIE,     /* Exited, waiting for parent to reap */
    TASK_STOPPED,    /* Stopped by signal */
    TASK_DEAD
} task_state_t;

/* Signal definitions */
#define SIGKILL  9
#define SIGSTOP  17
#define SIGCONT  18
#define SIGTERM  15
#define SIGCHLD  20
#define SIG_MAX  32

/* AI scheduling hints */
#define AI_HINT_NONE      0
#define AI_HINT_BATCH     1   /* Throughput-optimized */
#define AI_HINT_REALTIME  2   /* Latency-sensitive inference */
#define AI_HINT_TRAINING  3   /* Long-running, can be throttled */

struct file;
struct vma;
struct cgroup;

typedef struct task {
    u64           rsp;          /* saved kernel stack ptr (must be first) */
    u64           id;
    char          name[24];
    task_state_t  state;
    u8            prio;         /* 0..SCHED_PRIO_LEVELS-1 (0=highest) */
    i8            nice;         /* -20..+19 (lower = higher priority) */
    u32           cpu;          /* Current CPU */
    u64           cpu_affinity; /* Bitmask of allowed CPUs */
    u64           ticks;
    u64           vruntime;     /* CFS virtual runtime */
    u32           weight;       /* Derived from nice/prio */
    u64           wakeup_tick;

    /* Task accounting */
    u64           utime;        /* User-mode ticks */
    u64           stime;        /* System-mode ticks */
    u64           vol_switches; /* Voluntary context switches */
    u64           invol_switches; /* Involuntary context switches */
    u64           start_tick;   /* When task was created */
    u64           rss_pages;    /* Resident set size (pages) */

    /* Process hierarchy */
    struct task  *parent;
    struct task  *children[MAX_CHILDREN];
    u32           child_count;
    i32           exit_code;
    u32           signal_pending;  /* Bitmask of pending signals */

    /* AI scheduling */
    u8            ai_hint;

    /* Entry */
    void        (*entry)(void *);
    void         *arg;
    u8           *stack;
    u8           *fpu_state;
    struct file  *fd_table[128];
    struct vma   *vma_head;
    u64          *pml4;

    /* Cgroup */
    struct cgroup *cgroup;
    
    /* YamGraph */
    u32           graph_node;

    /* Links */
    struct task  *next;
    struct task  *wait_next;
} task_t;

/* Per-CPU run queue */
typedef struct {
    task_t  *tasks[256];   /* Sorted by vruntime (min-heap style) */
    u32      count;
    u64      min_vruntime;
    spinlock_t lock;
} runqueue_t;

/* Public API */
task_t *sched_current(void);
void    sched_unblock(task_t *t);
void    sched_sleep_until(u64 deadline_tick);

void    sched_init(void);
task_t *sched_spawn(const char *name, void (*entry)(void *), void *arg, u8 prio);
void    sched_yield(void);
void    sched_tick(void);
void    sched_install_timer(void);
void    sched_enable(void);
void    sched_start(void) NORETURN;

/* Process management */
i64     sys_fork(void);
i64     sys_waitpid(i64 pid, i32 *status, u32 options);
void    task_exit(void);
i64     sys_kill(u64 pid, u32 sig);
void    sched_kill_task(u64 id);

/* Nice / Affinity */
void    sched_set_nice(task_t *t, i8 nice);
void    sched_set_affinity(task_t *t, u64 mask);
u64     sched_get_affinity(task_t *t);

/* AI scheduling hints */
void    sched_set_ai_hint(task_t *t, u8 hint);

/* Statistics */
u64     sched_task_count(void);
void    sched_print_stats(void);

/* ASM */
void    context_switch(u64 *old_rsp, u64 new_rsp);

#endif
