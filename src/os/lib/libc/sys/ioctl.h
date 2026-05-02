#ifndef _LIBC_SYS_IOCTL_H
#define _LIBC_SYS_IOCTL_H

#include <nexus/types.h>
#include "../errno.h"

#define FIOCLEX 0x5451
#define FIONCLEX 0x5450
#define FIONBIO 0x5421

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

static inline int ioctl(int fd, unsigned long request, ...) {
    (void)fd;
    (void)request;
    errno = ENOSYS;
    return -1;
}

#endif
