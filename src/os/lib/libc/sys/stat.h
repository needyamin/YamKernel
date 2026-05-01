#ifndef _LIBC_SYS_STAT_H
#define _LIBC_SYS_STAT_H

#include <nexus/types.h>
#include "../unistd.h"

typedef unsigned int mode_t;
typedef unsigned long ino_t;
typedef unsigned long dev_t;
typedef long time_t;

struct stat {
    dev_t st_dev;
    ino_t st_ino;
    mode_t st_mode;
    u32 st_nlink;
    u32 st_uid;
    u32 st_gid;
    dev_t st_rdev;
    off_t st_size;
    long st_blksize;
    long st_blocks;
    time_t st_atime;
    time_t st_mtime;
    time_t st_ctime;
};

#define S_IFMT  0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100

#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)

int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int lstat(const char *path, struct stat *st);
int mkdir(const char *path, mode_t mode);

#endif
