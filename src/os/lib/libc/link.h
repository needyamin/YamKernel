#ifndef _LIBC_LINK_H
#define _LIBC_LINK_H

#include <nexus/types.h>

struct dl_phdr_info {
    unsigned long dlpi_addr;
    const char *dlpi_name;
};

struct link_map {
    unsigned long l_addr;
    char *l_name;
    void *l_ld;
    struct link_map *l_next;
    struct link_map *l_prev;
};

static inline int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, usize, void *), void *data) {
    (void)callback;
    (void)data;
    return 0;
}

#endif
