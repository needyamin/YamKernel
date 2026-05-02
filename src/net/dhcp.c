/* ============================================================================
 * YamKernel — DHCP Client
 * Sends DISCOVER → receives OFFER → sends REQUEST → receives ACK.
 * ============================================================================ */
#include "net.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../mem/heap.h"

#define DHCP_SERVER_PORT  67
#define DHCP_CLIENT_PORT  68
#define DHCP_MAGIC        0x63825363
#define DHCP_DISCOVER     1
#define DHCP_OFFER        2
#define DHCP_REQUEST      3
#define DHCP_ACK          5

typedef struct __attribute__((packed)) {
    u8  op;
    u8  htype;   /* 1 = Ethernet */
    u8  hlen;    /* 6 */
    u8  hops;
    u32 xid;     /* Transaction ID */
    u16 secs;
    u16 flags;   /* 0x8000 = broadcast */
    u32 ciaddr;  /* Client IP */
    u32 yiaddr;  /* Your IP (offered) */
    u32 siaddr;  /* Server IP */
    u32 giaddr;  /* Gateway IP */
    u8  chaddr[16]; /* Client hardware addr */
    u8  sname[64];
    u8  file[128];
    u32 magic;
    u8  options[308];
} dhcp_pkt_t;

static u32  g_dhcp_xid     = 0xBEEF1234;
static u32  g_dhcp_offered = 0;
static u32  g_dhcp_server  = 0;
static bool g_dhcp_done    = false;
static bool g_dhcp_request_sent = false;

static void dhcp_build_options(u8 *opts, u8 msg_type, u32 requested_ip, u32 server_ip) {
    int i = 0;
    /* Message Type */
    opts[i++] = 53; opts[i++] = 1; opts[i++] = msg_type;
    /* Client Identifier */
    opts[i++] = 61; opts[i++] = 7; opts[i++] = 1;
    memcpy(opts + i, g_net_iface.mac_addr, 6); i += 6;
    if (requested_ip) {
        /* Requested IP Address */
        opts[i++] = 50; opts[i++] = 4;
        opts[i++] = (u8)(requested_ip >> 24); opts[i++] = (u8)(requested_ip >> 16);
        opts[i++] = (u8)(requested_ip >> 8);  opts[i++] = (u8)requested_ip;
    }
    if (server_ip) {
        /* Server Identifier */
        opts[i++] = 54; opts[i++] = 4;
        opts[i++] = (u8)(server_ip >> 24); opts[i++] = (u8)(server_ip >> 16);
        opts[i++] = (u8)(server_ip >> 8);  opts[i++] = (u8)server_ip;
    }
    /* Parameter Request List */
    opts[i++] = 55; opts[i++] = 4;
    opts[i++] = 1;  /* Subnet mask */
    opts[i++] = 3;  /* Router */
    opts[i++] = 6;  /* DNS */
    opts[i++] = 15; /* Domain name */
    /* End */
    opts[i++] = 255;
}

static void dhcp_send(u8 msg_type, u32 requested_ip, u32 server_ip) {
    if (!g_net_iface.is_up || !g_net_iface.send) {
        kprintf_color(0xFFFF8800, "[DHCP] Cannot send: network interface is down\n");
        return;
    }

    dhcp_pkt_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.op    = 1; /* BOOTREQUEST */
    pkt.htype = 1;
    pkt.hlen  = 6;
    pkt.xid   = htonl(g_dhcp_xid);
    pkt.flags = htons(0x8000); /* Broadcast */
    memcpy(pkt.chaddr, g_net_iface.mac_addr, 6);
    pkt.magic = htonl(DHCP_MAGIC);
    dhcp_build_options(pkt.options, msg_type, requested_ip, server_ip);

    int sock = udp_socket();
    udp_bind(sock, DHCP_CLIENT_PORT);
    udp_sendto(sock, &pkt, sizeof(pkt), 0xFFFFFFFF, DHCP_SERVER_PORT);
    kprintf("[DHCP] TX message type=%u xid=0x%x\n", msg_type, g_dhcp_xid);
}

