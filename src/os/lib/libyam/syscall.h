#ifndef _YAM_SYSCALL_H
#define _YAM_SYSCALL_H

#include <nexus/types.h>

#define SYS_WRITE             1
#define SYS_EXIT              2
#define SYS_GETPID            3
#define SYS_YIELD             4
#define SYS_SLEEPMS           5
#define SYS_OPEN              6
#define SYS_CLOSE             7
#define SYS_READ              8
#define SYS_WRITE_FD          9
#define SYS_MMAP              10
#define SYS_MUNMAP            11
#define SYS_PIPE              12
#define SYS_POLL              13
#define SYS_FORK              14
#define SYS_WAITPID           15
#define SYS_BRK               16
#define SYS_MPROTECT          17
#define SYS_CLOCK_GETTIME     18
#define SYS_GETRUSAGE         19

#define SYS_WL_CREATE_SURFACE 20
#define SYS_WL_MAP_BUFFER     21
#define SYS_WL_COMMIT         22
#define SYS_WL_POLL_EVENT     23
#define SYS_LSEEK             24
#define SYS_MKDIR             25
#define SYS_UNLINK            26
#define SYS_READDIR           27
#define SYS_CHDIR             28
#define SYS_GETCWD            29

#define SYS_SCHED_SETAFFINITY 40
#define SYS_SCHED_GETAFFINITY 41
#define SYS_FUTEX             42
#define SYS_KILL              43
#define SYS_GETPPID           44
#define SYS_DUP               45
#define SYS_DUP2              46
#define SYS_SCHED_INFO        47

#define SYS_CHANNEL_SEND      60
#define SYS_CHANNEL_RECV      61
#define SYS_CHANNEL_LOOKUP    62
#define SYS_CLIPBOARD_SET     63
#define SYS_CLIPBOARD_GET     64
#define SYS_INSTALLER_STATUS  65
#define SYS_INSTALLER_REQUEST 66
#define SYS_OS_INFO           67
#define SYS_APP_REGISTER      68
#define SYS_APP_QUERY         69
#define SYS_SOCKET            70
#define SYS_BIND              71
#define SYS_CONNECT           72
#define SYS_LISTEN            73
#define SYS_ACCEPT            74
#define SYS_SENDTO            75
#define SYS_RECVFROM          76

static inline u64 syscall0(u64 num) {
    u64 ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num) : "rcx", "r11", "memory");
    return ret;
}

static inline u64 syscall1(u64 num, u64 arg1) {
    u64 ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1) : "rcx", "r11", "memory");
    return ret;
}

static inline u64 syscall2(u64 num, u64 arg1, u64 arg2) {
    u64 ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2) : "rcx", "r11", "memory");
    return ret;
}

static inline u64 syscall3(u64 num, u64 arg1, u64 arg2, u64 arg3) {
    u64 ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3) : "rcx", "r11", "memory");
    return ret;
}

static inline u64 syscall4(u64 num, u64 arg1, u64 arg2, u64 arg3, u64 arg4) {
    u64 ret;
    register u64 r10 __asm__("r10") = arg4;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10) : "rcx", "r11", "memory");
    return ret;
}

static inline u64 syscall5(u64 num, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {
    u64 ret;
    register u64 r10 __asm__("r10") = arg4;
    register u64 r8  __asm__("r8")  = arg5;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8) : "rcx", "r11", "memory");
    return ret;
}

static inline u64 syscall6(u64 num, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6) {
    u64 ret;
    register u64 r10 __asm__("r10") = arg4;
    register u64 r8  __asm__("r8")  = arg5;
    register u64 r9  __asm__("r9")  = arg6;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return ret;
}

#endif
