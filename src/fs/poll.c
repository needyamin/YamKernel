/* ============================================================================
 * YamKernel — Poll Implementation
 * Implements sys_poll by checking each FD's file_operations->poll callback.
 * If no FDs are ready, sleeps and retries until timeout.
 * ============================================================================ */
#include "poll.h"
#include "vfs.h"
#include "../sched/sched.h"
#include "../sched/wait.h"
#include "../drivers/timer/pit.h"
#include "../lib/kprintf.h"

int sys_poll(pollfd_t *fds, u32 nfds, i64 timeout_ms) {
    if (!fds || nfds == 0) return -1;

    u64 deadline = 0;
    if (timeout_ms > 0) {
        deadline = pit_get_ticks() + (u64)timeout_ms;
    }

    for (;;) {
        int ready = 0;

        for (u32 i = 0; i < nfds; i++) {
            fds[i].revents = 0;
            
            if (fds[i].fd < 0) {
                continue;
            }

            file_t *f = fd_get(fds[i].fd);
            if (!f) {
                fds[i].revents = POLLNVAL;
                ready++;
                continue;
            }

            if (f->fops && f->fops->poll) {
                int result = f->fops->poll(f);
                if (result > 0) {
                    if (fds[i].events & POLLIN)  fds[i].revents |= POLLIN;
                    if (fds[i].events & POLLOUT) fds[i].revents |= POLLOUT;
                    ready++;
                }
            }
        }

        if (ready > 0) return ready;
        if (timeout_ms == 0) return 0;
        if (timeout_ms > 0 && pit_get_ticks() >= deadline) return 0;

        task_sleep_ms(1);
    }
}

