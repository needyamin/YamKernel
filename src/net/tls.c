/* ============================================================================
 * YamKernel - Minimal TLS/Certificate Service
 *
 * This is not yet a full HTTPS implementation. It provides the first real
 * platform capability slice: a boot-time certificate-store service and a
 * bounded outbound TLS probe that sends a ClientHello and verifies that the
 * peer answers with a TLS ServerHello handshake.
 * ============================================================================ */
#include "net.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

#define TLS_PORT 443
#define TLS_RECV_MAX 512

typedef struct {
    const char *name;
    const char *subject;
    const char *spki_sha256;
} cert_anchor_t;

static const cert_anchor_t g_trust_anchors[] = {
    {
        "YamOS WebPKI Bootstrap Root",
        "CN=YamOS WebPKI Bootstrap Root,O=YamOS",
        "bootstrap-development-anchor"
    },
};

static bool g_cert_store_ready = false;
static bool g_tls_ready = false;

static usize append_u8(u8 *dst, usize cap, usize pos, u8 v) {
    if (pos < cap) dst[pos] = v;
    return pos + 1;
}

static usize append_be16(u8 *dst, usize cap, usize pos, u16 v) {
    pos = append_u8(dst, cap, pos, (u8)(v >> 8));
    pos = append_u8(dst, cap, pos, (u8)(v & 0xFF));
    return pos;
}

static usize append_bytes(u8 *dst, usize cap, usize pos, const void *src, usize len) {
    const u8 *p = (const u8 *)src;
    for (usize i = 0; i < len; i++) pos = append_u8(dst, cap, pos, p[i]);
    return pos;
}

static usize build_client_hello(const char *host, u8 *out, usize cap) {
    if (!host || !*host || !out || cap < 128) return 0;

    u8 body[256];
    usize b = 0;

    /* legacy_version TLS 1.2 */
    b = append_be16(body, sizeof(body), b, 0x0303);

    /* Random: deterministic but nonzero for this early kernel probe. */
    for (u8 i = 0; i < 32; i++) b = append_u8(body, sizeof(body), b, (u8)(0xA0 + i));

    /* session_id */
    b = append_u8(body, sizeof(body), b, 0);

    /* TLS 1.2 cipher_suites. Keep the probe conservative: no TLS 1.3
     * supported_versions/key_share until the real key exchange exists. */
    b = append_be16(body, sizeof(body), b, 12);
    b = append_be16(body, sizeof(body), b, 0xC02F); /* ECDHE_RSA_AES_128_GCM_SHA256 */
    b = append_be16(body, sizeof(body), b, 0xC02B); /* ECDHE_ECDSA_AES_128_GCM_SHA256 */
    b = append_be16(body, sizeof(body), b, 0xC030); /* ECDHE_RSA_AES_256_GCM_SHA384 */
    b = append_be16(body, sizeof(body), b, 0x009E); /* DHE_RSA_AES_128_GCM_SHA256 */
    b = append_be16(body, sizeof(body), b, 0x009C); /* RSA_AES_128_GCM_SHA256 */
    b = append_be16(body, sizeof(body), b, 0x003C); /* RSA_AES_128_CBC_SHA256 */

    /* compression_methods: null */
    b = append_u8(body, sizeof(body), b, 1);
    b = append_u8(body, sizeof(body), b, 0);

    u8 extensions[192];
    usize e = 0;

    /* server_name extension */
    usize host_len = strlen(host);
    if (host_len > 80) host_len = 80;
    e = append_be16(extensions, sizeof(extensions), e, 0x0000);
    e = append_be16(extensions, sizeof(extensions), e, (u16)(host_len + 5));
    e = append_be16(extensions, sizeof(extensions), e, (u16)(host_len + 3));
    e = append_u8(extensions, sizeof(extensions), e, 0);
    e = append_be16(extensions, sizeof(extensions), e, (u16)host_len);
    e = append_bytes(extensions, sizeof(extensions), e, host, host_len);

    /* supported_groups: x25519, secp256r1 */
    static const u8 groups[] = { 0x00, 0x1D, 0x00, 0x17 };
    e = append_be16(extensions, sizeof(extensions), e, 0x000A);
    e = append_be16(extensions, sizeof(extensions), e, sizeof(groups) + 2);
    e = append_be16(extensions, sizeof(extensions), e, sizeof(groups));
    e = append_bytes(extensions, sizeof(extensions), e, groups, sizeof(groups));

    /* ec_point_formats: uncompressed */
    static const u8 ec_points[] = { 0x01, 0x00 };
    e = append_be16(extensions, sizeof(extensions), e, 0x000B);
    e = append_be16(extensions, sizeof(extensions), e, sizeof(ec_points));
    e = append_bytes(extensions, sizeof(extensions), e, ec_points, sizeof(ec_points));

    /* signature_algorithms: rsa_pss_rsae_sha256, ecdsa_secp256r1_sha256, rsa_pkcs1_sha256 */
    static const u8 sigs[] = { 0x08, 0x04, 0x04, 0x03, 0x04, 0x01 };
    e = append_be16(extensions, sizeof(extensions), e, 0x000D);
    e = append_be16(extensions, sizeof(extensions), e, sizeof(sigs) + 2);
    e = append_be16(extensions, sizeof(extensions), e, sizeof(sigs));
    e = append_bytes(extensions, sizeof(extensions), e, sigs, sizeof(sigs));

    b = append_be16(body, sizeof(body), b, (u16)e);
    b = append_bytes(body, sizeof(body), b, extensions, e);

    usize p = 0;
    usize hs_len = b;
    usize record_len = hs_len + 4;

    p = append_u8(out, cap, p, 0x16);     /* handshake record */
    p = append_be16(out, cap, p, 0x0301); /* legacy record version */
    p = append_be16(out, cap, p, (u16)record_len);
    p = append_u8(out, cap, p, 0x01);     /* ClientHello */
    p = append_u8(out, cap, p, (u8)((hs_len >> 16) & 0xFF));
    p = append_u8(out, cap, p, (u8)((hs_len >> 8) & 0xFF));
    p = append_u8(out, cap, p, (u8)(hs_len & 0xFF));
    p = append_bytes(out, cap, p, body, b);

    return p <= cap ? p : 0;
}

