/* ============================================================================
 * YamKernel — Capability Manager Implementation
 * Capabilities are unforgeable tokens built on the YamGraph
 * ============================================================================ */

#include "capability.h"
#include "../lib/kprintf.h"

yam_capability_t cap_create(yam_node_id_t holder, yam_node_id_t target, yam_perm_t perms) {
    yam_capability_t cap = {0};

    yam_edge_id_t edge = yamgraph_edge_link(holder, target, YAM_EDGE_CAPABILITY, perms);
    if (edge == (yam_edge_id_t)-1) {
        cap.edge_id = (yam_edge_id_t)-1;
        return cap;
    }

    yam_edge_t *e = yamgraph_edge_get(edge);
    cap.edge_id    = edge;
    cap.holder     = holder;
    cap.target     = target;
    cap.perms      = perms;
    cap.generation = e ? e->generation : 0;

    return cap;
}

yam_capability_t cap_delegate(yam_capability_t *cap, yam_node_id_t new_holder, yam_perm_t mask) {
    yam_capability_t new_cap = {0};

    /* Can only delegate if holder has DELEGATE permission */
    if (!(cap->perms & YAM_PERM_DELEGATE)) {
        kprintf_color(0xFFFF3333, "[CAP] Delegation denied: no DELEGATE permission\n");
        new_cap.edge_id = (yam_edge_id_t)-1;
        return new_cap;
    }

    /* New capability has intersection of original perms and mask */
    yam_perm_t new_perms = cap->perms & mask;
    return cap_create(new_holder, cap->target, new_perms);
}

bool cap_revoke(yam_capability_t *cap) {
    if (!cap_is_valid(cap)) return false;
    return yamgraph_edge_revoke(cap->edge_id);
}

bool cap_is_valid(yam_capability_t *cap) {
    if (cap->edge_id == (yam_edge_id_t)-1) return false;
    yam_edge_t *e = yamgraph_edge_get(cap->edge_id);
    if (!e) return false;
    if (e->generation != cap->generation) return false;
    return true;
}

bool cap_check(yam_capability_t *cap, yam_perm_t perm) {
    if (!cap_is_valid(cap)) return false;
    return (cap->perms & perm) == perm;
}
