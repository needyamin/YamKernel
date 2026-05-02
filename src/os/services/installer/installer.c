#include "installer.h"
#include "lib/string.h"
#include "lib/kprintf.h"
#include "net/net.h"
#include "fs/vfs.h"

#define INSTALLER_HTTP_PROBE_HOST "example.com"
#define INSTALLER_HTTP_PROBE_PATH "/"
#define INSTALLER_HTTP_CACHE_PROBE "/var/cache/yamos/downloads/kernel-net-probe.txt"

typedef struct {
    const char *name;
    const char *display_name;
    const char *official_url;
    const char *install_path;
    u32 state;
    u32 last_error;
    u32 progress;
} installer_package_t;

static installer_package_t g_packages[] = {
    {
        .name = "kernel-net",
        .display_name = "Kernel Network Stack",
        .official_url = "https://example.com/",
        .install_path = "/var/cache/yamos/downloads/kernel-net-probe.txt",
        .state = YAM_INSTALL_STATE_AVAILABLE,
        .last_error = YAM_INSTALL_ERR_NONE,
        .progress = 0,
    },
};

static bool g_installer_ready = false;
static bool g_runtime_tcp_probe_done = false;
static bool g_runtime_tcp_probe_ok = false;
static bool g_runtime_http_probe_done = false;
static bool g_runtime_http_probe_ok = false;
static bool g_runtime_cache_probe_ok = false;
static bool g_runtime_tls_probe_done = false;
static bool g_runtime_tls_probe_ok = false;

static installer_package_t *find_package(const char *name) {
    if (!name || !*name) name = "kernel-net";
    for (usize i = 0; i < sizeof(g_packages) / sizeof(g_packages[0]); i++) {
        if (strcmp(name, g_packages[i].name) == 0) return &g_packages[i];
    }
    return NULL;
}

