#ifndef _LIBC_SIGNAL_H
#define _LIBC_SIGNAL_H

#include <nexus/types.h>
#include "sys/types.h"
#include "../libyam/syscall.h"

typedef int sig_atomic_t;
typedef unsigned long sigset_t;

typedef struct {
    int si_signo;
    int si_code;
    int si_errno;
    long si_band;
} siginfo_t;

typedef struct {
    void *ss_sp;
    int ss_flags;
    usize ss_size;
} stack_t;

struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_sigaction)(int, siginfo_t *, void *);
};

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

#define SIGHUP  1
#define SIGINT  2
#define SIGQUIT 3
#define SIGILL  4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS  7
#define SIGFPE  8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGSTKSZ 8192
#define SA_ONSTACK 0x08000000
#define SA_NODEFER 0x40000000
#define SA_RESTART 0x10000000

int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
void (*signal(int signum, void (*handler)(int)))(int);
int raise(int sig);
static inline int sigaltstack(const stack_t *ss, stack_t *old_ss) {
    (void)ss;
    if (old_ss) {
        old_ss->ss_sp = (void *)0;
        old_ss->ss_flags = 0;
        old_ss->ss_size = 0;
    }
    return 0;
}
static inline int kill(pid_t pid, int sig) {
    return (int)syscall2(SYS_KILL, (u64)pid, (u64)sig);
}

#endif
