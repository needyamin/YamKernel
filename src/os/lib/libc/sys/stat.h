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
#define S_IFCHR 0020000
#define S_IFREG 0100000
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100

#define AT_FDCWD -100
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_EACCESS 0x200
#define AT_REMOVEDIR 0x200

#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)

int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int lstat(const char *path, struct stat *st);
static inline int fstatat(int dirfd, const char *path, struct stat *st, int flags) {
    yam_stat_t yst;
    int rc = (int)syscall4(SYS_FSTATAT, (u64)dirfd, (u64)path, (u64)&yst, (u64)flags);
    if (rc < 0 || !st) {
        errno = ENOENT;
        return -1;
    }
    st->st_dev = yst.dev;
    st->st_ino = yst.ino;
    st->st_mode = yst.mode;
    st->st_nlink = yst.nlink;
    st->st_uid = yst.uid;
    st->st_gid = yst.gid;
    st->st_rdev = yst.rdev;
    st->st_size = yst.size;
    st->st_blksize = yst.blksize;
    st->st_blocks = yst.blocks;
    st->st_atime = yst.atime;
    st->st_mtime = yst.mtime;
    st->st_ctime = yst.ctime;
    return 0;
}
static inline int faccessat(int dirfd, const char *path, int mode, int flags) {
    (void)dirfd;
    (void)flags;
    return access(path, mode);
}
static inline int fchmod(int fd, mode_t mode) { (void)fd; (void)mode; return 0; }
static inline int chmod(const char *path, mode_t mode) { (void)path; (void)mode; return 0; }
static inline int lchmod(const char *path, mode_t mode) { (void)path; (void)mode; return 0; }
static inline int fchmodat(int dirfd, const char *path, mode_t mode, int flags) {
    (void)dirfd; (void)path; (void)mode; (void)flags; return 0;
}
#endif
