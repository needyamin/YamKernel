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

static int fill_stat_for_path(const char *path, struct stat *st) {
    if (!path || !st) {
        errno = EINVAL;
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | S_IRUSR | S_IWUSR;
    st->st_blksize = 512;
    st->st_nlink = 1;
    if (strcmp(path, "/") == 0 || strcmp(path, "/usr") == 0 ||
        strcmp(path, "/usr/lib") == 0 || strcmp(path, "/tmp") == 0) {
        st->st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR;
    }
    return 0;
}

int stat(const char *path, struct stat *st) {
    return fill_stat_for_path(path, st);
}

int lstat(const char *path, struct stat *st) {
    return fill_stat_for_path(path, st);
}

int fstat(int fd, struct stat *st) {
    if (!st || fd < 0) {
        errno = EBADF;
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = (fd <= 2) ? (S_IFREG | S_IRUSR | S_IWUSR) : (S_IFREG | S_IRUSR);
    st->st_blksize = 512;
    st->st_nlink = 1;
    return 0;
}
