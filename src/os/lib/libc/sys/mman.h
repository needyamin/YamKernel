#ifndef _LIBC_SYS_MMAN_H
#define _LIBC_SYS_MMAN_H

#include <nexus/types.h>
#include "../libyam/syscall.h"

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED    ((void *)-1)

#define MADV_NORMAL     0
#define MADV_RANDOM     1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED   3
#define MADV_DONTNEED   4
#define MADV_FREE       8

static inline void *mmap(void *addr, usize length, int prot, int flags, int fd, long offset) {
    return (void *)syscall6(SYS_MMAP, (u64)addr, (u64)length, (u64)prot, (u64)flags, (u64)fd, (u64)offset);
}

static inline int munmap(void *addr, usize length) {
    return (int)syscall2(SYS_MUNMAP, (u64)addr, (u64)length);
}

static inline int mprotect(void *addr, usize len, int prot) {
    return (int)syscall3(SYS_MPROTECT, (u64)addr, (u64)len, (u64)prot);
}

static inline int madvise(void *addr, usize length, int advice) {
    (void)addr;
    (void)length;
    (void)advice;
    return 0;
}

#endif
