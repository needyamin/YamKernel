/* ============================================================================
 * YamKernel — Network Stack Header (Full)
 * ============================================================================ */
#ifndef _NET_NET_H
#define _NET_NET_H

#include <nexus/types.h>
#include "net_socket.h"

/* ---- MAC / Ethernet ---- */
#define ETH_HLEN        14
#define ETH_ALEN         6
#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806

/* ---- IP Protocol Numbers ---- */
#define IP_PROTO_ICMP    1
#define IP_PROTO_TCP     6
#define IP_PROTO_UDP    17

/* ---- Network Interface ---- */
typedef struct {
    u8  mac_addr[6];
    u32 ip_addr;     /* Host byte order: 192.168.1.1 */
    u32 netmask;
    u32 gateway;
    u32 dns_server;
    bool is_up;
    bool dhcp_done;
    /* Send packet callback — filled in by the driver */
    void (*send)(const void *buf, usize len);
} net_interface_t;

/* Global default interface */
extern net_interface_t g_net_iface;

/* ---- Ethernet Frame ---- */
typedef struct __attribute__((packed)) {
    u8  dst_mac[6];
    u8  src_mac[6];
    u16 ethertype;  /* big-endian */
    u8  payload[];
} eth_frame_t;

/* ---- IPv4 Header ---- */
typedef struct __attribute__((packed)) {
    u8  ihl_ver;    /* version(4) | IHL(4) */
    u8  dscp_ecn;
    u16 total_len;
    u16 id;
    u16 flags_frag;
    u8  ttl;
    u8  protocol;
    u16 checksum;
    u32 src_ip;
    u32 dst_ip;
} ip_hdr_t;

/* ---- ICMP Header ---- */
typedef struct __attribute__((packed)) {
    u8  type;
    u8  code;
    u16 checksum;
    u16 id;
    u16 seq;
} icmp_hdr_t;

/* ---- UDP Header ---- */
typedef struct __attribute__((packed)) {
    u16 src_port;
    u16 dst_port;
    u16 length;
    u16 checksum;
} udp_hdr_t;

/* ---- TCP Header ---- */
typedef struct __attribute__((packed)) {
    u16 src_port;
    u16 dst_port;
    u32 seq;
    u32 ack;
    u8  data_offset; /* high 4 bits = offset in 32-bit words */
    u8  flags;
    u16 window;
    u16 checksum;
    u16 urgent;
} tcp_hdr_t;

/* TCP flags */
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10
#define TCP_URG 0x20

/* ---- ARP Packet ---- */
typedef struct __attribute__((packed)) {
    u16 htype;   /* 1 = Ethernet */
    u16 ptype;   /* 0x0800 = IPv4 */
    u8  hlen;    /* 6 */
    u8  plen;    /* 4 */
    u16 oper;    /* 1=request, 2=reply */
    u8  sha[6];  /* sender hardware addr */
    u32 spa;     /* sender protocol addr */
    u8  tha[6];  /* target hardware addr */
    u32 tpa;     /* target protocol addr */
} arp_pkt_t;

/* ---- Checksum ---- */
u16 net_checksum(const void *data, usize len);
u16 tcp_checksum(u32 src_ip, u32 dst_ip, const tcp_hdr_t *hdr, usize len);
u16 udp_checksum(u32 src_ip, u32 dst_ip, const udp_hdr_t *hdr, usize len);

/* ---- Init functions ---- */
void net_init(void);
void net_arp_init(void);
void net_ip_init(void);
void net_tcp_init(void);
void net_udp_init(void);
void net_icmp_init(void);
void net_dhcp_init(void);
void net_dns_init(void);

/* ---- Packet receive dispatch (called by driver on RX) ---- */
void net_receive(const void *buf, usize len);

/* ---- ARP API ---- */
bool arp_lookup(u32 ip, u8 mac_out[6]);
void arp_send_request(u32 target_ip);
void arp_handle(const void *buf, usize len);

/* ---- IP API ---- */
void ip_send(u32 dst_ip, u8 proto, const void *payload, usize payload_len);
void ip_receive(const ip_hdr_t *hdr, usize total_len);

/* ---- ICMP API ---- */
void icmp_handle(const ip_hdr_t *ip, const icmp_hdr_t *icmp, usize len);
int  icmp_ping(u32 dst_ip, u16 seq, u32 timeout_ms);

/* ---- UDP API ---- */
typedef void (*udp_callback_t)(u32 src_ip, u16 src_port, const void *data, usize len);
int  udp_socket(void);
int  udp_bind(int fd, u16 port);
int  udp_sendto(int fd, const void *data, usize len, u32 dst_ip, u16 dst_port);
void udp_register_callback(u16 port, udp_callback_t cb);
void udp_receive(const ip_hdr_t *ip, const udp_hdr_t *udp, const void *data, usize len);

/* ---- TCP API ---- */
int  tcp_socket(void);
int  tcp_connect(int fd, u32 dst_ip, u16 dst_port);
int  tcp_listen(int fd, u16 port);
int  tcp_accept(int fd);
int  tcp_send(int fd, const void *data, usize len);
int  tcp_recv(int fd, void *buf, usize max_len);
int  tcp_close(int fd);
void tcp_receive(const ip_hdr_t *ip, const tcp_hdr_t *tcp, const void *data, usize data_len);

/* ---- DHCP API ---- */
void dhcp_start(void);
void dhcp_receive(u32 src_ip, u16 src_port, const void *data, usize len);

/* ---- DNS API ---- */
int dns_resolve(const char *hostname, u32 *ip_out);

#endif
