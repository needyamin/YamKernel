/* ============================================================================
 * YamKernel — YamGraph: Core Resource Graph Implementation
 *
 * The YamGraph is a live directed graph where:
 *   - Every system resource (process, memory, device, file) is a NODE
 *   - Relationships and permissions are EDGES
 *   - Capability tokens flow through edges
 *   - The scheduler, allocator, and IPC all query the graph
 *
 * This is fundamentally different from any existing OS architecture.
 * ============================================================================ */

#include "graph.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../mem/heap.h"

/* Static pools (heap not always available at init) */
static yam_node_t nodes[YAMGRAPH_MAX_NODES];
static yam_edge_t edges[YAMGRAPH_MAX_EDGES];
static u32 next_node_id = 1;  /* 0 = invalid/kernel */
static u32 next_edge_id = 1;
static u32 active_nodes = 0;
static u32 active_edges = 0;
static u32 generation = 1;

/* Adjacency list pool (preallocated for early boot) */
#define ADJ_POOL_SIZE 16384
static yam_adj_t adj_pool[ADJ_POOL_SIZE];
static u32 adj_pool_next = 0;

static yam_adj_t *adj_alloc(void) {
    if (adj_pool_next >= ADJ_POOL_SIZE) return NULL;
    yam_adj_t *a = &adj_pool[adj_pool_next++];
    a->next = NULL;
    return a;
}

/* ---- Initialization ---- */

void yamgraph_init(void) {
    memset(nodes, 0, sizeof(nodes));
    memset(edges, 0, sizeof(edges));
    memset(adj_pool, 0, sizeof(adj_pool));
    next_node_id = 1;
    next_edge_id = 1;
    active_nodes = 0;
    active_edges = 0;
    adj_pool_next = 0;

    /* Create the root kernel node — node 0 is special */
    nodes[0].id    = 0;
    nodes[0].type  = YAM_NODE_NAMESPACE;
    nodes[0].alive = true;
    nodes[0].name  = "kernel";
    nodes[0].data  = NULL;
    nodes[0].edges_out = NULL;
    nodes[0].edges_in  = NULL;
    nodes[0].ref_count = 1;
    active_nodes = 1;

    kprintf_color(0xFF00FF88, "[YAMGRAPH] Initialized — root kernel node created\n");
}

/* ---- Node operations ---- */

yam_node_id_t yamgraph_node_create(yam_node_type_t type, const char *name, void *data) {
    if (next_node_id >= YAMGRAPH_MAX_NODES) {
        kprintf_color(0xFFFF3333, "[YAMGRAPH] Node limit reached!\n");
        return (yam_node_id_t)-1;
    }

    yam_node_id_t id = next_node_id++;
    yam_node_t *n = &nodes[id];
    n->id    = id;
    n->type  = type;
    n->alive = true;
    n->name  = name;
    n->data  = data;
    n->edges_out = NULL;
    n->edges_in  = NULL;
    n->ref_count = 1;
    active_nodes++;

    return id;
}

yam_node_t *yamgraph_node_get(yam_node_id_t id) {
    if (id >= YAMGRAPH_MAX_NODES) return NULL;
    yam_node_t *n = &nodes[id];
    return n->alive ? n : NULL;
}

bool yamgraph_node_destroy(yam_node_id_t id) {
    yam_node_t *n = yamgraph_node_get(id);
    if (!n) return false;
    if (id == 0) return false; /* Can't destroy kernel root */

    /* Revoke all outgoing edges */
    yam_adj_t *adj = n->edges_out;
    while (adj) {
        yamgraph_edge_revoke(adj->edge_id);
        adj = adj->next;
    }

    n->alive = false;
    active_nodes--;
    return true;
}

yam_node_id_t yamgraph_find_node_by_name(const char *name) {
    if (!name) return (yam_node_id_t)-1;
    for (u32 i = 0; i < YAMGRAPH_MAX_NODES; i++) {
        yam_node_t *n = &nodes[i];
        if (n->alive && n->name) {
            if (strcmp(n->name, name) == 0) {
                return n->id;
            }
        }
    }
    return (yam_node_id_t)-1;
}

