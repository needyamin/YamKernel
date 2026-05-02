/* ============================================================================
 * YamKernel — IPv4 Implementation
 * ============================================================================ */
#include "net.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../mem/heap.h"

#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

net_interface_t g_net_iface = {0};
static u16 g_ip_id = 0;

u16 net_checksum(const void *data, usize len) {
    const u16 *p = (const u16 *)(const void *)data;
    u32 sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(u8 *)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)~sum;
}

static void checksum_add_be_bytes(u32 *sum, const u8 *data, usize len) {
    while (len > 1) {
        *sum += ((u16)data[0] << 8) | data[1];
        data += 2;
        len -= 2;
    }
    if (len) *sum += ((u16)data[0] << 8);
}

static u16 transport_checksum(u32 src_ip, u32 dst_ip, u8 proto, const void *data, usize len) {
    u32 sum = 0;
    u8 pseudo[12];
    pseudo[0] = (u8)(src_ip >> 24);
    pseudo[1] = (u8)(src_ip >> 16);
    pseudo[2] = (u8)(src_ip >> 8);
    pseudo[3] = (u8)src_ip;
    pseudo[4] = (u8)(dst_ip >> 24);
    pseudo[5] = (u8)(dst_ip >> 16);
    pseudo[6] = (u8)(dst_ip >> 8);
    pseudo[7] = (u8)dst_ip;
    pseudo[8] = 0;
    pseudo[9] = proto;
    pseudo[10] = (u8)(len >> 8);
    pseudo[11] = (u8)len;
    checksum_add_be_bytes(&sum, pseudo, sizeof(pseudo));
    checksum_add_be_bytes(&sum, (const u8 *)data, len);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return htons((u16)~sum);
}

u16 tcp_checksum(u32 src_ip, u32 dst_ip, const tcp_hdr_t *hdr, usize len) {
    return transport_checksum(src_ip, dst_ip, IP_PROTO_TCP, hdr, len);
}

u16 udp_checksum(u32 src_ip, u32 dst_ip, const udp_hdr_t *hdr, usize len) {
    return transport_checksum(src_ip, dst_ip, IP_PROTO_UDP, hdr, len);
}

void net_ip_init(void) {
    g_ip_id = 1;
    kprintf_color(0xFF888888, "[IP] IPv4 protocol layer ready\n");
}

void ip_send(u32 dst_ip, u8 proto, const void *payload, usize payload_len) {
    if (!g_net_iface.is_up || !g_net_iface.send) return;

    usize total = sizeof(eth_frame_t) + sizeof(ip_hdr_t) + payload_len;
    u8 *buf = (u8 *)kmalloc(total);
    if (!buf) return;
    memset(buf, 0, total);

    eth_frame_t *eth = (eth_frame_t *)buf;
    ip_hdr_t    *ip  = (ip_hdr_t *)(buf + ETH_HLEN);
    u8          *pld = (u8 *)(buf + ETH_HLEN + sizeof(ip_hdr_t));

    /* Ethernet header */
    u8 dst_mac[6];
    u32 next_hop = dst_ip;
    /* If not on local subnet, use gateway */
    if ((dst_ip & g_net_iface.netmask) != (g_net_iface.ip_addr & g_net_iface.netmask))
        next_hop = g_net_iface.gateway;

    if (dst_ip == 0xFFFFFFFFu || next_hop == 0xFFFFFFFFu) {
        memset(dst_mac, 0xFF, 6);
    } else if ((dst_ip & 0xF0000000u) == 0xE0000000u) {
        dst_mac[0] = 0x01;
        dst_mac[1] = 0x00;
        dst_mac[2] = 0x5E;
        dst_mac[3] = (u8)((dst_ip >> 16) & 0x7F);
        dst_mac[4] = (u8)((dst_ip >> 8) & 0xFF);
        dst_mac[5] = (u8)(dst_ip & 0xFF);
    } else if (!arp_lookup(next_hop, dst_mac)) {
        arp_send_request(next_hop);
        /* Retry after a brief wait. A real send queue comes later; poll RX so
           ARP replies can be consumed before this packet is sent. */
        for (int i = 0; i < 100000; i++) {
            if ((i % 1000) == 0) net_poll();
            __asm__ volatile("pause");
        }
        if (!arp_lookup(next_hop, dst_mac)) {
            /* Broadcast attempt if ARP failed */
            memset(dst_mac, 0xFF, 6);
        }
    }
    memcpy(eth->dst_mac, dst_mac, 6);
    memcpy(eth->src_mac, g_net_iface.mac_addr, 6);
    eth->ethertype = htons(ETH_TYPE_IP);

    /* IPv4 header */
    ip->ihl_ver   = 0x45;   /* version=4, IHL=5 */
    ip->dscp_ecn  = 0;
    ip->total_len = htons((u16)(sizeof(ip_hdr_t) + payload_len));
    ip->id        = htons(g_ip_id++);
    ip->flags_frag = htons(0x4000); /* Don't fragment */
    ip->ttl       = 64;
    ip->protocol  = proto;
    ip->checksum  = 0;
    ip->src_ip    = htonl(g_net_iface.ip_addr);
    ip->dst_ip    = htonl(dst_ip);
    ip->checksum  = net_checksum(ip, sizeof(ip_hdr_t));

    memcpy(pld, payload, payload_len);
    g_net_iface.send(buf, total);
    kfree(buf);
}

void ip_receive(const ip_hdr_t *hdr, usize total_len) {
    if (total_len < sizeof(ip_hdr_t)) return;
    if ((hdr->ihl_ver >> 4) != 4) return;
    u8 proto = hdr->protocol;
    usize hdr_len = (hdr->ihl_ver & 0x0F) * 4;
    if (hdr_len < sizeof(ip_hdr_t) || hdr_len > total_len) return;

    usize ip_total = ntohs(hdr->total_len);
    if (ip_total < hdr_len) return;
    if (ip_total > total_len) return;

    const u8 *payload = (const u8 *)hdr + hdr_len;
    usize payload_len = ip_total - hdr_len;

    switch (proto) {
        case IP_PROTO_ICMP:
            icmp_handle(hdr, (const icmp_hdr_t *)payload, payload_len);
            break;
        case IP_PROTO_TCP: {
            if (payload_len < sizeof(tcp_hdr_t)) break;
            const tcp_hdr_t *tcp = (const tcp_hdr_t *)payload;
            usize tcp_hdr_len = (tcp->data_offset >> 4) * 4;
            if (tcp_hdr_len < sizeof(tcp_hdr_t) || tcp_hdr_len > payload_len) break;
            tcp_receive(hdr, tcp, payload + tcp_hdr_len, payload_len - tcp_hdr_len);
            break;
        }
        case IP_PROTO_UDP:
            if (payload_len < sizeof(udp_hdr_t)) break;
            udp_receive(hdr, (const udp_hdr_t *)payload,
                        payload + sizeof(udp_hdr_t),
                        payload_len - sizeof(udp_hdr_t));
            break;
    }
}

void net_receive(const void *buf, usize len) {
    if (len < ETH_HLEN) return;
    const eth_frame_t *eth = (const eth_frame_t *)buf;
    u16 type = ntohs(eth->ethertype);
    const u8 *payload = (const u8 *)buf + ETH_HLEN;
    usize payload_len = len - ETH_HLEN;

    if (type == ETH_TYPE_ARP) {
        arp_handle(payload, payload_len);
    } else if (type == ETH_TYPE_IP) {
        if (payload_len >= sizeof(ip_hdr_t))
            ip_receive((const ip_hdr_t *)payload, payload_len);
    }
}
