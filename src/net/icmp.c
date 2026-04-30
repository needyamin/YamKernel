/* ============================================================================
 * YamKernel — ICMP (Internet Control Message Protocol)
 * Echo request/reply (ping), destination unreachable.
 * ============================================================================ */
#include "net.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../mem/heap.h"

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

static volatile bool  g_ping_got_reply = false;
static volatile u16   g_ping_expected_seq = 0;
static volatile u32   g_ping_reply_time = 0;

void net_icmp_init(void) {
    kprintf_color(0xFF888888, "[ICMP] Protocol layer ready\n");
}

void icmp_handle(const ip_hdr_t *ip, const icmp_hdr_t *icmp, usize len) {
    if (len < sizeof(icmp_hdr_t)) return;

    if (icmp->type == ICMP_ECHO_REQUEST) {
        /* Respond with echo reply */
        usize reply_size = sizeof(icmp_hdr_t) + (len - sizeof(icmp_hdr_t));
        u8 *reply = (u8 *)kmalloc(reply_size);
        if (!reply) return;

        icmp_hdr_t *r = (icmp_hdr_t *)reply;
        r->type     = ICMP_ECHO_REPLY;
        r->code     = 0;
        r->checksum = 0;
        r->id       = icmp->id;
        r->seq      = icmp->seq;
        /* Copy payload */
        if (len > sizeof(icmp_hdr_t))
            memcpy(reply + sizeof(icmp_hdr_t),
                   (u8 *)icmp + sizeof(icmp_hdr_t),
                   len - sizeof(icmp_hdr_t));
        r->checksum = net_checksum(reply, reply_size);

        u32 src = ntohl(ip->src_ip);
        ip_send(src, IP_PROTO_ICMP, reply, reply_size);
        kfree(reply);
    } else if (icmp->type == ICMP_ECHO_REPLY) {
        if (ntohs(icmp->seq) == g_ping_expected_seq) {
            g_ping_got_reply = true;
        }
    }
}

int icmp_ping(u32 dst_ip, u16 seq, u32 timeout_ms) {
    usize pkt_size = sizeof(icmp_hdr_t) + 32; /* 32-byte payload */
    u8 *pkt = (u8 *)kmalloc(pkt_size);
    if (!pkt) return -1;
    memset(pkt, 0, pkt_size);

    icmp_hdr_t *h = (icmp_hdr_t *)pkt;
    h->type     = ICMP_ECHO_REQUEST;
    h->code     = 0;
    h->id       = htons(0xCAFE);
    h->seq      = htons(seq);
    h->checksum = 0;
    /* Fill payload with pattern */
    for (usize i = sizeof(icmp_hdr_t); i < pkt_size; i++) pkt[i] = (u8)i;
    h->checksum = net_checksum(pkt, pkt_size);

    g_ping_got_reply   = false;
    g_ping_expected_seq = seq;

    ip_send(dst_ip, IP_PROTO_ICMP, pkt, pkt_size);
    kfree(pkt);

    /* Poll for reply with timeout */
    for (u32 t = 0; t < timeout_ms * 100; t++) {
        if (g_ping_got_reply) return 0;
        __asm__ volatile("pause");
    }
    return -1; /* Timeout */
}
