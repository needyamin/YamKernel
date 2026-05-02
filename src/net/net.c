/* ============================================================================
 * YamKernel — Networking Stack Framework
 * ============================================================================ */
#include "net.h"
#include "../lib/kprintf.h"
#include "../nexus/graph.h"

static net_interface_t loopback_if;

void e1000_init(void);
void e1000_poll(void);
void iwlwifi_init(void);
void hci_init(void);

void net_init(void) {
    kprintf_color(0xFF00DDFF, "[NET] Initializing Network Protocol Stack...\n");
    
    net_arp_init();
    net_ip_init();
    net_tcp_init();
    net_udp_init();
    net_icmp_init();
    net_dhcp_init();
    net_dns_init();
    net_http_init();
    net_tls_init();

    /* Initialize Hardware Drivers */
    e1000_init();
    iwlwifi_init();
    hci_init();

    /* Setup loopback interface mock */
    loopback_if.ip_addr = 0x7F000001; /* 127.0.0.1 */
    loopback_if.is_up = true;

    /* Register net in YamGraph */
    yam_node_id_t net_node = yamgraph_node_create(YAM_NODE_NAMESPACE, "net", NULL);
    yamgraph_edge_link(0, net_node, YAM_EDGE_OWNS, YAM_PERM_ALL);
}

void net_poll(void) {
    e1000_poll();
}
