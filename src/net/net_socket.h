/* ============================================================================
 * YamKernel — POSIX-compatible Socket API Types (Kernel Internal)
 * ============================================================================ */
#pragma once
#include <nexus/types.h>

/* Address families */
#define AF_INET     2
#define AF_INET6    10

/* Socket types */
#define SOCK_STREAM 1   /* TCP */
#define SOCK_DGRAM  2   /* UDP */
#define SOCK_RAW    3   /* Raw IP */

/* Protocol numbers */
#define IPPROTO_ICMP 1
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17

/* Socket states */
typedef enum {
    SOCK_STATE_CLOSED = 0,
    SOCK_STATE_LISTEN,
    SOCK_STATE_SYN_SENT,
    SOCK_STATE_SYN_RCVD,
    SOCK_STATE_ESTABLISHED,
    SOCK_STATE_FIN_WAIT1,
    SOCK_STATE_FIN_WAIT2,
    SOCK_STATE_CLOSE_WAIT,
    SOCK_STATE_CLOSING,
    SOCK_STATE_LAST_ACK,
    SOCK_STATE_TIME_WAIT,
} sock_state_t;

/* IPv4 address structure */
typedef struct {
    u32 s_addr; /* Network byte order */
} in_addr_t;

typedef struct {
    u16      sin_family;  /* AF_INET */
    u16      sin_port;    /* Network byte order */
    in_addr_t sin_addr;
    u8       sin_zero[8];
} sockaddr_in_t;

/* Generic socket address */
typedef struct {
    u16 sa_family;
    u8  sa_data[14];
} sockaddr_t;

/* Byte order helpers */
static inline u16 htons(u16 x) {
    return (u16)((x >> 8) | (x << 8));
}
static inline u32 htonl(u32 x) {
    return ((x & 0xFF000000) >> 24) |
           ((x & 0x00FF0000) >> 8)  |
           ((x & 0x0000FF00) << 8)  |
           ((x & 0x000000FF) << 24);
}
#define ntohs(x) htons(x)
#define ntohl(x) htonl(x)

/* IP address from a.b.c.d notation */
static inline u32 make_ip(u8 a, u8 b, u8 c, u8 d) {
    return ((u32)a << 24) | ((u32)b << 16) | ((u32)c << 8) | d;
}

/* Kernel socket syscall numbers */
#define SYS_SOCKET    200
#define SYS_BIND      201
#define SYS_CONNECT   202
#define SYS_LISTEN    203
#define SYS_ACCEPT    204
#define SYS_SEND      205
#define SYS_RECV      206
#define SYS_SENDTO    207
#define SYS_RECVFROM  208
#define SYS_SHUTDOWN  209
#define SYS_GETPEERNAME 210