void dhcp_receive(u32 src_ip, u16 src_port, const void *data, usize len) {
    (void)src_port;
    if (len < sizeof(dhcp_pkt_t)) return;
    const dhcp_pkt_t *pkt = (const dhcp_pkt_t *)data;
    if (ntohl(pkt->magic) != DHCP_MAGIC) return;
    if (ntohl(pkt->xid) != g_dhcp_xid) return;

    /* Parse options */
    u8 msg_type = 0;
    u32 subnet = 0, gateway = 0, dns = 0;
    const u8 *opts = pkt->options;
    const u8 *end  = opts + 308;
    while (opts < end && *opts != 255) {
        u8 code = *opts++;
        if (code == 0) continue;
        u8 l = *opts++;
        if (code == 53 && l == 1)  msg_type = opts[0];
        if (code == 1  && l == 4)  subnet   = ((u32)opts[0]<<24)|((u32)opts[1]<<16)|((u32)opts[2]<<8)|opts[3];
        if (code == 3  && l >= 4)  gateway  = ((u32)opts[0]<<24)|((u32)opts[1]<<16)|((u32)opts[2]<<8)|opts[3];
        if (code == 6  && l >= 4)  dns      = ((u32)opts[0]<<24)|((u32)opts[1]<<16)|((u32)opts[2]<<8)|opts[3];
        opts += l;
    }

    u32 offered_ip = ntohl(pkt->yiaddr);

    if (msg_type == DHCP_OFFER) {
        if (g_dhcp_request_sent) return;
        g_dhcp_offered = offered_ip;
        g_dhcp_server  = src_ip;
        kprintf_color(0xFF00DDFF, "[DHCP] OFFER: %u.%u.%u.%u from server %u.%u.%u.%u\n",
                      (offered_ip>>24)&0xFF, (offered_ip>>16)&0xFF,
                      (offered_ip>>8)&0xFF,   offered_ip&0xFF,
                      (src_ip>>24)&0xFF, (src_ip>>16)&0xFF, (src_ip>>8)&0xFF, src_ip&0xFF);
        /* Send REQUEST */
        g_dhcp_request_sent = true;
        dhcp_send(DHCP_REQUEST, g_dhcp_offered, g_dhcp_server);
    } else if (msg_type == DHCP_ACK) {
        g_net_iface.ip_addr    = offered_ip;
        g_net_iface.netmask    = subnet ? subnet : 0xFFFFFF00;
        g_net_iface.gateway    = gateway;
        g_net_iface.dns_server = dns;
        g_net_iface.dhcp_done  = true;
        g_dhcp_done = true;

        kprintf_color(0xFF00FF88,
            "[DHCP] ACK! IP=%u.%u.%u.%u GW=%u.%u.%u.%u DNS=%u.%u.%u.%u\n",
            (offered_ip>>24)&0xFF,(offered_ip>>16)&0xFF,(offered_ip>>8)&0xFF,offered_ip&0xFF,
            (gateway>>24)&0xFF,(gateway>>16)&0xFF,(gateway>>8)&0xFF,gateway&0xFF,
            (dns>>24)&0xFF,(dns>>16)&0xFF,(dns>>8)&0xFF,dns&0xFF);
    }
}

void net_dhcp_init(void) {
    kprintf_color(0xFF888888, "[DHCP] Client ready\n");
    udp_register_callback(DHCP_CLIENT_PORT, dhcp_receive);
}

void dhcp_start(void) {
    g_dhcp_offered = 0;
    g_dhcp_server = 0;
    g_dhcp_done = false;
    g_dhcp_request_sent = false;
    kprintf_color(0xFF00DDFF, "[DHCP] Sending DISCOVER...\n");
    dhcp_send(DHCP_DISCOVER, 0, 0);
    /* Wait up to 5 seconds */
    for (int i = 0; i < 5000000 && !g_dhcp_done; i++) {
        if ((i % 1000) == 0) net_poll();
        __asm__ volatile("pause");
    }
    if (!g_dhcp_done)
        kprintf_color(0xFFFF8800, "[DHCP] Timeout — no server responded\n");
}
