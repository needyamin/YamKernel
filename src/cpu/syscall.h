/* YamKernel — SYSCALL/SYSRET fast syscall path */
#ifndef _CPU_SYSCALL_H
#define _CPU_SYSCALL_H

#include <nexus/types.h>

/* Syscall numbers (kept tiny for now — extend freely) */
#define SYS_WRITE   1
#define SYS_EXIT    2
#define SYS_GETPID  3
#define SYS_YIELD   4
#define SYS_SLEEPMS 5
#define SYS_MAX     16

void syscall_init(void);

/* Used by the assembly stub */
i64 syscall_dispatch(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5);

#endif
