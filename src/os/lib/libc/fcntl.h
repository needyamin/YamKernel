#ifndef _LIBC_FCNTL_H
#define _LIBC_FCNTL_H

#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_CREAT    0x0040
#define O_EXCL     0x0080
#define O_TRUNC    0x0200
#define O_APPEND   0x0400
#define O_NONBLOCK 0x0800
#define O_NOFOLLOW 0x1000

#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1

static inline int fcntl(int fd, int cmd, ...) {
    (void)fd;
    if (cmd == F_GETFD || cmd == F_GETFL) return 0;
    if (cmd == F_SETFD || cmd == F_SETFL) return 0;
    return -1;
}

#endif
