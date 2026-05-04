#include "unistd.h"
#include "string.h"
#include "errno.h"
#include "sys/stat.h"

char *getcwd(char *buf, size_t size) {
    if (!buf || size < 2) {
        errno = EINVAL;
        return NULL;
    }
    long rc = (long)syscall2(SYS_GETCWD, (u64)buf, (u64)size);
    if (rc < 0) {
        errno = ERANGE;
        return NULL;
    }
    return buf;
}

int chdir(const char *path) {
    if (!path || !*path) {
        errno = ENOENT;
        return -1;
    }
    int rc = (int)syscall1(SYS_CHDIR, (u64)path);
    if (rc < 0) errno = ENOENT;
    return rc;
}

int access(const char *path, int mode) {
    (void)mode;
    if (!path || !*path) {
        errno = ENOENT;
        return -1;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        errno = ENOENT;
        return -1;
    }
    close(fd);
    return 0;
}

int isatty(int fd) {
    return fd >= 0 && fd <= 2;
}

static void copy_yam_stat(struct stat *st, const yam_stat_t *yst) {
    memset(st, 0, sizeof(*st));
    st->st_dev = yst->dev;
    st->st_ino = yst->ino;
    st->st_mode = yst->mode;
    st->st_nlink = yst->nlink;
    st->st_uid = yst->uid;
    st->st_gid = yst->gid;
    st->st_rdev = yst->rdev;
    st->st_size = yst->size;
    st->st_blksize = yst->blksize;
    st->st_blocks = yst->blocks;
    st->st_atime = yst->atime;
    st->st_mtime = yst->mtime;
    st->st_ctime = yst->ctime;
}

int stat(const char *path, struct stat *st) {
    if (!path || !st) {
        errno = EINVAL;
        return -1;
    }
    yam_stat_t yst;
    int rc = (int)syscall2(SYS_STAT, (u64)path, (u64)&yst);
    if (rc < 0) {
        errno = ENOENT;
        return -1;
    }
    copy_yam_stat(st, &yst);
    return 0;
}

int lstat(const char *path, struct stat *st) {
    return stat(path, st);
}

int fstat(int fd, struct stat *st) {
    if (!st || fd < 0) {
        errno = EBADF;
        return -1;
    }
    yam_stat_t yst;
    int rc = (int)syscall2(SYS_FSTAT, (u64)fd, (u64)&yst);
    if (rc < 0) {
        errno = EBADF;
        return -1;
    }
    copy_yam_stat(st, &yst);
    return 0;
}
