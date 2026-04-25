/* ============================================================================
 * YamKernel — IPC Framework
 * Defines the skeleton interfaces for Pipes, FIFOs, Shared Memory, Message Queues, Sockets.
 * ============================================================================ */
#ifndef _IPC_IPC_H
#define _IPC_IPC_H

#include <nexus/types.h>

void ipc_init(void);

void ipc_shm_create(void);
void ipc_pipe_create(void);
void ipc_msgq_create(void);
void ipc_socket_create(void);

#endif
