#ifndef _LIBC_SYS_SYSMACROS_H
#define _LIBC_SYS_SYSMACROS_H

#include <nexus/types.h>
#include "types.h"

static inline unsigned int major(dev_t dev) { return (unsigned int)((dev >> 8) & 0xfff); }
static inline unsigned int minor(dev_t dev) { return (unsigned int)((dev & 0xff) | ((dev >> 12) & 0xfff00)); }
static inline dev_t makedev(unsigned int maj, unsigned int min) {
    return (dev_t)(((maj & 0xfff) << 8) | (min & 0xff) | ((min & 0xfff00) << 12));
}

#endif
