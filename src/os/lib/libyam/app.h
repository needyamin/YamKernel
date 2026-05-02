#ifndef _LIBYAM_APP_H
#define _LIBYAM_APP_H

#include "syscall.h"
#include "kernel/api/syscall.h"

#define YAM_APP_MANIFEST(app_name, app_kind, app_perms, app_desc) \
    (yam_app_manifest_t){ \
        .abi_version = YAM_ABI_VERSION, \
        .app_type = (app_kind), \
        .permissions = (app_perms), \
        .flags = 0, \
        .name = (app_name), \
        .publisher = "YamOS", \
        .version = "0.1.0", \
        .description = (app_desc) \
    }

static inline i64 yam_os_info(yam_os_info_t *out) {
    return (i64)syscall1(SYS_OS_INFO, (u64)out);
}

static inline i64 yam_app_register(const yam_app_manifest_t *manifest) {
    return (i64)syscall1(SYS_APP_REGISTER, (u64)manifest);
}

static inline i64 yam_app_query(u32 index, yam_app_record_t *out) {
    return (i64)syscall2(SYS_APP_QUERY, index, (u64)out);
}

static inline u64 yam_pid(void) {
    return syscall0(SYS_GETPID);
}

static inline void yam_sleep_ms(u64 ms) {
    syscall1(SYS_SLEEPMS, ms);
}

#endif
