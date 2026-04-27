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
#define SYS_OPEN    6
#define SYS_CLOSE   7
#define SYS_READ    8
#define SYS_WRITE_FD 9
#define SYS_MMAP    10
#define SYS_MUNMAP  11
#define SYS_PIPE    12
#define SYS_POLL    13

/* Wayland / GUI Syscalls */
#define SYS_WL_CREATE_SURFACE 20
#define SYS_WL_MAP_BUFFER     21
#define SYS_WL_COMMIT         22
#define SYS_WL_POLL_EVENT     23

/* Privileged Driver Syscalls (OS Level) */
#define SYS_IOPORT_READ       30
#define SYS_IOPORT_WRITE      31
#define SYS_IRQ_SUBSCRIBE     32
#define SYS_MAP_MMIO          33
#define SYS_PCI_CONFIG_READ   34

#define SYS_MAX     64

void syscall_init(void);

/* Used by the assembly stub */
i64 syscall_dispatch(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5);

#endif
