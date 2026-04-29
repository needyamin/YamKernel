/* YamKernel — cgroup v2 (basic CPU+Memory limits) v0.3.0 */
#ifndef _SCHED_CGROUP_H
#define _SCHED_CGROUP_H
#include <nexus/types.h>

#define CGROUP_MAX     32
#define CGROUP_NAME_LEN 32

typedef struct cgroup {
    u32           id;
    char          name[CGROUP_NAME_LEN];
    struct cgroup *parent;

    /* CPU limits */
    u32           cpu_shares;     /* Relative CPU weight (default: 1024) */
    u64           cpu_quota_us;   /* Max CPU time per period (0 = unlimited) */
    u64           cpu_period_us;  /* Period length (default: 100000 = 100ms) */
    u64           cpu_used_us;    /* CPU time used in current period */

    /* Memory limits */
    u64           mem_limit;      /* Max memory bytes (0 = unlimited) */
    u64           mem_usage;      /* Current memory usage */

    /* PID limits */
    u32           pid_max;        /* Max number of tasks (0 = unlimited) */
    u32           pid_current;    /* Current task count */

    bool          active;
} cgroup_t;

void     cgroup_init(void);
cgroup_t *cgroup_create(const char *name, cgroup_t *parent);
void     cgroup_destroy(cgroup_t *cg);
bool     cgroup_charge_memory(cgroup_t *cg, u64 bytes);
void     cgroup_uncharge_memory(cgroup_t *cg, u64 bytes);
bool     cgroup_can_fork(cgroup_t *cg);
void     cgroup_print_stats(void);

#endif
