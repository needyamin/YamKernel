/* ============================================================================
 * YamKernel — POSIX socket.h (User-Space libc)
 * ============================================================================ */
#pragma once
#include "../libyam/syscall.h"

#define AF_INET  2
#define SOCK_STREAM   1
#define SOCK_DGRAM    2
#define SOCK_NONBLOCK 0x800
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

/* Errors */
#define EAGAIN      11
#define EWOULDBLOCK 11
#define EINPROGRESS 115

/* fcntl */
#define F_GETFL  3
#define F_SETFL  4
#define O_NONBLOCK 0x800

/* fd_set for select() — supports up to 128 FDs */
#define FD_SETSIZE 128
typedef struct { unsigned long long bits[2]; } fd_set;
#define FD_ZERO(s)   do { (s)->bits[0]=0; (s)->bits[1]=0; } while(0)
#define FD_SET(fd,s) ((s)->bits[(fd)/64] |=  (1ULL << ((fd)%64)))
#define FD_CLR(fd,s) ((s)->bits[(fd)/64] &= ~(1ULL << ((fd)%64)))
#define FD_ISSET(fd,s) (!!((s)->bits[(fd)/64] & (1ULL << ((fd)%64))))

static inline int select(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, long timeout_ms) {
    (void)efds;
    return (int)syscall4(SYS_SELECT, (u64)nfds, (u64)rfds, (u64)wfds, (u64)timeout_ms);
}

static inline int fcntl(int fd, int cmd, long arg) {
    return (int)syscall3(SYS_FCNTL, (u64)fd, (u64)cmd, (u64)arg);
}

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
    return (long)syscall3(SYS_WRITE_FD, (u64)fd, (u64)buf, (u64)len);
}
static inline long recv(int fd, void *buf, unsigned long len, int flags) {
    (void)flags;
    return (long)syscall3(SYS_READ, (u64)fd, (u64)buf, (u64)len);
}
static inline long sendto(int fd, const void *buf, unsigned long len, int flags,
                          const struct sockaddr *addr, socklen_t addrlen) {
    return (long)syscall6(SYS_SENDTO, (u64)fd, (u64)buf, (u64)len,
                          (u64)flags, (u64)addr, (u64)addrlen);
}
static inline long recvfrom(int fd, void *buf, unsigned long len, int flags,
                            struct sockaddr *addr, socklen_t *addrlen) {
    return (long)syscall6(SYS_RECVFROM, (u64)fd, (u64)buf, (u64)len,
                          (u64)flags, (u64)addr, (u64)addrlen);
}
