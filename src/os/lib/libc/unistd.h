/* ============================================================================
 * YamKernel — POSIX unistd.h (User-Space libc)
 * ============================================================================ */
#pragma once
#include "../libyam/syscall.h"
#include "errno.h"

typedef long   ssize_t;
typedef unsigned long size_t;
typedef long   off_t;
typedef int    pid_t;
typedef unsigned int mode_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;

/* File descriptor constants */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* File access mode flags (for open) */
#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_CREAT    0x0040
#define O_EXCL     0x0080
#define O_TRUNC    0x0200
#define O_APPEND   0x0400
#define O_NONBLOCK 0x0800
#define O_NOFOLLOW 0x1000

/* lseek origins */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define _SC_PAGESIZE 1
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_OPEN_MAX 2
#define _SC_TTY_NAME_MAX 3

/* Syscall wrappers */
static inline ssize_t read(int fd, void *buf, size_t count) {
    return (ssize_t)syscall3(SYS_READ, (u64)fd, (u64)buf, (u64)count);
}
static inline ssize_t write(int fd, const void *buf, size_t count) {
    return (ssize_t)syscall3(SYS_WRITE_FD, (u64)fd, (u64)buf, (u64)count);
}
static inline int close(int fd) {
    return (int)syscall1(SYS_CLOSE, (u64)fd);
}
static inline int open(const char *path, int flags, ...) {
    return (int)syscall2(SYS_OPEN, (u64)path, (u64)flags);
}
static inline off_t lseek(int fd, off_t offset, int whence) {
    return (off_t)syscall3(SYS_LSEEK, (u64)fd, (u64)offset, (u64)whence);
}
static inline int mkdir(const char *path, mode_t mode) {
    return (int)syscall2(SYS_MKDIR, (u64)path, (u64)mode);
}
static inline int ftruncate(int fd, off_t length) {
    (void)fd;
    (void)length;
    errno = ENOSYS;
    return -1;
}
static inline pid_t getpid(void) {
    return (pid_t)syscall0(SYS_GETPID);
}
static inline pid_t getppid(void) {
    return (pid_t)syscall0(SYS_GETPPID);
}
static inline void _exit(int status) {
    syscall1(SYS_EXIT, (u64)status);
    __builtin_unreachable();
}
static inline unsigned int sleep(unsigned int seconds) {
    syscall1(SYS_SLEEPMS, (u64)(seconds * 1000));
    return 0;
}
static inline int usleep(unsigned int usec) {
    syscall1(SYS_SLEEPMS, (u64)(usec / 1000));
    return 0;
}
static inline int pause(void) {
    syscall1(SYS_SLEEPMS, 1000);
    return -1;
}
static inline int execve(const char *path, char *const argv[], char *const envp[]) {
    (void)path; (void)argv; (void)envp;
    errno = ENOSYS;
    return -1;
}
static inline pid_t fork(void) {
    return (pid_t)syscall0(SYS_FORK);
}
static inline pid_t waitpid(pid_t pid, int *status, int options) {
    return (pid_t)syscall3(SYS_WAITPID, (u64)pid, (u64)status, (u64)options);
}

char   *getcwd(char *buf, size_t size);
int     chdir(const char *path);
int     access(const char *path, int mode);
int     isatty(int fd);
static inline int dup(int fd) {
    return (int)syscall1(SYS_DUP, (u64)fd);
}
static inline int dup2(int oldfd, int newfd) {
    return (int)syscall2(SYS_DUP2, (u64)oldfd, (u64)newfd);
}
static inline ssize_t readlink(const char *path, char *buf, size_t bufsiz) {
    (void)path;
    (void)buf;
    (void)bufsiz;
    errno = ENOSYS;
    return -1;
}
static inline int close_range(unsigned int first, unsigned int last, int flags) {
    (void)flags;
    for (unsigned int fd = first; fd <= last && fd < 64; fd++) close((int)fd);
    return 0;
}
static inline void closefrom(int lowfd) {
    for (int fd = lowfd; fd < 64; fd++) close(fd);
}
static inline int ttyname_r(int fd, char *buf, size_t buflen) {
    (void)fd;
    const char *name = "/dev/console";
    size_t i = 0;
    if (!buf || buflen == 0) return -1;
    for (; i + 1 < buflen && name[i]; i++) buf[i] = name[i];
    buf[i] = 0;
    return 0;
}
static inline char *ctermid(char *s) {
    static char term[] = "/dev/console";
    if (!s) return term;
    for (int i = 0; term[i]; i++) s[i] = term[i];
    return s;
}
static inline int chown(const char *path, uid_t owner, gid_t group) { (void)path; (void)owner; (void)group; return 0; }
static inline int fchown(int fd, uid_t owner, gid_t group) { (void)fd; (void)owner; (void)group; return 0; }
static inline int lchown(const char *path, uid_t owner, gid_t group) { (void)path; (void)owner; (void)group; return 0; }
static inline int fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags) {
    (void)dirfd; (void)path; (void)owner; (void)group; (void)flags; return 0;
}
static inline void sync(void) {}
static inline int nice(int inc) { (void)inc; return 0; }
static inline int rename(const char *oldpath, const char *newpath) { (void)oldpath; (void)newpath; errno = ENOSYS; return -1; }
static inline int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath) {
    (void)olddirfd; (void)newdirfd; return rename(oldpath, newpath);
}
static inline int unlink(const char *path) {
    int rc = (int)syscall1(SYS_UNLINK, (u64)path);
    if (rc < 0) errno = ENOENT;
    return rc;
}
static inline int unlinkat(int dirfd, const char *path, int flags) {
    (void)dirfd; (void)flags; return unlink(path);
}
static inline int rmdir(const char *path) { (void)path; errno = ENOSYS; return -1; }
static inline mode_t umask(mode_t mask) { (void)mask; return 0; }
static inline long sysconf(int name) {
    if (name == _SC_PAGESIZE || name == _SC_PAGE_SIZE) return 4096;
    if (name == _SC_OPEN_MAX) return 64;
    if (name == _SC_TTY_NAME_MAX) return 64;
    errno = EINVAL;
    return -1;
}
static inline int getpagesize(void) { return 4096; }

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
