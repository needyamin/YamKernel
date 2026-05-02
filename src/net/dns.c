/* ============================================================================
 * YamKernel — DNS Resolver
 * A-record lookup via UDP port 53.
 * ============================================================================ */
#include "net.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../mem/heap.h"

#define DNS_PORT 53
#define DNS_BUF  512

typedef struct __attribute__((packed)) {
    u16 id;
    u16 flags;
    u16 qdcount;
    u16 ancount;
    u16 nscount;
    u16 arcount;
} dns_hdr_t;

static volatile u32  g_dns_result = 0;
static volatile bool g_dns_done   = false;
static volatile u16  g_dns_query_id = 0x1234;

static void dns_callback(u32 src_ip, u16 src_port, const void *data, usize len) {
    (void)src_ip; (void)src_port;
    if (len < sizeof(dns_hdr_t)) return;
    const dns_hdr_t *hdr = (const dns_hdr_t *)data;
    if (ntohs(hdr->id) != g_dns_query_id) return;
    if (!(ntohs(hdr->flags) & 0x8000)) return; /* Not a response */
    if (ntohs(hdr->ancount) == 0) { g_dns_done = true; return; }

    /* Skip question section */
    const u8 *p = (const u8 *)data + sizeof(dns_hdr_t);
    const u8 *end = (const u8 *)data + len;

    /* Skip QNAME (labels) */
    while (p < end && *p != 0) {
        if ((*p & 0xC0) == 0xC0) { p += 2; goto done_name; }
        p += *p + 1;
    }
    if (p < end) p++; /* null terminator */
done_name:
    p += 4; /* skip QTYPE and QCLASS */

    /* Parse first answer */
    while (p < end) {
        /* Skip NAME (pointer or label) */
        if (p >= end) break;
        if ((*p & 0xC0) == 0xC0) p += 2;
        else { while (p < end && *p) p += *p + 1; if (p < end) p++; }

        if (p + 10 > end) break;
        u16 rtype  = ((u16)p[0] << 8) | p[1];
        u16 rdlen  = ((u16)p[8] << 8) | p[9];
        p += 10;

        if (rtype == 1 && rdlen == 4) { /* A record */
            g_dns_result = ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
            g_dns_done = true;
            return;
        }
        p += rdlen;
    }
    g_dns_done = true;
}

void net_dns_init(void) {
    udp_register_callback(DNS_PORT + 1000, dns_callback); /* listen on ephemeral */
    kprintf_color(0xFF888888, "[DNS] Resolver ready\n");
}

int dns_resolve(const char *hostname, u32 *ip_out) {
    if (!hostname || !ip_out) return -1;
    if (!g_net_iface.is_up || !g_net_iface.dns_server) return -1;

    u8 buf[DNS_BUF];
    memset(buf, 0, sizeof(buf));

    dns_hdr_t *h = (dns_hdr_t *)buf;
    h->id      = htons(++g_dns_query_id);
    h->flags   = htons(0x0100); /* Standard query, recursion desired */
    h->qdcount = htons(1);

    /* Encode hostname as DNS labels */
    u8 *p = buf + sizeof(dns_hdr_t);
    const char *name = hostname;
    while (*name) {
        const char *dot = name;
        while (*dot && *dot != '.') dot++;
        usize llen = (usize)(dot - name);
        *p++ = (u8)llen;
        memcpy(p, name, llen);
        p += llen;
        if (*dot == '.') dot++;
        name = dot;
    }
    *p++ = 0; /* Null terminator */
    *p++ = 0; *p++ = 1; /* QTYPE = A */
    *p++ = 0; *p++ = 1; /* QCLASS = IN */
    usize query_len = (usize)(p - buf);

    g_dns_result = 0;
    g_dns_done   = false;

    int sock = udp_socket();
    udp_bind(sock, DNS_PORT + 1000);
    udp_sendto(sock, buf, query_len, g_net_iface.dns_server, DNS_PORT);

    /* Wait for response (up to 3s) */
    for (int i = 0; i < 3000000 && !g_dns_done; i++) {
        if ((i % 1000) == 0) net_poll();
        __asm__ volatile("pause");
    }

    if (!g_dns_done || !g_dns_result) return -1;
    *ip_out = g_dns_result;

    kprintf_color(0xFF00FF88, "[DNS] %s → %u.%u.%u.%u\n", hostname,
                  (g_dns_result>>24)&0xFF, (g_dns_result>>16)&0xFF,
                  (g_dns_result>>8)&0xFF,   g_dns_result&0xFF);
    return 0;
}
