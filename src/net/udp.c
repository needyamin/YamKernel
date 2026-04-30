/* ============================================================================
 * YamKernel — UDP Socket Implementation
 * ============================================================================ */
#include "net.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../mem/heap.h"

#define UDP_MAX_SOCKETS  32
#define UDP_MAX_CALLBACKS 16
#define UDP_RECV_BUF     4096

typedef struct {
    bool   active;
    u16    local_port;
    u8     recv_buf[UDP_RECV_BUF];
    u32    recv_src_ip;
    u16    recv_src_port;
    usize  recv_len;
    bool   has_data;
} udp_sock_t;

typedef struct {
    u16             port;
    udp_callback_t  cb;
} udp_cb_entry_t;

static udp_sock_t    g_udp_socks[UDP_MAX_SOCKETS];
static udp_cb_entry_t g_udp_cbs[UDP_MAX_CALLBACKS];
static int g_udp_cb_count = 0;
static u16 g_ephemeral_port = 49152;

void net_udp_init(void) {
    memset(g_udp_socks, 0, sizeof(g_udp_socks));
    kprintf_color(0xFF888888, "[UDP] Socket layer ready (%d sockets)\n", UDP_MAX_SOCKETS);
}

void udp_register_callback(u16 port, udp_callback_t cb) {
    if (g_udp_cb_count >= UDP_MAX_CALLBACKS) return;
    g_udp_cbs[g_udp_cb_count].port = port;
    g_udp_cbs[g_udp_cb_count].cb   = cb;
    g_udp_cb_count++;
}

int udp_socket(void) {
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!g_udp_socks[i].active) {
            g_udp_socks[i].active = true;
            g_udp_socks[i].local_port = g_ephemeral_port++;
            if (g_ephemeral_port == 0) g_ephemeral_port = 49152;
            return i;
        }
    }
    return -1;
}

int udp_bind(int fd, u16 port) {
    if (fd < 0 || fd >= UDP_MAX_SOCKETS || !g_udp_socks[fd].active) return -1;
    g_udp_socks[fd].local_port = port;
    return 0;
}

int udp_sendto(int fd, const void *data, usize len, u32 dst_ip, u16 dst_port) {
    if (fd < 0 || fd >= UDP_MAX_SOCKETS || !g_udp_socks[fd].active) return -1;

    usize total = sizeof(udp_hdr_t) + len;
    u8 *buf = (u8 *)kmalloc(total);
    if (!buf) return -1;

    udp_hdr_t *h = (udp_hdr_t *)buf;
    h->src_port = htons(g_udp_socks[fd].local_port);
    h->dst_port = htons(dst_port);
    h->length   = htons((u16)total);
    h->checksum = 0;
    memcpy(buf + sizeof(udp_hdr_t), data, len);
    h->checksum = udp_checksum(g_net_iface.ip_addr, dst_ip, h, total);

    ip_send(dst_ip, IP_PROTO_UDP, buf, total);
    kfree(buf);
    return (int)len;
}

void udp_receive(const ip_hdr_t *ip, const udp_hdr_t *udp, const void *data, usize len) {
    u16 dst_port = ntohs(udp->dst_port);
    u32 src_ip   = ntohl(ip->src_ip);
    u16 src_port = ntohs(udp->src_port);

    /* Dispatch to callback */
    for (int i = 0; i < g_udp_cb_count; i++) {
        if (g_udp_cbs[i].port == dst_port) {
            g_udp_cbs[i].cb(src_ip, src_port, data, len);
            return;
        }
    }

    /* Dispatch to socket */
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (g_udp_socks[i].active && g_udp_socks[i].local_port == dst_port) {
            usize copy = len < UDP_RECV_BUF ? len : UDP_RECV_BUF;
            memcpy(g_udp_socks[i].recv_buf, data, copy);
            g_udp_socks[i].recv_len      = copy;
            g_udp_socks[i].recv_src_ip   = src_ip;
            g_udp_socks[i].recv_src_port = src_port;
            g_udp_socks[i].has_data      = true;
            return;
        }
    }
}
