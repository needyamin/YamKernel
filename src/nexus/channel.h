/* ============================================================================
 * YamKernel — IPC Channels
 * ============================================================================ */

#ifndef _NEXUS_CHANNEL_H
#define _NEXUS_CHANNEL_H

#include <nexus/types.h>
#include "graph.h"

/* Channel message */
#define CHANNEL_MSG_MAX_SIZE 256

typedef struct {
    yam_node_id_t sender;
    u32           msg_type;
    u32           length;
    u8            data[CHANNEL_MSG_MAX_SIZE];
} yam_message_t;

/* Channel ring buffer */
#define CHANNEL_RING_SIZE 64

typedef struct {
    yam_node_id_t  node_id;      /* This channel's node in YamGraph */
    yam_node_id_t  endpoint_a;   /* Connected node A */
    yam_node_id_t  endpoint_b;   /* Connected node B */
    yam_message_t  ring[CHANNEL_RING_SIZE];
    u32            head;
    u32            tail;
    u32            count;
    bool           active;
} yam_channel_t;

/* Create a bidirectional channel between two nodes */
yam_channel_t *channel_create(yam_node_id_t a, yam_node_id_t b);

/* Send a message through a channel */
bool channel_send(yam_channel_t *chan, yam_node_id_t sender,
                  u32 msg_type, const void *data, u32 length);

/* Receive a message from a channel */
bool channel_recv(yam_channel_t *chan, yam_message_t *out);

/* Close a channel */
void channel_close(yam_channel_t *chan);

/* Check if channel has pending messages */
bool channel_has_message(yam_channel_t *chan);

#endif /* _NEXUS_CHANNEL_H */
