/* ============================================================================
 * YamKernel - Minimal HTTP/1.0 Client
 * Kernel service used by the installer/downloader capability probe.
 * ============================================================================ */
#include "net.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

#define HTTP_PORT 80
#define HTTP_RECV_CHUNK 512

void net_http_init(void) {
    kprintf_color(0xFF888888, "[HTTP] Client service ready (plain HTTP)\n");
}

static usize append_text(char *dst, usize cap, usize pos, const char *src) {
    if (!dst || !src || cap == 0) return pos;
    while (*src && pos + 1 < cap) {
        dst[pos++] = *src++;
    }
    dst[pos] = 0;
    return pos;
}

int http_get(const char *host, const char *path, char *out, usize out_cap, usize *out_len) {
    if (out_len) *out_len = 0;
    if (!host || !*host || !path || !out || out_cap == 0) return -1;
    if (!g_net_iface.is_up || !g_net_iface.dhcp_done || !g_net_iface.dns_server) return -2;

    u32 ip = 0;
    if (dns_resolve(host, &ip) != 0) {
        kprintf("[HTTP] dns failed host=%s\n", host);
        return -3;
    }

    int fd = tcp_socket();
    if (fd < 0) return -4;
    if (tcp_connect(fd, ip, HTTP_PORT) != 0) {
        kprintf("[HTTP] connect failed host=%s ip=%u.%u.%u.%u\n",
                host, (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
        tcp_close(fd);
        return -5;
    }

    char req[384];
    usize n = 0;
    n = append_text(req, sizeof(req), n, "GET ");
    n = append_text(req, sizeof(req), n, path);
    n = append_text(req, sizeof(req), n, " HTTP/1.0\r\nHost: ");
    n = append_text(req, sizeof(req), n, host);
    n = append_text(req, sizeof(req), n, "\r\nUser-Agent: YamOS/0.4 kernel-http\r\nConnection: close\r\n\r\n");

    if (tcp_send(fd, req, n) < 0) {
        tcp_close(fd);
        return -6;
    }

    usize total = 0;
    for (int tries = 0; tries < 32 && total + 1 < out_cap; tries++) {
        char chunk[HTTP_RECV_CHUNK];
        int got = tcp_recv(fd, chunk, sizeof(chunk));
        if (got < 0) {
            tcp_close(fd);
            return -7;
        }
        if (got == 0) {
            if (total > 0) break;
            continue;
        }
        usize copy = (usize)got;
        if (copy > out_cap - total - 1) copy = out_cap - total - 1;
        memcpy(out + total, chunk, copy);
        total += copy;
        out[total] = 0;
        if ((usize)got < sizeof(chunk)) break;
    }

    tcp_close(fd);
    if (out_len) *out_len = total;
    if (total == 0) return -8;
    kprintf("[HTTP] GET http://%s%s -> %lu bytes\n", host, path, total);
    return 0;
}
