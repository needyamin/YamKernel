#ifndef _OS_SERVICES_APP_REGISTRY_H
#define _OS_SERVICES_APP_REGISTRY_H

#include <nexus/types.h>
#include "kernel/api/syscall.h"
#include "sched/sched.h"

#define YAM_APP_REGISTRY_MAX 64

void app_registry_init(void);
i64 app_registry_register(task_t *task, const yam_app_manifest_t *manifest);
i64 app_registry_query(u32 index, yam_app_record_t *out);
u32 app_registry_count(void);

#endif
