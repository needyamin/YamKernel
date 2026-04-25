/* ============================================================================
 * YamKernel — IPC Framework
 * ============================================================================ */
#include "ipc.h"
#include "../lib/kprintf.h"
#include "../nexus/graph.h"

void ipc_shm_create(void)   { kprintf_color(0xFF888888, "[IPC] Shared Memory created\n"); }
void ipc_pipe_create(void)  { kprintf_color(0xFF888888, "[IPC] Pipe/FIFO created\n"); }
void ipc_msgq_create(void)  { kprintf_color(0xFF888888, "[IPC] Message Queue created\n"); }
void ipc_socket_create(void){ kprintf_color(0xFF888888, "[IPC] Unix Socket created\n"); }

void ipc_init(void) {
    kprintf_color(0xFF00DDFF, "[IPC] Initializing IPC Mechanisms...\n");
    
    /* Register IPC in YamGraph */
    yam_node_id_t ipc_node = yamgraph_node_create(YAM_NODE_NAMESPACE, "ipc", NULL);
    yamgraph_edge_link(0, ipc_node, YAM_EDGE_OWNS, YAM_PERM_ALL);
}
