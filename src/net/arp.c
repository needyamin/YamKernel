/* ============================================================================
 * YamKernel — ARP (Address Resolution Protocol)
 * ARP cache, request/reply, packet dispatch.
 * ============================================================================ */
#include "net.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../mem/heap.h"

#define ARP_CACHE_SIZE 64
#define ARP_TIMEOUT_MS 5000

typedef struct {
    u32  ip;
    u8   mac[6];
    bool valid;
    u32  age_ms;
} arp_entry_t;

static arp_entry_t g_arp_cache[ARP_CACHE_SIZE];

void net_arp_init(void) {
    memset(g_arp_cache, 0, sizeof(g_arp_cache));
    kprintf_color(0xFF888888, "[ARP] Cache initialized (%d entries)\n", ARP_CACHE_SIZE);
}

static arp_entry_t *arp_cache_find(u32 ip) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip)
            return &g_arp_cache[i];
    }
    return NULL;
}

static void arp_cache_add(u32 ip, const u8 mac[6]) {
    /* Find existing or empty slot */
    int oldest = 0;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!g_arp_cache[i].valid || g_arp_cache[i].ip == ip) {
            oldest = i; break;
        }
        if (g_arp_cache[i].age_ms > g_arp_cache[oldest].age_ms) oldest = i;
    }
    g_arp_cache[oldest].ip    = ip;
    g_arp_cache[oldest].valid = true;
    g_arp_cache[oldest].age_ms = 0;
    memcpy(g_arp_cache[oldest].mac, mac, 6);
}

bool arp_lookup(u32 ip, u8 mac_out[6]) {
    arp_entry_t *e = arp_cache_find(ip);
    if (e) { memcpy(mac_out, e->mac, 6); return true; }
    return false;
}

void arp_send_request(u32 target_ip) {
    if (!g_net_iface.is_up || !g_net_iface.send) return;

    u8 buf[sizeof(eth_frame_t) + sizeof(arp_pkt_t)];
    eth_frame_t *eth = (eth_frame_t *)buf;
    arp_pkt_t   *arp = (arp_pkt_t *)(buf + ETH_HLEN);

    /* Broadcast destination */
    memset(eth->dst_mac, 0xFF, 6);
    memcpy(eth->src_mac, g_net_iface.mac_addr, 6);
    eth->ethertype = htons(ETH_TYPE_ARP);

    arp->htype = htons(1);
    arp->ptype = htons(ETH_TYPE_IP);
    arp->hlen  = 6;
    arp->plen  = 4;
    arp->oper  = htons(1); /* Request */
    memcpy(arp->sha, g_net_iface.mac_addr, 6);
    arp->spa = htonl(g_net_iface.ip_addr);
    memset(arp->tha, 0, 6);
    arp->tpa = htonl(target_ip);

    g_net_iface.send(buf, sizeof(buf));
}

void arp_handle(const void *buf, usize len) {
    if (len < sizeof(arp_pkt_t)) return;
    const arp_pkt_t *arp = (const arp_pkt_t *)buf;
    if (ntohs(arp->htype) != 1 || ntohs(arp->ptype) != ETH_TYPE_IP) return;

    u32 sender_ip  = ntohl(arp->spa);
    /* Learn sender's MAC */
    arp_cache_add(sender_ip, arp->sha);

    u16 oper = ntohs(arp->oper);
    if (oper == 1) {
        /* ARP Request — are we the target? */
        u32 target_ip = ntohl(arp->tpa);
        if (target_ip == g_net_iface.ip_addr) {
            /* Send ARP reply */
            u8 reply_buf[sizeof(eth_frame_t) + sizeof(arp_pkt_t)];
            eth_frame_t *eth_r = (eth_frame_t *)reply_buf;
            arp_pkt_t   *arp_r = (arp_pkt_t *)(reply_buf + ETH_HLEN);

            memcpy(eth_r->dst_mac, arp->sha, 6);
            memcpy(eth_r->src_mac, g_net_iface.mac_addr, 6);
            eth_r->ethertype = htons(ETH_TYPE_ARP);

            arp_r->htype = htons(1);
            arp_r->ptype = htons(ETH_TYPE_IP);
            arp_r->hlen  = 6;
            arp_r->plen  = 4;
            arp_r->oper  = htons(2); /* Reply */
            memcpy(arp_r->sha, g_net_iface.mac_addr, 6);
            arp_r->spa = htonl(g_net_iface.ip_addr);
            memcpy(arp_r->tha, arp->sha, 6);
            arp_r->tpa = arp->spa;
            if (g_net_iface.send) g_net_iface.send(reply_buf, sizeof(reply_buf));
        }
    }
}