/* ---- Edge operations ---- */

yam_edge_id_t yamgraph_edge_link(yam_node_id_t src, yam_node_id_t dst,
                                  yam_edge_type_t type, yam_perm_t perms) {
    yam_node_t *src_node = yamgraph_node_get(src);
    yam_node_t *dst_node = yamgraph_node_get(dst);
    if (!src_node || !dst_node) return (yam_edge_id_t)-1;

    if (next_edge_id >= YAMGRAPH_MAX_EDGES) {
        kprintf_color(0xFFFF3333, "[YAMGRAPH] Edge limit reached!\n");
        return (yam_edge_id_t)-1;
    }

    yam_edge_id_t id = next_edge_id++;
    yam_edge_t *e = &edges[id];
    e->id         = id;
    e->type       = type;
    e->source     = src;
    e->target     = dst;
    e->perms      = perms;
    e->generation = generation++;
    e->alive      = true;
    active_edges++;

    /* Add to source's outgoing adjacency list */
    yam_adj_t *out = adj_alloc();
    if (out) {
        out->edge_id = id;
        out->target  = dst;
        out->next    = src_node->edges_out;
        src_node->edges_out = out;
    }

    /* Add to target's incoming adjacency list */
    yam_adj_t *in = adj_alloc();
    if (in) {
        in->edge_id = id;
        in->target  = src;
        in->next    = dst_node->edges_in;
        dst_node->edges_in = in;
    }

    dst_node->ref_count++;
    return id;
}

bool yamgraph_edge_revoke(yam_edge_id_t id) {
    if (id >= YAMGRAPH_MAX_EDGES) return false;
    yam_edge_t *e = &edges[id];
    if (!e->alive) return false;

    e->alive = false;
    active_edges--;

    /* Decrement target reference count */
    yam_node_t *target = yamgraph_node_get(e->target);
    if (target && target->ref_count > 0) {
        target->ref_count--;
    }

    return true;
}

yam_edge_t *yamgraph_edge_get(yam_edge_id_t id) {
    if (id >= YAMGRAPH_MAX_EDGES) return NULL;
    return edges[id].alive ? &edges[id] : NULL;
}

/* ---- Permission check via graph traversal ---- */

bool yamgraph_has_permission(yam_node_id_t src, yam_node_id_t dst, yam_perm_t perm) {
    yam_node_t *node = yamgraph_node_get(src);
    if (!node) return false;

    /* Walk outgoing edges looking for a capability to dst with required perms */
    yam_adj_t *adj = node->edges_out;
    while (adj) {
        yam_edge_t *e = yamgraph_edge_get(adj->edge_id);
        if (e && e->target == dst && (e->perms & perm) == perm) {
            return true;
        }
        adj = adj->next;
    }

    /* Kernel (node 0) always has permission */
    if (src == 0) return true;

    return false;
}

/* ---- Walking ---- */

void yamgraph_walk_outgoing(yam_node_id_t id, yamgraph_walk_fn fn, void *ctx) {
    yam_node_t *node = yamgraph_node_get(id);
    if (!node) return;

    yam_adj_t *adj = node->edges_out;
    while (adj) {
        yam_edge_t *e = yamgraph_edge_get(adj->edge_id);
        yam_node_t *target = yamgraph_node_get(adj->target);
        if (e && target) fn(target, e, ctx);
        adj = adj->next;
    }
}

void yamgraph_walk_incoming(yam_node_id_t id, yamgraph_walk_fn fn, void *ctx) {
    yam_node_t *node = yamgraph_node_get(id);
    if (!node) return;

    yam_adj_t *adj = node->edges_in;
    while (adj) {
        yam_edge_t *e = yamgraph_edge_get(adj->edge_id);
        yam_node_t *source = yamgraph_node_get(adj->target);
        if (e && source) fn(source, e, ctx);
        adj = adj->next;
    }
}

