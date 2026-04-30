/* YamKernel — SYSCALL/SYSRET fast syscall path v0.3.0 */
#ifndef _CPU_SYSCALL_H
#define _CPU_SYSCALL_H

#include <nexus/types.h>

/* Syscall numbers */
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

/* Process management (new in v0.3.0) */
#define SYS_FORK          14
#define SYS_WAITPID       15
#define SYS_BRK           16
#define SYS_MPROTECT      17
#define SYS_CLOCK_GETTIME 18
#define SYS_GETRUSAGE     19

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

/* Scheduling (new in v0.3.0) */
#define SYS_SCHED_SETAFFINITY 40
#define SYS_SCHED_GETAFFINITY 41
#define SYS_FUTEX             42
#define SYS_KILL              43
#define SYS_GETPPID           44
#define SYS_DUP               45
#define SYS_DUP2              46
#define SYS_SCHED_INFO        47

/* AI/ML Acceleration (new in v0.3.0) */
#define SYS_AI_DEVICE_QUERY   50
#define SYS_AI_TENSOR_ALLOC   51
#define SYS_AI_TENSOR_FREE    52
#define SYS_AI_SUBMIT_JOB     53
#define SYS_AI_WAIT_JOB       54
#define SYS_AI_MAP_TENSOR     55

/* YamGraph IPC (new in v0.3.0) */
#define SYS_CHANNEL_SEND      60
#define SYS_CHANNEL_RECV      61
#define SYS_CHANNEL_LOOKUP    62

/* Touchscreen (new in v0.3.0) */
#define SYS_TOUCH_CALIBRATE   56
#define SYS_TOUCH_GET_SLOTS   57
#define SYS_GESTURE_CONFIG    58

#define SYS_MAX     64

typedef struct {
    u64 pid;
    u64 ppid;
    u64 ticks;
    u64 utime;
    u64 stime;
    u64 voluntary_switches;
    u64 involuntary_switches;
    u64 start_tick;
    u64 rss_pages;
    i32 nice;
    u32 cpu;
    u64 affinity;
} yam_rusage_t;

typedef struct {
    u32 detected_cpus;
    u32 schedulable_cpus;
    u32 total_tasks;
    u32 ready_tasks;
    u32 blocked_tasks;
    u32 running_tasks;
    u64 total_switches;
    u64 ticks;
    u64 rq_load[8];
    u32 rq_ready[8];
} yam_sched_info_t;

void syscall_init(void);
i64 syscall_dispatch(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5);

#endif
