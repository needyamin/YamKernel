/* ============================================================================
 * YamKernel — Full TCP State Machine
 * RFC 793: SYN, SYN-ACK, ACK, data transfer, FIN, RST, retransmit.
 * ============================================================================ */
#include "net.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../mem/heap.h"

#define TCP_MAX_SOCKETS  32
#define TCP_RECV_BUF     16384
#define TCP_SEND_BUF     8192
#define TCP_WINDOW_SIZE  8192
#define TCP_RETRANSMIT_MS 500
#define TCP_MAX_RETRIES  5

typedef struct {
    bool          active;
    sock_state_t  state;
    u32           local_ip;
    u16           local_port;
    u32           remote_ip;
    u16           remote_port;

    /* Sequence numbers */
    u32           snd_una;   /* Oldest unacknowledged sequence number */
    u32           snd_nxt;   /* Next byte to send */
    u32           snd_isn;   /* Initial send sequence number */
    u32           rcv_nxt;   /* Next expected receive sequence number */
    u32           rcv_isn;   /* Initial receive sequence number */
    u16           snd_wnd;   /* Remote receive window */

    /* Receive buffer */
    u8            recv_buf[TCP_RECV_BUF];
    usize         recv_head;
    usize         recv_tail;

    /* Send buffer */
    u8            send_buf[TCP_SEND_BUF];
    usize         send_len;

    /* Retransmit */
    int           retransmit_count;
    bool          waiting_syn_ack;
    bool          waiting_fin_ack;
    bool          nonblocking;  /* O_NONBLOCK / SOCK_NONBLOCK */

    /* Accept queue (for listening sockets) */
    bool          listening;
    int           accept_queue[8];
    int           accept_head;
    int           accept_tail;
} tcp_sock_t;

static tcp_sock_t g_tcp_socks[TCP_MAX_SOCKETS];
static u32 g_tcp_isn_counter = 0xDEAD0001;
static u16 g_ephemeral_port = 32768;

/* ---- Helpers ---- */

static void tcp_send_segment(tcp_sock_t *s, u8 flags,
                              const void *data, usize data_len) {
    usize tcp_len = sizeof(tcp_hdr_t) + data_len;
    u8 *buf = (u8 *)kmalloc(tcp_len);
    if (!buf) return;

    tcp_hdr_t *h = (tcp_hdr_t *)buf;
    h->src_port   = htons(s->local_port);
    h->dst_port   = htons(s->remote_port);
    h->seq        = htonl(s->snd_nxt);
    h->ack        = (flags & TCP_ACK) ? htonl(s->rcv_nxt) : 0;
    h->data_offset = (sizeof(tcp_hdr_t) / 4) << 4;
    h->flags      = flags;
    h->window     = htons(TCP_WINDOW_SIZE);
    h->checksum   = 0;
    h->urgent     = 0;

    if (data && data_len) memcpy(buf + sizeof(tcp_hdr_t), data, data_len);
    h->checksum = tcp_checksum(s->local_ip, s->remote_ip, h, tcp_len);

    ip_send(s->remote_ip, IP_PROTO_TCP, buf, tcp_len);

    /* Advance snd_nxt for data and SYN/FIN (each consumes 1 seq) */
    if (data_len) s->snd_nxt += (u32)data_len;
    if (flags & TCP_SYN) s->snd_nxt++;
    if (flags & TCP_FIN) s->snd_nxt++;

    kfree(buf);
}

static tcp_sock_t *tcp_find_sock(u32 remote_ip, u16 remote_port, u16 local_port) {
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (!g_tcp_socks[i].active) continue;
        if (g_tcp_socks[i].local_port == local_port &&
            (g_tcp_socks[i].remote_ip   == remote_ip || g_tcp_socks[i].listening) &&
            (g_tcp_socks[i].remote_port == remote_port || g_tcp_socks[i].listening))
            return &g_tcp_socks[i];
    }
    return NULL;
}

static tcp_sock_t *tcp_find_listener(u16 local_port) {
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (g_tcp_socks[i].active && g_tcp_socks[i].listening &&
            g_tcp_socks[i].local_port == local_port)
            return &g_tcp_socks[i];
    }
    return NULL;
}

