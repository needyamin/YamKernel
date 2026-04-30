/* ============================================================================
 * YamKernel — IPC Channel Implementation
 * Channels are first-class graph edges with ring-buffered messages
 * ============================================================================ */

#include "channel.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../lib/kdebug.h"

/* Channel pool */
#define MAX_CHANNELS 256
static yam_channel_t channels[MAX_CHANNELS];
static u32 channel_count = 0;

yam_channel_t *channel_create(const char *name, yam_node_id_t a, yam_node_id_t b) {
    if (channel_count >= MAX_CHANNELS) {
        kprintf_color(0xFFFF3333, "[CHANNEL] Channel limit reached!\n");
        return NULL;
    }

    yam_channel_t *chan = &channels[channel_count++];
    memset(chan, 0, sizeof(yam_channel_t));

    /* Create a channel node in the YamGraph */
    chan->node_id = yamgraph_node_create(YAM_NODE_CHANNEL, name, chan);
    chan->endpoint_a = a;
    chan->endpoint_b = b;
    chan->a_to_b.head = chan->a_to_b.tail = chan->a_to_b.count = 0;
    chan->b_to_a.head = chan->b_to_a.tail = chan->b_to_a.count = 0;
    chan->active = true;
    KINFO("CHANNEL", "Created '%s' id=%lu (%lu <-> %lu)", name, (u64)chan->node_id, (u64)a, (u64)b);

    /* Link both endpoints to the channel via graph edges */
    yamgraph_edge_link(a, chan->node_id, YAM_EDGE_CHANNEL,
                       YAM_PERM_READ | YAM_PERM_WRITE);
    yamgraph_edge_link(b, chan->node_id, YAM_EDGE_CHANNEL,
                       YAM_PERM_READ | YAM_PERM_WRITE);

    return chan;
}

yam_channel_t *channel_get(yam_node_id_t node_id) {
    for (u32 i = 0; i < channel_count; i++) {
        if (channels[i].active && channels[i].node_id == node_id) {
            return &channels[i];
        }
    }
    return NULL;
}

bool channel_send(yam_channel_t *chan, yam_node_id_t sender,
                  u32 msg_type, const void *data, u32 length) {
    if (!chan || !chan->active) return false;
    if (length > CHANNEL_MSG_MAX_SIZE) return false;

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

    /* Determine direction */
    if (sender == chan->endpoint_a) {
        if (chan->a_to_b.count >= CHANNEL_RING_SIZE) return false;
        yam_message_t *msg = &chan->a_to_b.ring[chan->a_to_b.tail];
        msg->sender = sender;
        msg->msg_type = msg_type;
        msg->length = length;
        if (data && length > 0) memcpy(msg->data, data, length);
        chan->a_to_b.tail = (chan->a_to_b.tail + 1) % CHANNEL_RING_SIZE;
        chan->a_to_b.count++;
    } else {
        if (chan->b_to_a.count >= CHANNEL_RING_SIZE) return false;
        yam_message_t *msg = &chan->b_to_a.ring[chan->b_to_a.tail];
        msg->sender = sender;
        msg->msg_type = msg_type;
        msg->length = length;
        if (data && length > 0) memcpy(msg->data, data, length);
        chan->b_to_a.tail = (chan->b_to_a.tail + 1) % CHANNEL_RING_SIZE;
        chan->b_to_a.count++;
    }
    return true;
}

bool channel_recv(yam_channel_t *chan, yam_node_id_t receiver, yam_message_t *out) {
    if (!chan || !chan->active) return false;

    if (receiver == chan->endpoint_b) {
        /* B receives from A */
        if (chan->a_to_b.count == 0) return false;
        memcpy(out, &chan->a_to_b.ring[chan->a_to_b.head], sizeof(yam_message_t));
        chan->a_to_b.head = (chan->a_to_b.head + 1) % CHANNEL_RING_SIZE;
        chan->a_to_b.count--;
        return true;
    } else if (receiver == chan->endpoint_a) {
        /* A receives from B */
        if (chan->b_to_a.count == 0) return false;
        memcpy(out, &chan->b_to_a.ring[chan->b_to_a.head], sizeof(yam_message_t));
        chan->b_to_a.head = (chan->b_to_a.head + 1) % CHANNEL_RING_SIZE;
        chan->b_to_a.count--;
        return true;
    }
    return false;
}

void channel_close(yam_channel_t *chan) {
    if (!chan) return;
    chan->active = false;
    yamgraph_node_destroy(chan->node_id);
}

bool channel_has_message(yam_channel_t *chan, yam_node_id_t receiver) {
    if (!chan || !chan->active) return false;
    if (receiver == chan->endpoint_b) return chan->a_to_b.count > 0;
    if (receiver == chan->endpoint_a) return chan->b_to_a.count > 0;
    return false;
}
