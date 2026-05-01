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

/* lseek origins */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Syscall wrappers */
static inline ssize_t read(int fd, void *buf, size_t count) {
    return (ssize_t)syscall3(SYS_READ, (u64)fd, (u64)buf, (u64)count);
}
static inline ssize_t write(int fd, const void *buf, size_t count) {
    return (ssize_t)syscall3(SYS_WRITE, (u64)fd, (u64)buf, (u64)count);
}
static inline int close(int fd) {
    return (int)syscall1(SYS_CLOSE, (u64)fd);
}
static inline int open(const char *path, int flags) {
    return (int)syscall2(SYS_OPEN, (u64)path, (u64)flags);
}
static inline off_t lseek(int fd, off_t offset, int whence) {
    (void)fd; (void)offset; (void)whence;
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

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
