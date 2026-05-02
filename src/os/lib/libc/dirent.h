#ifndef _LIBC_DIRENT_H
#define _LIBC_DIRENT_H

#include <nexus/types.h>
#include "sys/types.h"

typedef struct {
    int fd;
} DIR;

struct dirent {
    ino_t d_ino;
    unsigned char d_type;
    char d_name[256];
};

typedef struct dirent direct;

#define DT_UNKNOWN 0
#define DT_REG 8
#define DT_DIR 4

static inline DIR *opendir(const char *name) { (void)name; return (DIR *)0; }
static inline DIR *fdopendir(int fd) { (void)fd; return (DIR *)0; }
static inline struct dirent *readdir(DIR *dirp) { (void)dirp; return (struct dirent *)0; }
static inline void rewinddir(DIR *dirp) { (void)dirp; }
static inline int closedir(DIR *dirp) { (void)dirp; return 0; }

#endif
