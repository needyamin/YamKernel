/* YamKernel — DHCP Protocol Skeleton */
#include "../lib/kprintf.h"
#include "net.h"

void net_dhcp_init(void) { kprintf_color(0xFF888888, "[NET] DHCP protocol layer active\n"); }
