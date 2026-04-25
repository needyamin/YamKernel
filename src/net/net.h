/* ============================================================================
 * YamKernel — Networking Stack Framework
 * Defines the skeleton interfaces for TCP, IP, UDP, ICMP, ARP.
 * ============================================================================ */
#ifndef _NET_NET_H
#define _NET_NET_H

#include <nexus/types.h>

typedef struct {
    u8 mac_addr[6];
    u32 ip_addr;
    u32 netmask;
    u32 gateway;
    bool is_up;
} net_interface_t;

void net_init(void);
void net_arp_init(void);
void net_ip_init(void);
void net_tcp_init(void);
void net_udp_init(void);
void net_icmp_init(void);
void net_dhcp_init(void);
void net_dns_init(void);

#endif