bool cert_store_ready(void) {
    return g_cert_store_ready;
}

u32 cert_store_anchor_count(void) {
    return (u32)(sizeof(g_trust_anchors) / sizeof(g_trust_anchors[0]));
}

bool tls_service_ready(void) {
    return g_tls_ready && g_cert_store_ready;
}

void net_tls_init(void) {
    g_cert_store_ready = cert_store_anchor_count() > 0;
    g_tls_ready = g_cert_store_ready;
    kprintf("[CERT] store ready: anchors=%u policy=bootstrap\n", cert_store_anchor_count());
    kprintf("[TLS] service ready: client_hello=1 sni=1 cert_store=%s full_https=pending\n",
            g_cert_store_ready ? "ready" : "missing");
}

int tls_probe_host(const char *host) {
    if (!tls_service_ready()) return -1;
    if (!host || !*host) return -2;
    if (!g_net_iface.is_up || !g_net_iface.dhcp_done || !g_net_iface.dns_server) return -3;

    u32 ip = 0;
    if (dns_resolve(host, &ip) != 0) {
        kprintf("[TLS] probe blocked: DNS resolve %s failed\n", host);
        return -4;
    }

    int fd = tcp_socket();
    if (fd < 0) return -5;
    if (tcp_connect(fd, ip, TLS_PORT) != 0) {
        kprintf("[TLS] probe blocked: connect %s:443 failed\n", host);
        tcp_close(fd);
        return -6;
    }

    u8 hello[384];
    usize hello_len = build_client_hello(host, hello, sizeof(hello));
    if (hello_len == 0 || tcp_send(fd, hello, hello_len) < 0) {
        tcp_close(fd);
        return -7;
    }

    u8 buf[TLS_RECV_MAX];
    int got = -1;
    for (int tries = 0; tries < 24; tries++) {
        got = tcp_recv(fd, buf, sizeof(buf));
        if (got > 0) break;
        if (got < 0) {
            tcp_close(fd);
            return -8;
        }
    }

    tcp_close(fd);
    if (got < 6) {
        kprintf("[TLS] probe blocked: no handshake response host=%s got=%d\n", host, got);
        return -9;
    }

    bool is_tls_record = buf[0] == 0x16;
    bool version_ok = buf[1] == 0x03 && (buf[2] >= 0x01 && buf[2] <= 0x04);
    if (!is_tls_record || !version_ok) {
        kprintf("[TLS] probe blocked: unexpected record host=%s type=0x%x version=%u.%u\n",
                host, buf[0], buf[1], buf[2]);
        return -10;
    }

    if (buf[5] != 0x02) {
        kprintf("[TLS] probe blocked: expected ServerHello host=%s handshake=0x%x\n",
                host, buf[5]);
        return -11;
    }

    kprintf("[TLS] probe ok: host=%s record=0x%x handshake=server_hello version=0x%02x%02x bytes=%d\n",
            host, buf[0], buf[1], buf[2], got);
    return 0;
}
