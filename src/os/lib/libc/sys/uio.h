#ifndef _LIBC_SYS_UIO_H
#define _LIBC_SYS_UIO_H

#include <nexus/types.h>
#include "../sys/types.h"
#include "../errno.h"

struct iovec {
    void *iov_base;
    size_t iov_len;
};

static inline ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    (void)fd;
    (void)iov;
    (void)iovcnt;
    errno = ENOSYS;
    return -1;
}

static inline ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    (void)fd;
    (void)iov;
    (void)iovcnt;
    errno = ENOSYS;
    return -1;
}

static inline ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset) {
    (void)offset;
    return readv(fd, iov, iovcnt);
}

static inline ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset) {
    (void)offset;
    return writev(fd, iov, iovcnt);
}

static inline ssize_t process_vm_readv(pid_t pid, const struct iovec *local_iov,
        unsigned long liovcnt, const struct iovec *remote_iov,
        unsigned long riovcnt, unsigned long flags) {
    (void)pid; (void)local_iov; (void)liovcnt; (void)remote_iov; (void)riovcnt; (void)flags;
    errno = ENOSYS;
    return -1;
}

static inline ssize_t process_vm_writev(pid_t pid, const struct iovec *local_iov,
        unsigned long liovcnt, const struct iovec *remote_iov,
        unsigned long riovcnt, unsigned long flags) {
    (void)pid; (void)local_iov; (void)liovcnt; (void)remote_iov; (void)riovcnt; (void)flags;
    errno = ENOSYS;
    return -1;
}

#endif
