/* ============================================================================
 * YamKernel — HTTPS fetch over mbed TLS + kernel TCP (TLS 1.2/1.3 client).
 * ============================================================================ */

#include "net.h"

#include "../drivers/timer/pit.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include "psa/crypto.h"

/* ISRG Root X1 (Let's Encrypt) — see src/net/certs/isrgrootx1.pem, embedded via xxd */
extern unsigned char isrgrootx1_pem[];
extern unsigned int isrgrootx1_pem_len;

#define HTTPS_TLS_PORT 443

typedef struct {
    u64 t0_ticks;
    u32 int_ms;
    u32 fin_ms;
} yssl_timer_t;

static int ssl_send_cb(void *ctx, const unsigned char *buf, size_t len) {
    int fd = (int)(intptr_t)ctx;
    int r = tcp_send(fd, buf, (usize)len);
    if (r < 0)
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    return r;
}

static int ssl_recv_cb(void *ctx, unsigned char *buf, size_t len) {
    int fd = (int)(intptr_t)ctx;
    int r = tcp_recv(fd, buf, (usize)len);
    if (r < 0)
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    return r;
}

static void yssl_set_timer(void *ctx, uint32_t int_ms, uint32_t fin_ms) {
    yssl_timer_t *t = (yssl_timer_t *)ctx;
    t->int_ms = int_ms;
    t->fin_ms = fin_ms;
    if (fin_ms == 0) {
        t->t0_ticks = 0;
        return;
    }
    t->t0_ticks = pit_get_ticks();
}

static int yssl_get_timer(void *ctx) {
    yssl_timer_t *t = (yssl_timer_t *)ctx;
    if (t->fin_ms == 0)
        return -1;

    u32 hz = pit_get_frequency();
    if (hz == 0)
        hz = 1000;
    u64 elapsed_ms =
        ((pit_get_ticks() - t->t0_ticks) * 1000ULL + hz - 1) / (u64)hz;

    if (elapsed_ms >= (u64)t->fin_ms)
        return 2;
    if (elapsed_ms >= (u64)t->int_ms)
        return 1;
    return 0;
}

static int https_read_body(mbedtls_ssl_context *ssl, char *out, usize out_cap,
                              usize *out_len) {
    usize total = 0;
    int tries = 0;

    while (total + 1 < out_cap && tries < 64000) {
        tries++;
        int ret =
            mbedtls_ssl_read(ssl, (unsigned char *)out + total, out_cap - total - 1);
        if (ret > 0) {
            total += (usize)ret;
            out[total] = 0;
            continue;
        }
        if (ret == 0)
            break;
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            net_poll();
            net_coop_yield((u32)tries);
            continue;
        }
        return ret;
    }

    if (out_len)
        *out_len = total;
    return 0;
}

static int https_write_req(mbedtls_ssl_context *ssl, const char *req) {
    usize len = strlen(req);
    usize off = 0;
    u32 spin = 0;

    while (off < len) {
        int ret =
            mbedtls_ssl_write(ssl, (const unsigned char *)req + off, len - off);
        if (ret > 0) {
            off += (usize)ret;
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            net_poll();
            net_coop_yield(spin++);
            continue;
        }
        return ret;
    }
    return 0;
}

