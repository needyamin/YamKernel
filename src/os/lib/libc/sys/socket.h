/* ============================================================================
 * YamKernel — POSIX socket.h (User-Space libc)
 * ============================================================================ */
#pragma once
#include "../libyam/syscall.h"

#define AF_INET  2
#define SOCK_STREAM 1
#define SOCK_DGRAM  2

typedef unsigned int  socklen_t;
typedef unsigned short sa_family_t;

struct in_addr { unsigned int s_addr; };
struct sockaddr_in {
    sa_family_t    sin_family;
    unsigned short sin_port;
    struct in_addr sin_addr;
    unsigned char  sin_zero[8];
};
struct sockaddr { sa_family_t sa_family; char sa_data[14]; };

static inline unsigned short htons(unsigned short x) { return (unsigned short)((x>>8)|(x<<8)); }
static inline unsigned int   htonl(unsigned int x)   {
    return ((x&0xFF000000)>>24)|((x&0x00FF0000)>>8)|((x&0x0000FF00)<<8)|((x&0x000000FF)<<24);
}
#define ntohs(x) htons(x)
#define ntohl(x) htonl(x)

static inline unsigned int inet_addr(const char *s) {
    unsigned int a=0, b=0, c=0, d=0;
    while (*s>='0'&&*s<='9') { a=a*10+(*s++)-'0'; } s++;
    while (*s>='0'&&*s<='9') { b=b*10+(*s++)-'0'; } s++;
    while (*s>='0'&&*s<='9') { c=c*10+(*s++)-'0'; } s++;
    while (*s>='0'&&*s<='9') { d=d*10+(*s++)-'0'; }
    return htonl((a<<24)|(b<<16)|(c<<8)|d);
}

static inline int socket(int domain, int type, int proto) {
    return (int)syscall3(SYS_SOCKET, (u64)domain, (u64)type, (u64)proto);
}
static inline int bind(int fd, const struct sockaddr *addr, socklen_t len) {
    return (int)syscall3(SYS_BIND, (u64)fd, (u64)addr, (u64)len);
}
static inline int connect(int fd, const struct sockaddr *addr, socklen_t len) {
    return (int)syscall3(SYS_CONNECT, (u64)fd, (u64)addr, (u64)len);
}
static inline int listen(int fd, int backlog) {
    return (int)syscall2(SYS_LISTEN, (u64)fd, (u64)backlog);
}
static inline int accept(int fd, struct sockaddr *addr, socklen_t *len) {
    return (int)syscall3(SYS_ACCEPT, (u64)fd, (u64)addr, (u64)len);
}
static inline long send(int fd, const void *buf, unsigned long len, int flags) {
    (void)flags;
    return (long)syscall3(SYS_WRITE, (u64)fd, (u64)buf, (u64)len);
}
static inline long recv(int fd, void *buf, unsigned long len, int flags) {
    (void)flags;
    return (long)syscall3(SYS_READ, (u64)fd, (u64)buf, (u64)len);
}
