/* ============================================================================
 * YamKernel — IPC Channel Implementation
 * Channels are first-class graph edges with ring-buffered messages
 * ============================================================================ */

#include "channel.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

/* Channel pool */
#define MAX_CHANNELS 256
static yam_channel_t channels[MAX_CHANNELS];
static u32 channel_count = 0;

yam_channel_t *channel_create(yam_node_id_t a, yam_node_id_t b) {
    if (channel_count >= MAX_CHANNELS) {
        kprintf_color(0xFFFF3333, "[CHANNEL] Channel limit reached!\n");
        return NULL;
    }

    yam_channel_t *chan = &channels[channel_count++];
    memset(chan, 0, sizeof(yam_channel_t));

    /* Create a channel node in the YamGraph */
    chan->node_id = yamgraph_node_create(YAM_NODE_CHANNEL, "channel", chan);
    chan->endpoint_a = a;
    chan->endpoint_b = b;
    chan->head = 0;
    chan->tail = 0;
    chan->count = 0;
    chan->active = true;

    /* Link both endpoints to the channel via graph edges */
    yamgraph_edge_link(a, chan->node_id, YAM_EDGE_CHANNEL,
                       YAM_PERM_READ | YAM_PERM_WRITE);
    yamgraph_edge_link(b, chan->node_id, YAM_EDGE_CHANNEL,
                       YAM_PERM_READ | YAM_PERM_WRITE);

    return chan;
}

bool channel_send(yam_channel_t *chan, yam_node_id_t sender,
                  u32 msg_type, const void *data, u32 length) {
    if (!chan || !chan->active) return false;
    if (length > CHANNEL_MSG_MAX_SIZE) return false;
    if (chan->count >= CHANNEL_RING_SIZE) return false; /* Ring full */

    /* Verify sender is an endpoint */
    if (sender != chan->endpoint_a && sender != chan->endpoint_b) {
        kprintf_color(0xFFFF3333, "[CHANNEL] Unauthorized sender!\n");
        return false;
    }

    /* Check permission via YamGraph */
    if (!yamgraph_has_permission(sender, chan->node_id, YAM_PERM_WRITE)) {
        kprintf_color(0xFFFF3333, "[CHANNEL] Permission denied!\n");
        return false;
    }

    yam_message_t *msg = &chan->ring[chan->tail];
    msg->sender   = sender;
    msg->msg_type = msg_type;
    msg->length   = length;
    if (data && length > 0) {
        memcpy(msg->data, data, length);
    }

    chan->tail = (chan->tail + 1) % CHANNEL_RING_SIZE;
    chan->count++;
    return true;
}

bool channel_recv(yam_channel_t *chan, yam_message_t *out) {
    if (!chan || !chan->active) return false;
    if (chan->count == 0) return false;

    memcpy(out, &chan->ring[chan->head], sizeof(yam_message_t));
    chan->head = (chan->head + 1) % CHANNEL_RING_SIZE;
    chan->count--;
    return true;
}

void channel_close(yam_channel_t *chan) {
    if (!chan) return;
    chan->active = false;
    yamgraph_node_destroy(chan->node_id);
}

bool channel_has_message(yam_channel_t *chan) {
    return chan && chan->active && chan->count > 0;
}
