/* ============================================================================
 * YamKernel — YamGraph: Core Resource Graph
 * The novel heart of the kernel — all resources are graph nodes
 * ============================================================================ */

#ifndef _NEXUS_GRAPH_H
#define _NEXUS_GRAPH_H

#include <nexus/types.h>

/* Maximum nodes and edges in the graph */
#define YAMGRAPH_MAX_NODES 4096
#define YAMGRAPH_MAX_EDGES 8192

/* Adjacency list entry */
typedef struct yam_adj {
    yam_edge_id_t  edge_id;
    yam_node_id_t  target;
    struct yam_adj *next;
} yam_adj_t;

/* Graph node */
typedef struct {
    yam_node_id_t   id;
    yam_node_type_t type;
    bool            alive;
    const char     *name;       /* Human-readable name (debug) */
    void           *data;       /* Node-specific data pointer */
    yam_adj_t      *edges_out;  /* Outgoing edges */
    yam_adj_t      *edges_in;   /* Incoming edges */
    u32             ref_count;  /* Reference count */
} yam_node_t;

/* Graph edge */
typedef struct {
    yam_edge_id_t   id;
    yam_edge_type_t type;
    yam_node_id_t   source;
    yam_node_id_t   target;
    yam_perm_t      perms;      /* Permission bitmask */
    u32             generation; /* For revocation tracking */
    bool            alive;
} yam_edge_t;

/* ---- Graph API ---- */

void          yamgraph_init(void);

/* Node operations */
yam_node_id_t yamgraph_node_create(yam_node_type_t type, const char *name, void *data);
yam_node_t   *yamgraph_node_get(yam_node_id_t id);
bool          yamgraph_node_destroy(yam_node_id_t id);

/* Edge operations */
yam_edge_id_t yamgraph_edge_link(yam_node_id_t src, yam_node_id_t dst, 
                                  yam_edge_type_t type, yam_perm_t perms);
bool          yamgraph_edge_revoke(yam_edge_id_t id);
yam_edge_t   *yamgraph_edge_get(yam_edge_id_t id);

/* Query operations */
bool          yamgraph_has_permission(yam_node_id_t src, yam_node_id_t dst, yam_perm_t perm);
u32           yamgraph_node_count(void);
u32           yamgraph_edge_count(void);

/* Walk edges from a node */
typedef void (*yamgraph_walk_fn)(yam_node_t *node, yam_edge_t *edge, void *ctx);
void          yamgraph_walk_outgoing(yam_node_id_t id, yamgraph_walk_fn fn, void *ctx);
void          yamgraph_walk_incoming(yam_node_id_t id, yamgraph_walk_fn fn, void *ctx);

/* Self-test */
void          yamgraph_self_test(void);

#endif /* _NEXUS_GRAPH_H */
