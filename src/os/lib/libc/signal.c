/* ============================================================================
 * YamKernel — POSIX signal (User-Space libc)
 * ============================================================================ */
#include "signal.h"
#include "errno.h"
#include "unistd.h"

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
    yam_sigaction_t yact, yold;
    yam_sigaction_t *pact = NULL;
    yam_sigaction_t *pold = NULL;

    if (act) {
        yact.sa_handler = (u64)act->sa_handler;
        yact.sa_mask = (u64)act->sa_mask;
        yact.sa_flags = (u32)act->sa_flags;
        yact.sa_sigaction = (u64)act->sa_sigaction;
        pact = &yact;
    }
    
    if (oldact) {
        pold = &yold;
    }

    int rc = (int)syscall3(SYS_SIGACTION, (u64)signum, (u64)pact, (u64)pold);
    if (rc < 0) {
        errno = EINVAL;
        return -1;
    }

    if (oldact) {
        oldact->sa_handler = (void (*)(int))yold.sa_handler;
        oldact->sa_mask = (sigset_t)yold.sa_mask;
        oldact->sa_flags = (int)yold.sa_flags;
        oldact->sa_sigaction = (void (*)(int, siginfo_t *, void *))yold.sa_sigaction;
    }

    return 0;
}

void (*signal(int signum, void (*handler)(int)))(int) {
    struct sigaction act, old;
    act.sa_handler = handler;
    act.sa_mask = 0;
    act.sa_flags = SA_RESTART;
    
    if (sigaction(signum, &act, &old) < 0) {
        return SIG_ERR;
    }
    return old.sa_handler;
}

int raise(int sig) {
    return kill(getpid(), sig);
}

int sigemptyset(sigset_t *set) {
    if (set) *set = 0;
    return 0;
}

int sigfillset(sigset_t *set) {
    if (set) *set = ~0UL;
    return 0;
}

int sigaddset(sigset_t *set, int signum) {
    if (set && signum > 0 && signum < 32) {
        *set |= (1UL << signum);
        return 0;
    }
    errno = EINVAL;
    return -1;
}

int sigdelset(sigset_t *set, int signum) {
    if (set && signum > 0 && signum < 32) {
        *set &= ~(1UL << signum);
        return 0;
    }
    errno = EINVAL;
    return -1;
}
