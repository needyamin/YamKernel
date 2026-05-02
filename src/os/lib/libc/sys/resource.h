#ifndef _LIBC_SYS_RESOURCE_H
#define _LIBC_SYS_RESOURCE_H

#include <sys/time.h>

#define RUSAGE_SELF 0
#define PRIO_PROCESS 0
#define RLIMIT_CORE 4
#define RLIM_INFINITY ((unsigned long)-1)

typedef unsigned long rlim_t;

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    long ru_maxrss;
    long ru_ixrss;
    long ru_idrss;
    long ru_isrss;
    long ru_minflt;
    long ru_majflt;
    long ru_nswap;
    long ru_inblock;
    long ru_oublock;
    long ru_msgsnd;
    long ru_msgrcv;
    long ru_nsignals;
    long ru_nvcsw;
    long ru_nivcsw;
};

static inline int getrusage(int who, struct rusage *usage) {
    (void)who;
    if (!usage) return -1;
    usage->ru_utime.tv_sec = 0;
    usage->ru_utime.tv_usec = 0;
    usage->ru_stime.tv_sec = 0;
    usage->ru_stime.tv_usec = 0;
    usage->ru_maxrss = 0;
    usage->ru_ixrss = 0;
    usage->ru_idrss = 0;
    usage->ru_isrss = 0;
    usage->ru_minflt = 0;
    usage->ru_majflt = 0;
    usage->ru_nswap = 0;
    usage->ru_inblock = 0;
    usage->ru_oublock = 0;
    usage->ru_msgsnd = 0;
    usage->ru_msgrcv = 0;
    usage->ru_nsignals = 0;
    usage->ru_nvcsw = 0;
    usage->ru_nivcsw = 0;
    return 0;
}

static inline int getrlimit(int resource, struct rlimit *rlim) {
    (void)resource;
    if (!rlim) return -1;
    rlim->rlim_cur = 0;
    rlim->rlim_max = RLIM_INFINITY;
    return 0;
}

static inline int setrlimit(int resource, const struct rlimit *rlim) {
    (void)resource;
    (void)rlim;
    return 0;
}

static inline int getpriority(int which, int who) {
    (void)which;
    (void)who;
    return 0;
}

static inline int setpriority(int which, int who, int prio) {
    (void)which;
    (void)who;
    (void)prio;
    return 0;
}

#endif
