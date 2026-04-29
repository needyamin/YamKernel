/* YamKernel — CPU Power Management v0.3.0 */
#ifndef _CPU_POWER_H
#define _CPU_POWER_H
#include <nexus/types.h>

typedef enum { CSTATE_C0, CSTATE_C1, CSTATE_C1E, CSTATE_C3 } cstate_t;

typedef struct {
    cstate_t current_state;
    u64      c1_residency;
    u64      c1e_residency;
    u64      c3_residency;
    u64      total_idle_ticks;
    bool     mwait_supported;
} power_state_t;

void power_init(void);
void power_idle(void);                /* Called from idle loop — picks best C-state */
const power_state_t *power_get_state(void);
void power_print_stats(void);
#endif
