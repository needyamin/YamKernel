#ifndef _LIBYAM_IPC_H
#define _LIBYAM_IPC_H

#include <nexus/types.h>
#include "syscall.h"

#define CHANNEL_MSG_MAX_SIZE 256

typedef struct {
    u32 sender;
    u32 msg_type;
    u32 length;
    u8  data[CHANNEL_MSG_MAX_SIZE];
} yam_message_t;

static inline bool yam_channel_send(u32 node_id, u32 msg_type, const void *data, u32 length) {
    return syscall4(SYS_CHANNEL_SEND, node_id, msg_type, (u64)data, length) == 0;
}

static inline bool yam_channel_recv(u32 node_id, yam_message_t *out) {
    return syscall2(SYS_CHANNEL_RECV, node_id, (u64)out) == 0;
}

static inline u32 yam_channel_lookup(const char *name) {
    return (u32)syscall1(SYS_CHANNEL_LOOKUP, (u64)name);
}

#endif
