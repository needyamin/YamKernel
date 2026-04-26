/* ============================================================================
 * YamKernel — Anonymous Pipe (POSIX IPC)
 * Unidirectional byte stream between two file descriptors.
 * ============================================================================ */
#ifndef _IPC_PIPE_H
#define _IPC_PIPE_H

#include <nexus/types.h>
#include "../sched/wait.h"
#include "../lib/spinlock.h"

#define PIPE_BUF_SIZE   4096

typedef struct pipe {
    u8            buf[PIPE_BUF_SIZE];
    usize         read_pos;
    usize         write_pos;
    usize         count;
    spinlock_t    lock;
    wait_queue_t  readers;
    wait_queue_t  writers;
    u32           readers_open;     /* ref count of read-end FDs */
    u32           writers_open;     /* ref count of write-end FDs */
} pipe_t;

/* Create a pipe: returns 0 on success, sets fds[0] = read-end, fds[1] = write-end */
int sys_pipe(int fds[2]);

#endif /* _IPC_PIPE_H */
