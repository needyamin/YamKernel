#ifndef _LIBC_SYS_RANDOM_H
#define _LIBC_SYS_RANDOM_H

#include <nexus/types.h>

#define GRND_NONBLOCK 0x0001
#define GRND_RANDOM 0x0002

static inline isize getrandom(void *buf, usize buflen, unsigned int flags) {
    (void)flags;
    u8 *out = (u8 *)buf;
    u64 x = 0x9e3779b97f4a7c15UL;
    for (usize i = 0; i < buflen; i++) {
        x ^= x << 7;
        x ^= x >> 9;
        out[i] = (u8)(x + i);
    }
    return (isize)buflen;
}

#endif