/* ---- Public API ---- */

void net_tcp_init(void) {
    memset(g_tcp_socks, 0, sizeof(g_tcp_socks));
    kprintf_color(0xFF888888, "[TCP] Full state machine ready (%d sockets)\n", TCP_MAX_SOCKETS);
}

int tcp_socket(void) {
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (!g_tcp_socks[i].active) {
            memset(&g_tcp_socks[i], 0, sizeof(tcp_sock_t));
            g_tcp_socks[i].active = true;
            g_tcp_socks[i].state  = SOCK_STATE_CLOSED;
            g_tcp_socks[i].local_ip = g_net_iface.ip_addr;
            return i;
        }
    }
    return -1;
}

int tcp_socket_nonblock(void) {
    int fd = tcp_socket();
    if (fd >= 0) g_tcp_socks[fd].nonblocking = true;
    return fd;
}

void tcp_set_nonblock(int fd, bool nonblock) {
    if (fd >= 0 && fd < TCP_MAX_SOCKETS) g_tcp_socks[fd].nonblocking = nonblock;
}

bool tcp_is_nonblock(int fd) {
    if (fd < 0 || fd >= TCP_MAX_SOCKETS) return false;
    return g_tcp_socks[fd].nonblocking;
}

int tcp_connect(int fd, u32 dst_ip, u16 dst_port) {
    if (fd < 0 || fd >= TCP_MAX_SOCKETS || !g_tcp_socks[fd].active) return -1;
    tcp_sock_t *s = &g_tcp_socks[fd];

    s->local_port  = g_ephemeral_port++;
    if (g_ephemeral_port == 0) g_ephemeral_port = 32768;
    s->remote_ip   = dst_ip;
    s->remote_port = dst_port;
    s->snd_isn     = g_tcp_isn_counter;
    g_tcp_isn_counter += 64521; /* Pseudo-random increment */
    s->snd_una     = s->snd_isn;
    s->snd_nxt     = s->snd_isn;
    s->state       = SOCK_STATE_SYN_SENT;
    s->waiting_syn_ack = true;

    /* Send SYN */
    tcp_send_segment(s, TCP_SYN, NULL, 0);

    /* Non-blocking: return EINPROGRESS immediately */
    if (s->nonblocking) {
        return -EINPROGRESS;
    }

    /* Blocking: wait for SYN-ACK */
    for (int i = 0; i < 8000000 && s->waiting_syn_ack; i++) {
        if ((i % 100) == 0) net_poll();
        __asm__ volatile("pause");
    }

    if (s->state != SOCK_STATE_ESTABLISHED) {
        kprintf("[TCP] connect timeout/fail dst=%u.%u.%u.%u:%u state=%u snd_nxt=%u rcv_nxt=%u\n",
                (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
                (dst_ip >> 8) & 0xFF, dst_ip & 0xFF,
                dst_port, s->state, s->snd_nxt, s->rcv_nxt);
        return -1;
    }
    return 0;
}

int tcp_listen(int fd, u16 port) {
    if (fd < 0 || fd >= TCP_MAX_SOCKETS || !g_tcp_socks[fd].active) return -1;
    g_tcp_socks[fd].local_port = port;
    g_tcp_socks[fd].listening  = true;
    g_tcp_socks[fd].state      = SOCK_STATE_LISTEN;
    g_tcp_socks[fd].accept_head = 0;
    g_tcp_socks[fd].accept_tail = 0;
    return 0;
}

int tcp_accept(int fd) {
    if (fd < 0 || fd >= TCP_MAX_SOCKETS || !g_tcp_socks[fd].active) return -1;
    tcp_sock_t *ls = &g_tcp_socks[fd];

    if (ls->nonblocking) {
        if (ls->accept_head == ls->accept_tail) return -EAGAIN;
    } else {
        /* Blocking: wait for a connection in the accept queue */
        while (ls->accept_head == ls->accept_tail)
            __asm__ volatile("pause");
    }

    int client_fd = ls->accept_queue[ls->accept_tail];
    ls->accept_tail = (ls->accept_tail + 1) % 8;
    return client_fd;
}

int tcp_send(int fd, const void *data, usize len) {
    if (fd < 0 || fd >= TCP_MAX_SOCKETS || !g_tcp_socks[fd].active) return -1;
    tcp_sock_t *s = &g_tcp_socks[fd];
    if (s->state != SOCK_STATE_ESTABLISHED) return -1;
    tcp_send_segment(s, TCP_PSH | TCP_ACK, data, len);
    return (int)len;
}

int tcp_recv(int fd, void *buf, usize max_len) {
    if (fd < 0 || fd >= TCP_MAX_SOCKETS) return -1;
    tcp_sock_t *s = &g_tcp_socks[fd];
    if (!s->active && s->recv_head == s->recv_tail) return -1;

    /* Non-blocking: return EAGAIN if no data available */
    if (s->nonblocking) {
        if (s->recv_head == s->recv_tail) return -EAGAIN;
    } else {
        /* Blocking: wait for data */
        for (int i = 0; i < 1000000; i++) {
            if (s->recv_head != s->recv_tail) break;
            if (s->state == SOCK_STATE_CLOSE_WAIT || s->state == SOCK_STATE_CLOSED) return 0;
            if ((i % 1000) == 0) net_poll();
            __asm__ volatile("pause");
        }
    }

    usize avail = 0;
    if (s->recv_head >= s->recv_tail)
        avail = s->recv_head - s->recv_tail;
    else
        avail = TCP_RECV_BUF - s->recv_tail + s->recv_head;

    usize copy = avail < max_len ? avail : max_len;
    if (copy == 0) return 0;

    /* Linear copy (simplified — ignores wrap-around for brevity) */
    usize linear = TCP_RECV_BUF - s->recv_tail;
    if (linear >= copy) {
        memcpy(buf, s->recv_buf + s->recv_tail, copy);
    } else {
        memcpy(buf, s->recv_buf + s->recv_tail, linear);
        memcpy((u8 *)buf + linear, s->recv_buf, copy - linear);
    }
    s->recv_tail = (s->recv_tail + copy) % TCP_RECV_BUF;
    return (int)copy;
}

int tcp_available(int fd) {
    if (fd < 0 || fd >= TCP_MAX_SOCKETS || !g_tcp_socks[fd].active) return 0;
    tcp_sock_t *s = &g_tcp_socks[fd];
    if (s->recv_head >= s->recv_tail)
        return (int)(s->recv_head - s->recv_tail);
    return (int)(TCP_RECV_BUF - s->recv_tail + s->recv_head);
}

int tcp_close(int fd) {
    if (fd < 0 || fd >= TCP_MAX_SOCKETS || !g_tcp_socks[fd].active) return -1;
    tcp_sock_t *s = &g_tcp_socks[fd];
    if (s->state == SOCK_STATE_ESTABLISHED || s->state == SOCK_STATE_CLOSE_WAIT) {
        s->state = SOCK_STATE_FIN_WAIT1;
        tcp_send_segment(s, TCP_FIN | TCP_ACK, NULL, 0);
    }
    /* Wait for close to complete */
    for (int i = 0; i < 1000000 && s->state != SOCK_STATE_CLOSED; i++) {
        if ((i % 1000) == 0) net_poll();
        __asm__ volatile("pause");
    }
    s->active = false;
    return 0;
}

/* ---- Receive Handler (called from ip_receive) ---- */

void tcp_receive(const ip_hdr_t *ip, const tcp_hdr_t *tcp,
                 const void *data, usize data_len) {
    u32 src_ip   = ntohl(ip->src_ip);
    u16 src_port = ntohs(tcp->src_port);
    u16 dst_port = ntohs(tcp->dst_port);
    u8  flags    = tcp->flags;
    u32 seq      = ntohl(tcp->seq);
    u32 ack_num  = ntohl(tcp->ack);

    /* Find socket */
    tcp_sock_t *s = tcp_find_sock(src_ip, src_port, dst_port);
    if (!s && (flags & (TCP_SYN | TCP_ACK | TCP_RST))) {
        kprintf("[TCP] no socket for %u.%u.%u.%u:%u -> local:%u flags=0x%x\n",
                (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                (src_ip >> 8) & 0xFF, src_ip & 0xFF,
                src_port, dst_port, flags);
    }

    /* Handle SYN to a listening socket */
    if ((flags & TCP_SYN) && !(flags & TCP_ACK) && !s) {
        tcp_sock_t *ls = tcp_find_listener(dst_port);
        if (!ls) return;

        /* Allocate new connection socket */
        int new_fd = tcp_socket();
        if (new_fd < 0) return;
        tcp_sock_t *ns = &g_tcp_socks[new_fd];
        ns->local_port  = dst_port;
        ns->remote_ip   = src_ip;
        ns->remote_port = src_port;
        ns->rcv_isn     = seq;
        ns->rcv_nxt     = seq + 1;
        ns->snd_isn     = g_tcp_isn_counter;
        g_tcp_isn_counter += 64521;
        ns->snd_una     = ns->snd_isn;
        ns->snd_nxt     = ns->snd_isn;
        ns->state       = SOCK_STATE_SYN_RCVD;

        /* Send SYN-ACK */
        tcp_send_segment(ns, TCP_SYN | TCP_ACK, NULL, 0);

        /* Add to listener's accept queue */
        int next = (ls->accept_head + 1) % 8;
        if (next != ls->accept_tail) {
            ls->accept_queue[ls->accept_head] = new_fd;
            ls->accept_head = next;
        }
        return;
    }

    if (!s) return;

    /* SYN-ACK: complete client 3-way handshake */
    if ((flags & TCP_SYN) && (flags & TCP_ACK) && s->state == SOCK_STATE_SYN_SENT) {
        s->rcv_isn = seq;
        s->rcv_nxt = seq + 1;
        s->snd_una = ack_num;
        s->snd_wnd = ntohs(tcp->window);
        s->state   = SOCK_STATE_ESTABLISHED;
        s->waiting_syn_ack = false;
        /* Send ACK */
        tcp_send_segment(s, TCP_ACK, NULL, 0);
        kprintf_color(0xFF00FF88, "[TCP] Connected to %u.%u.%u.%u:%u\n",
                      (src_ip>>24)&0xFF, (src_ip>>16)&0xFF,
                      (src_ip>>8)&0xFF, src_ip&0xFF, src_port);
        return;
    }

    /* ACK: advance snd_una */
    if (flags & TCP_ACK) {
        if (ack_num > s->snd_una) s->snd_una = ack_num;
        if (s->state == SOCK_STATE_SYN_RCVD) {
            s->state = SOCK_STATE_ESTABLISHED;
        } else if (s->state == SOCK_STATE_FIN_WAIT1) {
            s->state = SOCK_STATE_FIN_WAIT2;
        } else if (s->state == SOCK_STATE_LAST_ACK) {
            s->state = SOCK_STATE_CLOSED;
            s->active = false;
        }
    }

    /* Data */
    if (data_len > 0 && s->state == SOCK_STATE_ESTABLISHED) {
        if (seq == s->rcv_nxt) {
            /* In-order data — copy to receive buffer */
            const u8 *src = (const u8 *)data;
            for (usize i = 0; i < data_len; i++) {
                usize next_head = (s->recv_head + 1) % TCP_RECV_BUF;
                if (next_head == s->recv_tail) break; /* Buffer full */
                s->recv_buf[s->recv_head] = src[i];
                s->recv_head = next_head;
            }
            s->rcv_nxt += (u32)data_len;
        }
        /* Send ACK */
        tcp_send_segment(s, TCP_ACK, NULL, 0);
    }

    /* FIN: remote is closing */
    if (flags & TCP_FIN) {
        s->rcv_nxt++;
        tcp_send_segment(s, TCP_ACK, NULL, 0);
        if (s->state == SOCK_STATE_ESTABLISHED) {
            s->state = SOCK_STATE_CLOSE_WAIT;
        } else if (s->state == SOCK_STATE_FIN_WAIT2) {
            s->state = SOCK_STATE_TIME_WAIT;
            /* In real TCP we'd wait 2*MSL; simplified: close immediately */
            s->state = SOCK_STATE_CLOSED;
            s->active = false;
        }
    }

    /* RST */
    if (flags & TCP_RST) {
        s->state = SOCK_STATE_CLOSED;
        s->active = false;
    }
}
