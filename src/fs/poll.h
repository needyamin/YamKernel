/* ============================================================================
 * YamKernel — Poll Subsystem
 * Allows tasks to wait on multiple file descriptors simultaneously.
 * ============================================================================ */
#ifndef _FS_POLL_H
#define _FS_POLL_H

#include <nexus/types.h>

/* Events for poll */
#define POLLIN      0x001   /* Data available to read */
#define POLLOUT     0x004   /* Writing won't block */
#define POLLERR     0x008   /* Error */
#define POLLHUP     0x010   /* Hang up */
#define POLLNVAL    0x020   /* Invalid fd */

typedef struct {
    int   fd;
    u16   events;     /* Requested events */
    u16   revents;    /* Returned events */
} pollfd_t;

/* Wait for events on a set of file descriptors.
 * Returns: number of FDs with events, 0 on timeout, -1 on error.
 * timeout_ms: -1 = block forever, 0 = non-blocking poll */
int sys_poll(pollfd_t *fds, u32 nfds, i64 timeout_ms);

#endif /* _FS_POLL_H */