int https_get(const char *host, const char *path, char *out, usize out_cap,
              usize *out_len) {
    if (out_len)
        *out_len = 0;
    if (!host || !*host || !path || !out || out_cap == 0)
        return -1;
    if (!g_net_iface.is_up || !g_net_iface.dhcp_done || !g_net_iface.dns_server)
        return -2;

    u32 ip = 0;
    if (dns_resolve(host, &ip) != 0) {
        kprintf("[HTTPS] dns failed host=%s\n", host);
        return -3;
    }

    int fd = tcp_socket();
    if (fd < 0)
        return -4;

    if (tcp_connect(fd, ip, HTTPS_TLS_PORT) != 0) {
        kprintf("[HTTPS] tcp connect failed host=%s\n", host);
        tcp_close(fd);
        return -5;
    }

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt cacert;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;

    mbedtls_x509_crt_init(&cacert);
    {
        u8 ca_buf[2048];
        if (isrgrootx1_pem_len + 1 > sizeof(ca_buf)) {
            kprintf("[HTTPS] embedded CA PEM too large\n");
            tcp_close(fd);
            mbedtls_x509_crt_free(&cacert);
            return -10;
        }
        memcpy(ca_buf, isrgrootx1_pem, (usize)isrgrootx1_pem_len);
        ca_buf[isrgrootx1_pem_len] = 0;
        int pc = mbedtls_x509_crt_parse(&cacert, ca_buf, isrgrootx1_pem_len + 1);
        if (pc != 0) {
            char eb[88];
            mbedtls_strerror(pc, eb, sizeof(eb));
            kprintf("[HTTPS] CA parse failed: %s (-0x%04x)\n", eb, -pc);
            tcp_close(fd);
            mbedtls_x509_crt_free(&cacert);
            return -11;
        }
    }

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);

    static const unsigned char pers[] = "yamkernel_https_get";
    int drbg_rc = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                          pers, sizeof(pers) - 1);
    if (drbg_rc != 0) {
        kprintf("[HTTPS] ctr_drbg_seed failed: %d\n", drbg_rc);
        goto cleanup;
    }

    int cfg_rc =
        mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (cfg_rc != 0) {
        kprintf("[HTTPS] ssl_config_defaults failed: %d\n", cfg_rc);
        goto cleanup;
    }

    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    int setup_rc = mbedtls_ssl_setup(&ssl, &conf);
    if (setup_rc != 0) {
        kprintf("[HTTPS] ssl_setup failed: %d\n", setup_rc);
        goto cleanup;
    }

    mbedtls_ssl_set_hostname(&ssl, host);
    mbedtls_ssl_set_bio(&ssl, (void *)(intptr_t)fd, ssl_send_cb, ssl_recv_cb, NULL);

    yssl_timer_t tm;
    memset(&tm, 0, sizeof(tm));
    mbedtls_ssl_set_timer_cb(&ssl, &tm, yssl_set_timer, yssl_get_timer);

    int hs = mbedtls_ssl_handshake(&ssl);
    u32 hs_spin = 0;
    while (hs != 0) {
        if (hs != MBEDTLS_ERR_SSL_WANT_READ && hs != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char errbuf[96];
            mbedtls_strerror(hs, errbuf, sizeof(errbuf));
            kprintf("[HTTPS] handshake failed: %s (-0x%04x)\n", errbuf, -hs);
            mbedtls_ssl_close_notify(&ssl);
            goto cleanup;
        }
        net_poll();
        net_coop_yield(hs_spin++);
        hs = mbedtls_ssl_handshake(&ssl);
    }

    char req[384];
    usize rn = (usize)ksnprintf(req, sizeof(req),
                                  "GET %s HTTP/1.0\r\nHost: %s\r\n"
                                  "User-Agent: YamOS/0.4 kernel-https\r\n"
                                  "Connection: close\r\n\r\n",
                                  path, host);
    (void)rn;

    int wr = https_write_req(&ssl, req);
    if (wr != 0) {
        char errbuf[96];
        mbedtls_strerror(wr, errbuf, sizeof(errbuf));
        kprintf("[HTTPS] write failed: %s (-0x%04x)\n", errbuf, -wr);
        mbedtls_ssl_close_notify(&ssl);
        goto cleanup;
    }

    usize total = 0;
    int rr = https_read_body(&ssl, out, out_cap, &total);
    if (rr != 0) {
        char errbuf[96];
        mbedtls_strerror(rr, errbuf, sizeof(errbuf));
        kprintf("[HTTPS] read failed: %s (-0x%04x)\n", errbuf, -rr);
        mbedtls_ssl_close_notify(&ssl);
        goto cleanup;
    }

    mbedtls_ssl_close_notify(&ssl);

    if (out_len)
        *out_len = total;
    kprintf("[HTTPS] GET https://%s%s -> %lu bytes\n", host, path, total);

    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_x509_crt_free(&cacert);
    tcp_close(fd);
    return total == 0 ? -8 : 0;

cleanup:
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_x509_crt_free(&cacert);
    tcp_close(fd);
    return -9;
}