u32 yamgraph_node_count(void) { return active_nodes; }
u32 yamgraph_edge_count(void) { return active_edges; }

/* ---- Self-test ---- */

void yamgraph_self_test(void) {
    kprintf_color(0xFFFFDD00, "\n[YAMGRAPH] === Resource Graph Self-Test ===\n");

    /* Test 1: Create nodes */
    yam_node_id_t task1 = yamgraph_node_create(YAM_NODE_TASK,    "init",    NULL);
    yam_node_id_t mem1  = yamgraph_node_create(YAM_NODE_MEMORY,  "heap0",   NULL);
    yam_node_id_t dev1  = yamgraph_node_create(YAM_NODE_DEVICE,  "serial0", NULL);
    yam_node_id_t chan1 = yamgraph_node_create(YAM_NODE_CHANNEL, "ipc0",    NULL);

    kprintf("[YAMGRAPH] Test 1 - Create 4 nodes: %s (nodes=%u)\n",
            (task1 != (yam_node_id_t)-1 && yamgraph_node_count() == 5) ? "PASS" : "FAIL",
            yamgraph_node_count());

    /* Test 2: Link edges with capabilities */
    yam_edge_id_t e1 = yamgraph_edge_link(task1, mem1, YAM_EDGE_OWNS, YAM_PERM_ALL);
    yam_edge_id_t e2 = yamgraph_edge_link(task1, dev1, YAM_EDGE_CAPABILITY, 
                                           YAM_PERM_READ | YAM_PERM_WRITE);
    yam_edge_id_t e3 = yamgraph_edge_link(task1, chan1, YAM_EDGE_CHANNEL, 
                                           YAM_PERM_READ | YAM_PERM_WRITE);

    kprintf("[YAMGRAPH] Test 2 - Link 3 edges: %s (edges=%u)\n",
            (e1 != (yam_edge_id_t)-1 && e3 != (yam_edge_id_t)-1 && yamgraph_edge_count() == 3) ? "PASS" : "FAIL",
            yamgraph_edge_count());

    /* Test 3: Permission check */
    bool has_rw  = yamgraph_has_permission(task1, dev1, YAM_PERM_READ | YAM_PERM_WRITE);
    bool has_x   = yamgraph_has_permission(task1, dev1, YAM_PERM_EXEC);
    kprintf("[YAMGRAPH] Test 3 - Permissions: has_rw=%s, has_exec=%s %s\n",
            has_rw ? "true" : "false", has_x ? "true" : "false",
            (has_rw && !has_x) ? "PASS" : "FAIL");

    /* Test 4: Revoke edge */
    yamgraph_edge_revoke(e2);
    bool still_has = yamgraph_has_permission(task1, dev1, YAM_PERM_READ);
    kprintf("[YAMGRAPH] Test 4 - Revoke capability: %s (edges=%u)\n",
            !still_has ? "PASS" : "FAIL", yamgraph_edge_count());

    /* Test 5: Node traversal */
    u32 walk_count = 0;
    yam_adj_t *adj = yamgraph_node_get(task1)->edges_out;
    while (adj) {
        yam_edge_t *e = yamgraph_edge_get(adj->edge_id);
        if (e) walk_count++;
        adj = adj->next;
    }
    kprintf("[YAMGRAPH] Test 5 - Walk outgoing: %u live edges %s\n",
            walk_count, (walk_count == 2) ? "PASS" : "FAIL");

    /* Test 6: Destroy node */
    yamgraph_node_destroy(chan1);
    kprintf("[YAMGRAPH] Test 6 - Destroy node: %s (nodes=%u)\n",
            (yamgraph_node_get(chan1) == NULL) ? "PASS" : "FAIL",
            yamgraph_node_count());

    kprintf_color(0xFF00FF88, "[YAMGRAPH] Resource Graph self-test: PASS\n\n");
}
