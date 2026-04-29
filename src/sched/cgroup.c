/* YamKernel — cgroup v2 implementation v0.3.0 */
#include "cgroup.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

static cgroup_t cgroups[CGROUP_MAX];
static u32 next_cg_id = 1;

void cgroup_init(void) {
    memset(cgroups, 0, sizeof(cgroups));
    /* Create root cgroup */
    cgroups[0].id = 0;
    cgroups[0].active = true;
    cgroups[0].cpu_shares = 1024;
    cgroups[0].cpu_period_us = 100000;
    const char *n = "root";
    for (int i = 0; n[i] && i < CGROUP_NAME_LEN-1; i++) cgroups[0].name[i] = n[i];
    kprintf_color(0xFF00FF88, "[CGROUP] v2 initialized (max %d groups)\n", CGROUP_MAX);
}

cgroup_t *cgroup_create(const char *name, cgroup_t *parent) {
    for (int i = 1; i < CGROUP_MAX; i++) {
        if (!cgroups[i].active) {
            cgroups[i].id = next_cg_id++;
            cgroups[i].parent = parent ? parent : &cgroups[0];
            cgroups[i].active = true;
            cgroups[i].cpu_shares = 1024;
            cgroups[i].cpu_period_us = 100000;
            for (int j = 0; name[j] && j < CGROUP_NAME_LEN-1; j++)
                cgroups[i].name[j] = name[j];
            return &cgroups[i];
        }
    }
    return NULL;
}

void cgroup_destroy(cgroup_t *cg) {
    if (cg && cg->id != 0) {
        memset(cg, 0, sizeof(*cg));
    }
}

bool cgroup_charge_memory(cgroup_t *cg, u64 bytes) {
    if (!cg) return true;
    if (cg->mem_limit > 0 && cg->mem_usage + bytes > cg->mem_limit) return false;
    cg->mem_usage += bytes;
    return true;
}

void cgroup_uncharge_memory(cgroup_t *cg, u64 bytes) {
    if (!cg) return;
    if (cg->mem_usage >= bytes) cg->mem_usage -= bytes;
    else cg->mem_usage = 0;
}

bool cgroup_can_fork(cgroup_t *cg) {
    if (!cg) return true;
    if (cg->pid_max > 0 && cg->pid_current >= cg->pid_max) return false;
    return true;
}

void cgroup_print_stats(void) {
    kprintf_color(0xFF00DDFF, "\n[CGROUP] === Resource Groups ===\n");
    for (int i = 0; i < CGROUP_MAX; i++) {
        if (!cgroups[i].active) continue;
        kprintf("  [%u] '%s' cpu_shares=%u mem=%lu/%lu pids=%u/%u\n",
                cgroups[i].id, cgroups[i].name, cgroups[i].cpu_shares,
                cgroups[i].mem_usage, cgroups[i].mem_limit,
                cgroups[i].pid_current, cgroups[i].pid_max);
    }
}