static void copy_text(char *dst, usize cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) src = "";
    usize i = 0;
    for (; i + 1 < cap && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static u32 current_caps(void) {
    u32 caps = YAM_INSTALL_CAP_PACKAGE_DB;
    if (g_runtime_cache_probe_ok) caps |= YAM_INSTALL_CAP_TEMP_STORAGE;
    if (g_runtime_cache_probe_ok) caps |= YAM_INSTALL_CAP_PERSISTENT_STORAGE;
    if (g_net_iface.is_up) caps |= YAM_INSTALL_CAP_NET_IFACE;
    if (g_net_iface.dhcp_done) caps |= YAM_INSTALL_CAP_DHCP;
    if (g_net_iface.dns_server != 0) caps |= YAM_INSTALL_CAP_DNS;
    if (g_runtime_tcp_probe_ok) caps |= YAM_INSTALL_CAP_TCP_CONNECT;
    if (g_runtime_http_probe_ok) caps |= YAM_INSTALL_CAP_HTTP_CLIENT;
    if (cert_store_ready()) caps |= YAM_INSTALL_CAP_CERT_STORE;
    if (g_runtime_tls_probe_ok) caps |= YAM_INSTALL_CAP_HTTPS_TLS;
    return caps;
}

static u32 missing_for_runtime(u32 caps) {
    u32 missing = 0;
    if (!(caps & YAM_INSTALL_CAP_NET_IFACE)) missing |= YAM_INSTALL_CAP_NET_IFACE;
    if (!(caps & YAM_INSTALL_CAP_DHCP)) missing |= YAM_INSTALL_CAP_DHCP;
    if (!(caps & YAM_INSTALL_CAP_DNS)) missing |= YAM_INSTALL_CAP_DNS;
    if (!(caps & YAM_INSTALL_CAP_TCP_CONNECT)) missing |= YAM_INSTALL_CAP_TCP_CONNECT;
    if (!(caps & YAM_INSTALL_CAP_HTTP_CLIENT)) missing |= YAM_INSTALL_CAP_HTTP_CLIENT;
    if (!(caps & YAM_INSTALL_CAP_HTTPS_TLS)) missing |= YAM_INSTALL_CAP_HTTPS_TLS;
    if (!(caps & YAM_INSTALL_CAP_CERT_STORE)) missing |= YAM_INSTALL_CAP_CERT_STORE;
    if (!(caps & YAM_INSTALL_CAP_PERSISTENT_STORAGE)) missing |= YAM_INSTALL_CAP_PERSISTENT_STORAGE;
    return missing;
}

static const char *blocker_text(u32 missing) {
    if (missing & YAM_INSTALL_CAP_NET_IFACE) return "network interface not ready";
    if (missing & YAM_INSTALL_CAP_DHCP) return "DHCP lease not ready";
    if (missing & YAM_INSTALL_CAP_DNS) return "DNS resolver not ready";
    if (missing & YAM_INSTALL_CAP_TCP_CONNECT) return "TCP client connect path not validated";
    if (missing & YAM_INSTALL_CAP_HTTP_CLIENT) return "HTTP client service missing";
    if (missing & YAM_INSTALL_CAP_HTTPS_TLS) return "HTTPS/TLS service missing";
    if (missing & YAM_INSTALL_CAP_CERT_STORE) return "certificate store missing";
    if (missing & YAM_INSTALL_CAP_PERSISTENT_STORAGE) return "persistent package storage missing";
    return "ready";
}

static void probe_runtime_tcp(void) {
    if (g_runtime_tcp_probe_done || !g_net_iface.dhcp_done || !g_net_iface.dns_server) return;
    g_runtime_tcp_probe_done = true;

    u32 ip = 0;
    if (dns_resolve(INSTALLER_HTTP_PROBE_HOST, &ip) != 0) {
        kprintf("[INSTALLER] tcp probe blocked: DNS resolve %s failed\n", INSTALLER_HTTP_PROBE_HOST);
        return;
    }

    int fd = tcp_socket();
    if (fd < 0) {
        kprintf("[INSTALLER] tcp probe blocked: tcp_socket failed\n");
        return;
    }

    if (tcp_connect(fd, ip, 80) == 0) {
        g_runtime_tcp_probe_ok = true;
        tcp_close(fd);
        kprintf("[INSTALLER] tcp probe ok: outbound TCP reachable\n");
    } else {
        kprintf("[INSTALLER] tcp probe blocked: connect %s:80 failed\n", INSTALLER_HTTP_PROBE_HOST);
    }
}

static void probe_runtime_http(void) {
    if (g_runtime_http_probe_done || !g_runtime_tcp_probe_ok) return;
    g_runtime_http_probe_done = true;

    char buf[1024];
    usize len = 0;
    int rc = http_get(INSTALLER_HTTP_PROBE_HOST, INSTALLER_HTTP_PROBE_PATH, buf, sizeof(buf), &len);
    if (rc == 0 && len >= 12 && strncmp(buf, "HTTP/", 5) == 0) {
        g_runtime_http_probe_ok = true;
        char first_line[64];
        usize i = 0;
        for (; i + 1 < sizeof(first_line) && buf[i] && buf[i] != '\r' && buf[i] != '\n'; i++) {
            first_line[i] = buf[i];
        }
        first_line[i] = 0;
        kprintf("[INSTALLER] http probe ok: response='%s'\n", first_line);
        int fd = sys_open(INSTALLER_HTTP_CACHE_PROBE, 0x40 | 0x0200 | 0x0001);
        if (fd >= 0) {
            isize written = sys_write(fd, buf, len);
            sys_close(fd);
            if (written == (isize)len) {
                g_runtime_cache_probe_ok = true;
                kprintf("[INSTALLER] cache probe ok: wrote %lu bytes to %s\n",
                        len, INSTALLER_HTTP_CACHE_PROBE);
            } else {
                kprintf("[INSTALLER] cache probe blocked: write=%ld len=%lu path=%s\n",
                        written, len, INSTALLER_HTTP_CACHE_PROBE);
            }
        } else {
            kprintf("[INSTALLER] cache probe blocked: open failed path=%s\n",
                    INSTALLER_HTTP_CACHE_PROBE);
        }
    } else {
        kprintf("[INSTALLER] http probe blocked rc=%d len=%lu\n", rc, len);
    }
}

static void probe_runtime_tls(void) {
    if (g_runtime_tls_probe_done || !g_runtime_tcp_probe_ok) return;
    g_runtime_tls_probe_done = true;
    int rc = tls_probe_host(INSTALLER_HTTP_PROBE_HOST);
    if (rc == 0) {
        g_runtime_tls_probe_ok = true;
        kprintf("[INSTALLER] tls probe ok: outbound TLS ServerHello reachable\n");
    } else {
        kprintf("[INSTALLER] tls probe blocked rc=%d\n", rc);
    }
}

static void fill_status(installer_package_t *pkg, yam_installer_status_t *out) {
    memset(out, 0, sizeof(*out));
    if (!pkg) {
        out->state = YAM_INSTALL_STATE_FAILED;
        out->last_error = YAM_INSTALL_ERR_UNKNOWN_PACKAGE;
        copy_text(out->message, sizeof(out->message), "unknown package");
        return;
    }

    u32 caps = current_caps();
    u32 missing = missing_for_runtime(caps);
    out->state = pkg->state;
    out->last_error = pkg->last_error;
    out->capabilities = caps;
    out->missing = missing;
    out->progress = pkg->progress;
    copy_text(out->package, sizeof(out->package), pkg->name);
    copy_text(out->display_name, sizeof(out->display_name), pkg->display_name);
    copy_text(out->official_url, sizeof(out->official_url), pkg->official_url);
    copy_text(out->install_path, sizeof(out->install_path), pkg->install_path);
    copy_text(out->message, sizeof(out->message), blocker_text(missing));
}

void installer_init(void) {
    g_installer_ready = true;
    kprintf("[INSTALLER] service ready: probe db=%lu net_probe=%s\n",
            sizeof(g_packages) / sizeof(g_packages[0]), g_packages[0].official_url);
}

int installer_status(const char *package, yam_installer_status_t *out) {
    if (!g_installer_ready || !out) return -1;
    installer_package_t *pkg = find_package(package);
    fill_status(pkg, out);
    return pkg ? 0 : -2;
}

int installer_request(const char *package, yam_installer_status_t *out) {
    if (!g_installer_ready || !out) return -1;
    installer_package_t *pkg = find_package(package);
    if (!pkg) {
        fill_status(NULL, out);
        return -2;
    }

    probe_runtime_tcp();
    probe_runtime_http();
    probe_runtime_tls();

    yam_installer_status_t st;
    fill_status(pkg, &st);
    if (st.missing) {
        pkg->state = YAM_INSTALL_STATE_BLOCKED;
        pkg->last_error = YAM_INSTALL_ERR_MISSING_CAPABILITY;
        pkg->progress = 0;
        fill_status(pkg, out);
        kprintf("[INSTALLER] request package=%s blocked missing=0x%x msg='%s'\n",
                pkg->name, out->missing, out->message);
        return -3;
    }

    pkg->state = YAM_INSTALL_STATE_READY_TO_DOWNLOAD;
    pkg->last_error = YAM_INSTALL_ERR_NONE;
    pkg->progress = 5;
    fill_status(pkg, out);
    kprintf("[INSTALLER] request package=%s ready for secure downloader url=%s\n",
            pkg->name, pkg->official_url);
    return 0;
}
