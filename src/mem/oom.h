/* YamKernel — OOM Killer v0.3.0 */
#ifndef _MEM_OOM_H
#define _MEM_OOM_H
#include <nexus/types.h>

void oom_init(void);
void oom_check(void);              /* Called when memory pressure detected */
i64  oom_score(void *task);        /* Score a task (higher = more likely to kill) */
void oom_kill_worst(void);         /* Kill the highest-scoring task */
#endif
