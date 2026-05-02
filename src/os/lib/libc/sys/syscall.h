#ifndef _LIBC_SYS_SYSCALL_H
#define _LIBC_SYS_SYSCALL_H

#include "../libyam/syscall.h"
#include <stdarg.h>

#define SYS_gettid SYS_GETPID

static inline long syscall(long number, ...) {
    va_list args;
    va_start(args, number);
    u64 a1 = va_arg(args, u64);
    u64 a2 = va_arg(args, u64);
    u64 a3 = va_arg(args, u64);
    va_end(args);
    return (long)syscall3((u64)number, a1, a2, a3);
}

#endif
