/* ============================================================================
 * YamKernel — Capability Manager
 * ============================================================================ */

#ifndef _NEXUS_CAPABILITY_H
#define _NEXUS_CAPABILITY_H

#include <nexus/types.h>
#include "graph.h"

/* A capability token — unforgeable reference to a resource */
typedef struct {
    yam_edge_id_t  edge_id;    /* Backing edge in YamGraph */
    yam_node_id_t  holder;     /* Who holds this capability */
    yam_node_id_t  target;     /* What resource it refers to */
    yam_perm_t     perms;      /* Granted permissions */
    u32            generation; /* Revocation generation */
} yam_capability_t;

/* Create a capability (creates a graph edge) */
yam_capability_t cap_create(yam_node_id_t holder, yam_node_id_t target, yam_perm_t perms);

/* Duplicate a capability (delegate to another node, with optional perm reduction) */
yam_capability_t cap_delegate(yam_capability_t *cap, yam_node_id_t new_holder, yam_perm_t mask);

/* Revoke a capability (and all delegated copies downstream) */
bool cap_revoke(yam_capability_t *cap);

/* Check if a capability is still valid */
bool cap_is_valid(yam_capability_t *cap);

/* Check specific permission on a capability */
bool cap_check(yam_capability_t *cap, yam_perm_t perm);

#endif /* _NEXUS_CAPABILITY_H */
