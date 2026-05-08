/* ============================================================================
 * YamKernel — Wayland Client: Terminal Emulator
 * A graphical terminal window in the Wayland compositor.
 * ============================================================================ */
#include <nexus/types.h>
#include "../sched/sched.h"
#include "../sched/wait.h"
#include "compositor.h"
#include "wl_draw.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../net/net.h"
#include "../drivers/net/e1000.h"
#include "drivers/timer/pit.h"
#include "fs/vfs.h"
#include "fs/elf.h"
#include "os/services/installer/installer.h"
#include "mem/heap.h"
#include "lib/spinlock.h"

void wl_spawn_app_async(void *data, usize size, const char *name);

/* Terminal dimensions in characters */
#define TERM_COLS  72
#define TERM_ROWS  22
#define TERM_W     (TERM_COLS * 8)   /* 576 px */
#define TERM_H     (TERM_ROWS * 16)  /* 352 px */
#define TERM_MIN_W 360
#define TERM_MIN_H 220

/* Colors */
#define COL_BG       0xFF1A1A2E
#define COL_FG       0xFF00FF88
#define COL_PROMPT   0xFF00DDFF
#define COL_CURSOR   0xFF00FF88
#define COL_ERR      0xFFFF4444

/* PS/2 Scancode Set 1 → ASCII (unshifted) */
static const char sc_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',  0,
    '\\','z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
};
static const char sc_ascii_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',  0,
    '|','Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ',
};

/* Terminal state */
static char screen[TERM_ROWS][TERM_COLS + 1]; /* text buffer */
static u32  screen_color[TERM_ROWS];           /* per-row color */
static int  cur_row = 0;
static int  cur_col = 0;
static char input_buf[128];
static int  input_len = 0;
static bool shift_held = false;
static char history[16][128];
static int history_count = 0;
static int history_pos = 0;

typedef struct {
    char host[96];
    int loops;
    u32 worker_id;
} netstress_job_t;

static volatile int g_netstress_done = 0;
static volatile int g_netstress_dns_ok = 0;
static volatile int g_netstress_ping_ok = 0;
static volatile int g_netstress_http_ok = 0;
static spinlock_t g_netstress_lock = SPINLOCK_INIT;

static void term_scroll(void) {
    for (int r = 0; r < TERM_ROWS - 1; r++) {
        memcpy(screen[r], screen[r + 1], TERM_COLS + 1);
        screen_color[r] = screen_color[r + 1];
    }
    memset(screen[TERM_ROWS - 1], 0, TERM_COLS + 1);
    screen_color[TERM_ROWS - 1] = COL_FG;
    if (cur_row > 0) cur_row--;
}

static void term_putchar(char c, u32 color) {
    if (c == '\n') {
        screen_color[cur_row] = color;
        cur_row++;
        cur_col = 0;
        if (cur_row >= TERM_ROWS) term_scroll();
        return;
    }
    if (c == '\b') {
        if (cur_col > 0) {
            cur_col--;
            screen[cur_row][cur_col] = ' ';
        }
        return;
    }
    if (cur_col < TERM_COLS) {
        screen[cur_row][cur_col] = c;
        screen_color[cur_row] = color;
        cur_col++;
    }
    if (cur_col >= TERM_COLS) {
        cur_col = 0;
        cur_row++;
        if (cur_row >= TERM_ROWS) term_scroll();
    }
}

static void term_puts(const char *s, u32 color) {
    while (*s) term_putchar(*s++, color);
}

static void term_newline(void) {
    term_putchar('\n', COL_FG);
}

static void copy_text(char *dst, const char *src, usize cap) {
    if (cap == 0) return;
    usize i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void history_add(const char *cmd) {
    if (!cmd || !*cmd) return;
    if (history_count > 0 && strcmp(history[history_count - 1], cmd) == 0) {
        history_pos = history_count;
        return;
    }
    if (history_count < 16) {
        copy_text(history[history_count++], cmd, sizeof(history[0]));
    } else {
        for (int i = 1; i < 16; i++) copy_text(history[i - 1], history[i], sizeof(history[0]));
        copy_text(history[15], cmd, sizeof(history[0]));
    }
    history_pos = history_count;
}

static int text_len(const char *s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static int parse_int_simple(const char *s, int def) {
    if (!s || !*s) return def;
    int v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v > 0 ? v : def;
}

static void replace_input_line(const char *cmd) {
    while (input_len > 0) {
        input_len--;
        term_putchar('\b', COL_FG);
    }
    int old_len = input_len;
    (void)old_len;
    for (int i = 0; i < 127; i++) input_buf[i] = 0;
    copy_text(input_buf, cmd, sizeof(input_buf));
    input_len = text_len(input_buf);
    term_puts(input_buf, COL_FG);
}

/* ---- Built-in commands ---- */
static void cmd_help(void) {
    term_puts("Available commands:", COL_PROMPT);
    term_newline();
    term_puts("  help     - Show this help", COL_FG);
    term_newline();
    term_puts("  clear    - Clear the screen", COL_FG);
    term_newline();
    term_puts("  uname    - Show system info", COL_FG);
    term_newline();
    term_puts("  ps       - List running tasks", COL_FG);
    term_newline();
    term_puts("  ls cd pwd cat mkdir touch rm write history - File tools", COL_FG);
    term_newline();
    term_puts("  run /bin/hello - Launch a VFS ELF app", COL_FG);
    term_newline();
    term_puts("  ifconfig netstat netstats dns ping - Network status/tools", COL_FG);
    term_newline();
    term_puts("  http https curl dns ping netstress git - Web/network commands", COL_FG);
    term_newline();
    term_puts("  installer status | install kernel-net", COL_FG);
    term_newline();
    term_puts("  echo ... - Print text", COL_FG);
    term_newline();
    term_puts("  uptime   - Show tick count", COL_FG);
    term_newline();
    term_puts("  whoami   - Current user", COL_FG);
    term_newline();
    term_puts("  neofetch - System info", COL_FG);
    term_newline();
    term_puts("Use Up/Down arrows for command history.", COL_PROMPT);
    term_newline();
}

static void cmd_clear(void) {
    for (int r = 0; r < TERM_ROWS; r++) {
        memset(screen[r], 0, TERM_COLS + 1);
        screen_color[r] = COL_FG;
    }
    cur_row = 0;
    cur_col = 0;
}

static void cmd_uname(void) {
    term_puts("YamKernel v0.2.0 x86_64 Graph-Based Adaptive OS", COL_PROMPT);
    term_newline();
}

static void cmd_whoami(void) {
    term_puts("root", COL_FG);
    term_newline();
}

static void cmd_neofetch(void) {
    term_puts("   __  __          ", COL_PROMPT); term_puts("root@yamkernel", COL_FG); term_newline();
    term_puts("  |  \\/  |         ", COL_PROMPT); term_puts("--------------", COL_FG); term_newline();
    term_puts("  |      | __ _ _ ", COL_PROMPT);  term_puts("OS: YamKernel v0.2.0", COL_FG); term_newline();
    term_puts("  |_|\\/|_|/ _` | |", COL_PROMPT); term_puts("Arch: x86_64", COL_FG); term_newline();
    term_puts("         | (_| | |", COL_PROMPT);  term_puts("Shell: yamsh", COL_FG); term_newline();
    term_puts("          \\__,_|_|", COL_PROMPT);  term_puts("WM: Wayland", COL_FG); term_newline();
    term_puts("                  ", COL_PROMPT);   term_puts("Heap: 64 MB", COL_FG); term_newline();
}

static void cmd_uptime(void) {
    char buf[64];
    task_t *t = sched_current();
    u64 ticks = t ? t->ticks : 0;
    ksnprintf(buf, sizeof(buf), "Uptime: %lu ticks", ticks);
    term_puts(buf, COL_FG);
    term_newline();
}

static void cmd_echo(const char *args) {
    if (args && *args) {
        term_puts(args, COL_FG);
    }
    term_newline();
}

static void cmd_history(void) {
    char line[160];
    for (int i = 0; i < history_count; i++) {
        ksnprintf(line, sizeof(line), "%4d  %s", i + 1, history[i]);
        term_puts(line, COL_FG);
        term_newline();
    }
}

static void cmd_ls(const char *path) {
    const char *p = (path && *path) ? path : ".";
    if (strcmp(p, "~") == 0) p = "/home/root";
    vfs_dirent_t ent;
    bool any = false;
    for (u32 i = 0; i < 64; i++) {
        if (sys_readdir(p, i, &ent) < 0) break;
        term_puts(ent.is_dir ? "[d] " : "[f] ", ent.is_dir ? COL_PROMPT : COL_FG);
        term_puts(ent.name, COL_FG);
        term_newline();
        any = true;
    }
    if (!any) term_puts("(empty or not listable)", COL_FG);
    term_newline();
}

static void cmd_pwd(void) {
    char cwd[256];
    if (sys_getcwd(cwd, sizeof(cwd)) >= 0) term_puts(cwd, COL_FG);
    else term_puts("pwd: failed", COL_ERR);
    term_newline();
}

static void cmd_cd(const char *path) {
    const char *p = (path && *path) ? path : "/home/root";
    if (strcmp(p, "~") == 0) p = "/home/root";
    if (sys_chdir(p) == 0) {
        char cwd[256];
        if (sys_getcwd(cwd, sizeof(cwd)) >= 0) {
            term_puts("cwd: ", COL_PROMPT);
            term_puts(cwd, COL_FG);
        }
    } else {
        term_puts("cd: no such directory", COL_ERR);
    }
    term_newline();
}

static void cmd_cat(const char *path) {
    if (!path || !*path) {
        term_puts("cat: missing file operand", COL_ERR);
    } else {
        char buf[512];
        int fd = sys_open(path, 0);
        if (fd < 0) {
            term_puts("cat: cannot open file", COL_ERR);
        } else {
            isize n = sys_read(fd, buf, sizeof(buf) - 1);
            sys_close(fd);
            if (n < 0) term_puts("cat: read failed", COL_ERR);
            else {
                buf[n] = 0;
                term_puts(buf, COL_FG);
            }
        }
    }
    term_newline();
}

static void cmd_mkdir_term(const char *path) {
    if (!path || !*path) {
        term_puts("mkdir: missing operand", COL_ERR);
    } else if (sys_mkdir(path, 0755) == 0) {
        term_puts("directory created", COL_PROMPT);
    } else {
        term_puts("mkdir: failed", COL_ERR);
    }
    term_newline();
}

static void cmd_touch(const char *path) {
    if (!path || !*path) {
        term_puts("touch: missing file operand", COL_ERR);
    } else {
        int fd = sys_open(path, 0x40 | 0x2);
        if (fd >= 0) {
            sys_close(fd);
            term_puts("file ready", COL_PROMPT);
        } else {
            term_puts("touch: failed", COL_ERR);
        }
    }
    term_newline();
}

static void cmd_rm(const char *path) {
    if (!path || !*path) {
        term_puts("rm: missing operand", COL_ERR);
    } else if (sys_unlink(path) == 0) {
        term_puts("deleted", COL_PROMPT);
    } else {
        term_puts("rm: failed", COL_ERR);
    }
    term_newline();
}

static void cmd_write_file(char *args) {
    if (!args || !*args) {
        term_puts("write: usage write /home/root/file.txt text", COL_ERR);
        term_newline();
        return;
    }
    char *space = args;
    while (*space && *space != ' ') space++;
    if (!*space) {
        term_puts("write: missing text", COL_ERR);
        term_newline();
        return;
    }
    *space++ = 0;
    int fd = sys_open(args, 0x40 | 0x200 | 0x2);
    if (fd < 0) {
        term_puts("write: open failed", COL_ERR);
    } else {
        sys_write(fd, space, strlen(space));
        sys_close(fd);
        term_puts("saved", COL_PROMPT);
    }
    term_newline();
}

static void cmd_run_app(char *args) {
    if (!args || !*args) {
        term_puts("run: usage run /bin/hello", COL_ERR);
        term_newline();
        return;
    }
    char *argv[16];
    int argc = 0;
    char *p = args;
    while (*p && argc < 15) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    argv[argc] = NULL;
    if (argc == 0) {
        term_puts("run: usage run /bin/hello", COL_ERR);
        term_newline();
        return;
    }
    const char *envp[] = {
        "YAMOS_SHELL=terminal",
        "YAMOS_USER=root",
        "PATH=/bin:/usr/local/bin:/opt/yamos/packages:/home/root/bin",
        NULL
    };
    i64 pid = elf_spawn_resolved_argv_envp(argv[0], argc,
                                           (const char *const *)argv, envp);
    if (pid < 0) {
        term_puts("run: spawn failed", COL_ERR);
    } else {
        char line[96];
        ksnprintf(line, sizeof(line), "spawned pid=%ld", pid);
        term_puts(line, COL_PROMPT);
    }
    term_newline();
}

static void cmd_net_status(void) {
    char line[128];
    ksnprintf(line, sizeof(line), "eth0: %s  dhcp=%s  mac=%02x:%02x:%02x:%02x:%02x:%02x",
              g_net_iface.is_up ? "up" : "down",
              g_net_iface.dhcp_done ? "done" : "pending",
              g_net_iface.mac_addr[0], g_net_iface.mac_addr[1], g_net_iface.mac_addr[2],
              g_net_iface.mac_addr[3], g_net_iface.mac_addr[4], g_net_iface.mac_addr[5]);
    term_puts(line, g_net_iface.is_up ? COL_PROMPT : COL_ERR);
    term_newline();
    ksnprintf(line, sizeof(line), "ip=%lu.%lu.%lu.%lu gateway=%lu.%lu.%lu.%lu dns=%lu.%lu.%lu.%lu",
              (g_net_iface.ip_addr >> 24) & 255, (g_net_iface.ip_addr >> 16) & 255,
              (g_net_iface.ip_addr >> 8) & 255, g_net_iface.ip_addr & 255,
              (g_net_iface.gateway >> 24) & 255, (g_net_iface.gateway >> 16) & 255,
              (g_net_iface.gateway >> 8) & 255, g_net_iface.gateway & 255,
              (g_net_iface.dns_server >> 24) & 255, (g_net_iface.dns_server >> 16) & 255,
              (g_net_iface.dns_server >> 8) & 255, g_net_iface.dns_server & 255);
    term_puts(line, COL_FG);
    term_newline();
    if (!g_net_iface.dhcp_done) {
        term_puts("network blocker: DHCP has no lease yet", COL_ERR);
        term_newline();
    }
}

static void cmd_netstats(void) {
    e1000_stats_t st;
    memset(&st, 0, sizeof(st));
    e1000_get_stats(&st);

    char line[160];
    term_puts("kernel net stats (e1000):", COL_PROMPT);
    term_newline();

    ksnprintf(line, sizeof(line), "tx: attempts=%lu ok=%lu timeout=%lu",
              st.tx_attempts, st.tx_ok, st.tx_timeouts);
    term_puts(line, COL_FG);
    term_newline();

    ksnprintf(line, sizeof(line), "tx drops: ring_full=%lu oversize=%lu not_ready=%lu",
              st.tx_ring_full_drop, st.tx_oversize_drop, st.tx_not_ready_drop);
    term_puts(line, (st.tx_ring_full_drop || st.tx_timeouts) ? COL_ERR : COL_FG);
    term_newline();

    ksnprintf(line, sizeof(line), "rx: packets=%lu bytes=%lu drops=%lu",
              st.rx_packets, st.rx_bytes, st.rx_drops);
    term_puts(line, st.rx_drops ? COL_ERR : COL_FG);
    term_newline();
}

static bool parse_ipv4(const char *s, u32 *out_ip) {
    if (!s || !*s || !out_ip) return false;
    int a = 0, b = 0, c = 0, d = 0;
    int dots = 0;
    const char *p = s;
    while (*p) {
        if (*p == '.') {
            dots++;
            p++;
            continue;
        }
        if (*p < '0' || *p > '9') return false;
        int v = *p - '0';
        if (dots == 0) a = a * 10 + v;
        else if (dots == 1) b = b * 10 + v;
        else if (dots == 2) c = c * 10 + v;
        else if (dots == 3) d = d * 10 + v;
        else return false;
        p++;
    }
    if (dots != 3) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    *out_ip = ((u32)a << 24) | ((u32)b << 16) | ((u32)c << 8) | (u32)d;
    return true;
}

static bool resolve_host_or_ip(const char *arg, u32 *out_ip) {
    if (parse_ipv4(arg, out_ip)) return true;
    return dns_resolve(arg, out_ip) == 0;
}

static void cmd_dns(const char *host) {
    if (!host || !*host) {
        term_puts("dns: usage dns <host>", COL_ERR);
        term_newline();
        return;
    }
    u32 ip = 0;
    if (dns_resolve(host, &ip) != 0) {
        term_puts("dns: resolve failed", COL_ERR);
        term_newline();
        return;
    }
    char line[96];
    ksnprintf(line, sizeof(line), "%s -> %u.%u.%u.%u", host,
              (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    term_puts(line, COL_PROMPT);
    term_newline();
}

static void cmd_ping(const char *target) {
    if (!target || !*target) {
        term_puts("ping: usage ping <host-or-ip>", COL_ERR);
        term_newline();
        return;
    }
    u32 ip = 0;
    if (!resolve_host_or_ip(target, &ip)) {
        term_puts("ping: target resolve failed", COL_ERR);
        term_newline();
        return;
    }
    int ok = 0;
    for (u16 seq = 1; seq <= 4; seq++) {
        int rc = icmp_ping(ip, seq, 1000);
        if (rc == 0) {
            ok++;
            term_puts("reply", COL_PROMPT);
        } else {
            term_puts("timeout", COL_ERR);
        }
        term_newline();
    }
    char sum[64];
    ksnprintf(sum, sizeof(sum), "ping: %d/4 replies", ok);
    term_puts(sum, ok ? COL_PROMPT : COL_ERR);
    term_newline();
}

static void cmd_http_get(const char *url, bool tls) {
    if (!url || !*url) {
        term_puts(tls ? "https: usage https <host>[/path]" : "http: usage http <host>[/path]", COL_ERR);
        term_newline();
        return;
    }
    char host[96];
    char path[160];
    int hi = 0;
    const char *p = url;
    while (*p && *p != '/' && hi + 1 < (int)sizeof(host)) host[hi++] = *p++;
    host[hi] = 0;
    if (!host[0]) {
        term_puts("request: missing host", COL_ERR);
        term_newline();
        return;
    }
    if (*p == '/') copy_text(path, p, sizeof(path));
    else copy_text(path, "/", sizeof(path));
    static char out[8192];
    usize out_len = 0;
    int rc = tls ? https_get(host, path, out, sizeof(out), &out_len)
                 : http_get(host, path, out, sizeof(out), &out_len);
    if (rc < 0) {
        term_puts(tls ? "https: request failed" : "http: request failed", COL_ERR);
        term_newline();
        return;
    }
    char line[96];
    ksnprintf(line, sizeof(line), "%s://%s%s -> %lu bytes", tls ? "https" : "http", host, path, out_len);
    term_puts(line, COL_PROMPT);
    term_newline();
    out[(out_len < sizeof(out) - 1) ? out_len : sizeof(out) - 1] = 0;
    term_puts(out, COL_FG);
    term_newline();
}

static void netstress_worker(void *arg) {
    netstress_job_t *job = (netstress_job_t *)arg;
    char out[1024];
    usize out_len = 0;
    u32 ip = 0;

    for (int i = 0; i < job->loops; i++) {
        if (dns_resolve(job->host, &ip) == 0) {
            u64 f = spin_lock_irqsave(&g_netstress_lock);
            g_netstress_dns_ok++;
            spin_unlock_irqrestore(&g_netstress_lock, f);

            if (icmp_ping(ip, (u16)((job->worker_id << 8) + (i & 0xff)), 800) == 0) {
                f = spin_lock_irqsave(&g_netstress_lock);
                g_netstress_ping_ok++;
                spin_unlock_irqrestore(&g_netstress_lock, f);
            }

            if (http_get(job->host, "/", out, sizeof(out), &out_len) == 0) {
                f = spin_lock_irqsave(&g_netstress_lock);
                g_netstress_http_ok++;
                spin_unlock_irqrestore(&g_netstress_lock, f);
            }
        }
        task_sleep_ms(10);
    }

    u64 f = spin_lock_irqsave(&g_netstress_lock);
    g_netstress_done++;
    spin_unlock_irqrestore(&g_netstress_lock, f);
    kfree(job);
}

static void cmd_netstress(char *args) {
    int workers = 2;
    int loops = 5;
    char host[96];
    copy_text(host, "example.com", sizeof(host));

    if (args && *args) {
        char *p = args;
        while (*p == ' ') p++;
        if (*p) {
            workers = parse_int_simple(p, workers);
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
            if (*p) {
                loops = parse_int_simple(p, loops);
                while (*p && *p != ' ') p++;
                while (*p == ' ') p++;
                if (*p) copy_text(host, p, sizeof(host));
            }
        }
    }
    if (workers < 1) workers = 1;
    if (workers > 8) workers = 8;
    if (loops < 1) loops = 1;
    if (loops > 40) loops = 40;

    g_netstress_done = 0;
    g_netstress_dns_ok = 0;
    g_netstress_ping_ok = 0;
    g_netstress_http_ok = 0;

    char line[128];
    ksnprintf(line, sizeof(line), "netstress: workers=%d loops=%d host=%s", workers, loops, host);
    term_puts(line, COL_PROMPT);
    term_newline();

    for (int i = 0; i < workers; i++) {
        netstress_job_t *job = (netstress_job_t *)kmalloc(sizeof(netstress_job_t));
        if (!job) continue;
        memset(job, 0, sizeof(*job));
        copy_text(job->host, host, sizeof(job->host));
        job->loops = loops;
        job->worker_id = (u32)i;
        sched_spawn("netstress", netstress_worker, job, 2);
    }

    int guard = 0;
    while (g_netstress_done < workers && guard < 2000) {
        task_sleep_ms(10);
        guard++;
    }

    ksnprintf(line, sizeof(line), "netstress done: dns_ok=%d ping_ok=%d http_ok=%d expected=%d",
              g_netstress_dns_ok, g_netstress_ping_ok, g_netstress_http_ok, workers * loops);
    term_puts(line, (g_netstress_dns_ok > 0 && g_netstress_http_ok > 0) ? COL_PROMPT : COL_ERR);
    term_newline();
}

static void cmd_curl(const char *args) {
    if (!args || !*args) {
        term_puts("curl: usage curl http://host/path | https://host/path", COL_ERR);
        term_newline();
        return;
    }
    if (strncmp(args, "http://", 7) == 0) {
        cmd_http_get(args + 7, false);
        return;
    }
    if (strncmp(args, "https://", 8) == 0) {
        cmd_http_get(args + 8, true);
        return;
    }
    term_puts("curl: only http:// and https:// supported", COL_ERR);
    term_newline();
}

static void cmd_git(const char *args) {
    term_puts("git: installed command stub, backend not ready", COL_ERR);
    term_newline();
    term_puts("missing: exec/subprocess, encrypted HTTPS client, cert validation, packfile/storage support", COL_ERR);
    term_newline();
    kprintf("[GIT] blocked: args='%s' if_up=%d dhcp=%d\n",
            args ? args : "", g_net_iface.is_up, g_net_iface.dhcp_done);
}

static void print_installer_status(const yam_installer_status_t *st) {
    char line[192];
    ksnprintf(line, sizeof(line), "%s: state=%u progress=%u%% missing=0x%x",
              st->display_name, st->state, st->progress, st->missing);
    term_puts(line, st->missing ? COL_ERR : COL_PROMPT);
    term_newline();
    term_puts("source: ", COL_FG);
    term_puts(st->official_url, COL_FG);
    term_newline();
    term_puts("target: ", COL_FG);
    term_puts(st->install_path, COL_FG);
    term_newline();
    term_puts(st->missing ? "blocked: " : "ready: ", st->missing ? COL_ERR : COL_PROMPT);
    term_puts(st->message, st->missing ? COL_ERR : COL_PROMPT);
    term_newline();
}

static void cmd_installer_status(void) {
    yam_installer_status_t st;
    int rc = installer_status("kernel-net", &st);
    if (rc < 0) {
        term_puts("installer: package database unavailable", COL_ERR);
        term_newline();
        return;
    }
    print_installer_status(&st);
}

static bool cmd_install_package(const char *package) {
    if (!package || !*package) package = "kernel-net";
    yam_installer_status_t st;
    int rc = installer_request(package, &st);
    print_installer_status(&st);
    if (rc < 0 || st.missing) {
        kprintf("[INSTALLER] terminal install request package='%s' blocked missing=0x%x msg='%s'\n",
                package, st.missing, st.message);
        return false;
    }
    kprintf("[INSTALLER] terminal install request package='%s' ok state=%u\n", package,
            st.state);
    return true;
}

static bool cmd_kernel_probe(void) {
    installer_refresh_probes();
    yam_installer_status_t st;
    int rc = installer_status("kernel-net", &st);
    term_puts("kernel-net: probing kernel network/cache capability", COL_PROMPT);
    term_newline();
    term_puts("probe: ", COL_FG);
    term_puts(st.official_url, COL_FG);
    term_newline();
    if (rc < 0 || st.missing) {
        term_puts("blocked: ", COL_ERR);
        term_puts(st.message, COL_ERR);
        term_newline();
        term_puts("order: drivers -> network -> cache -> persistent storage", COL_FG);
        term_newline();
        kprintf("[KERNEL_PROBE] request blocked state=%u missing=0x%x msg='%s'\n",
                st.state, st.missing, st.message);
        return false;
    }
    term_puts("kernel capability probe ready", COL_PROMPT);
    term_newline();
    kprintf("[KERNEL_PROBE] request ready url='%s'\n", st.official_url);
    return true;
}

static void process_command(void) {
    /* Null-terminate */
    input_buf[input_len] = 0;

    /* Skip leading spaces */
    char *cmd = input_buf;
    while (*cmd == ' ') cmd++;
    history_add(cmd);

    if (*cmd == 0) {
        /* Empty command */
    } else if (strcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "clear") == 0) {
        cmd_clear();
    } else if (strcmp(cmd, "uname") == 0 || strcmp(cmd, "uname -a") == 0) {
        cmd_uname();
    } else if (strcmp(cmd, "whoami") == 0) {
        cmd_whoami();
    } else if (strcmp(cmd, "neofetch") == 0) {
        cmd_neofetch();
    } else if (strcmp(cmd, "uptime") == 0) {
        cmd_uptime();
    } else if (strcmp(cmd, "pwd") == 0) {
        cmd_pwd();
    } else if (strcmp(cmd, "cd") == 0) {
        cmd_cd("/home/root");
    } else if (strncmp(cmd, "cd ", 3) == 0) {
        cmd_cd(cmd + 3);
    } else if (strcmp(cmd, "history") == 0) {
        cmd_history();
    } else if (strcmp(cmd, "ls") == 0) {
        cmd_ls(".");
    } else if (strncmp(cmd, "ls ", 3) == 0) {
        cmd_ls(cmd + 3);
    } else if (strncmp(cmd, "cat ", 4) == 0) {
        cmd_cat(cmd + 4);
    } else if (strncmp(cmd, "mkdir ", 6) == 0) {
        cmd_mkdir_term(cmd + 6);
    } else if (strncmp(cmd, "touch ", 6) == 0) {
        cmd_touch(cmd + 6);
    } else if (strncmp(cmd, "rm ", 3) == 0) {
        cmd_rm(cmd + 3);
    } else if (strncmp(cmd, "write ", 6) == 0) {
        cmd_write_file(cmd + 6);
    } else if (strncmp(cmd, "run ", 4) == 0) {
        cmd_run_app(cmd + 4);
    } else if (strcmp(cmd, "ifconfig") == 0 || strcmp(cmd, "ip addr") == 0 ||
               strcmp(cmd, "netstat") == 0 || strcmp(cmd, "network") == 0) {
        cmd_net_status();
    } else if (strcmp(cmd, "netstats") == 0) {
        cmd_netstats();
    } else if (strncmp(cmd, "dns ", 4) == 0) {
        cmd_dns(cmd + 4);
    } else if (strncmp(cmd, "ping ", 5) == 0) {
        cmd_ping(cmd + 5);
    } else if (strncmp(cmd, "http ", 5) == 0) {
        cmd_http_get(cmd + 5, false);
    } else if (strncmp(cmd, "https ", 6) == 0) {
        cmd_http_get(cmd + 6, true);
    } else if (strcmp(cmd, "netstress") == 0) {
        cmd_netstress("");
    } else if (strncmp(cmd, "netstress ", 10) == 0) {
        cmd_netstress(cmd + 10);
    } else if (strcmp(cmd, "ps") == 0) {
        term_puts("  PID  NAME         STATE", COL_PROMPT);
        term_newline();
        term_puts("    0  kernel       running", COL_FG);
        term_newline();
        term_puts("    1  wayland      running", COL_FG);
        term_newline();
        term_puts("    2  wl-calc      running", COL_FG);
        term_newline();
        term_puts("    3  wl-browser   running", COL_FG);
        term_newline();
        term_puts("    4  wl-term      running", COL_FG);
        term_newline();
    } else if (strncmp(cmd, "echo ", 5) == 0) {
        cmd_echo(cmd + 5);
    } else if (strcmp(cmd, "installer status") == 0 || strcmp(cmd, "install status") == 0) {
        cmd_installer_status();
    } else if (strcmp(cmd, "install") == 0) {
        cmd_install_package("kernel-net");
    } else if (strncmp(cmd, "install ", 8) == 0) {
        cmd_install_package(cmd + 8);
    } else if (strcmp(cmd, "kernel-net") == 0 || strcmp(cmd, "driver-probe") == 0) {
        cmd_kernel_probe();
    } else if (strcmp(cmd, "curl") == 0) {
        cmd_curl("");
    } else if (strncmp(cmd, "curl ", 5) == 0) {
        cmd_curl(cmd + 5);
    } else if (strcmp(cmd, "git") == 0) {
        cmd_git("");
    } else if (strncmp(cmd, "git ", 4) == 0) {
        cmd_git(cmd + 4);
    } else {
        char direct_cmd[128];
        copy_text(direct_cmd, cmd, sizeof(direct_cmd));
        char *argv[16];
        int argc = 0;
        char *p = direct_cmd;
        while (*p && argc < 15) {
            while (*p == ' ') p++;
            if (!*p) break;
            argv[argc++] = p;
            while (*p && *p != ' ') p++;
            if (*p) *p++ = 0;
        }
        argv[argc] = NULL;
        if (argc > 0) {
            const char *envp[] = {
                "YAMOS_SHELL=terminal",
                "YAMOS_USER=root",
                "PATH=/bin:/usr/local/bin:/opt/yamos/packages:/home/root/bin",
                NULL
            };
            i64 pid = elf_spawn_resolved_argv_envp(argv[0], argc,
                                                   (const char *const *)argv,
                                                   envp);
            if (pid >= 0) {
                char line[96];
                ksnprintf(line, sizeof(line), "spawned pid=%ld", pid);
                term_puts(line, COL_PROMPT);
                term_newline();
                input_len = 0;
                return;
            }
        }
        term_puts(cmd, COL_ERR);
        term_puts(": command not found", COL_ERR);
        term_newline();
    }

    input_len = 0;
}

static void show_prompt(void) {
    char cwd[256];
    term_puts("root@yam", COL_PROMPT);
    if (sys_getcwd(cwd, sizeof(cwd)) >= 0) {
        term_puts(":", COL_FG);
        if (strcmp(cwd, "/home/root") == 0) term_puts("~", COL_FG);
        else term_puts(cwd, COL_FG);
    } else {
        term_puts(":?", COL_FG);
    }
    term_puts("$ ", COL_FG);
}

static void draw_terminal(wl_surface_t *s, bool cursor_on) {
    if (!s) return;
    /* Background */
    wl_draw_rect(s, 0, 0, s->width, s->height, COL_BG);

    int visible_cols = (int)(s->width / 8);
    int visible_rows = (int)(s->height / 16);
    if (visible_cols < 8) visible_cols = 8;
    if (visible_cols > TERM_COLS) visible_cols = TERM_COLS;
    if (visible_rows < 4) visible_rows = 4;
    if (visible_rows > TERM_ROWS) visible_rows = TERM_ROWS;

    /* Render text buffer */
    for (int r = 0; r < visible_rows; r++) {
        if (screen[r][0] != 0 || r == cur_row) {
            wl_draw_text(s, 0, r * 16, screen[r], screen_color[r], 0);
        }
    }

    /* Draw cursor (blinking block) */
    if (cursor_on) {
        if (cur_col < visible_cols && cur_row < visible_rows) {
            wl_draw_rect(s, cur_col * 8, cur_row * 16, 8, 16, COL_CURSOR);
        }
    }
}

void wl_term_task(void *arg) {
    (void)arg;
    task_sleep_ms(400);

    wl_surface_t *s = wl_surface_create("Terminal", 50, 50, TERM_W, TERM_H, sched_current()->id);
    if (!s) return;
    wl_surface_set_constraints(s, true, TERM_MIN_W, TERM_MIN_H);

    /* Initialize screen */
    memset(screen, 0, sizeof(screen));
    for (int r = 0; r < TERM_ROWS; r++) screen_color[r] = COL_FG;

    /* Welcome message */
    term_puts("YamKernel Terminal v0.2", COL_PROMPT);
    term_newline();
    term_puts("Type 'help' for available commands.", COL_FG);
    term_newline();
    sys_chdir("/home/root");
    term_newline();
    show_prompt();

    bool cursor_on = ((pit_uptime_ms() / 500) & 1) != 0;
    draw_terminal(s, cursor_on);
    wl_surface_commit(s);

    u32 my_id = s->id;
    bool last_cursor_on = cursor_on;
    while (s->state == WL_SURFACE_ACTIVE && s->id == my_id) {
        input_event_t ev;
        bool dirty = false;

        while (wl_surface_pop_event(s, &ev)) {
            if (ev.type == EV_RESIZE) {
                dirty = true;
                continue;
            }
            if (ev.type == EV_KEY) {
                u16 sc = ev.code;

                /* Track shift */
                if (sc == 0x2A || sc == 0x36) {
                    shift_held = (ev.value == KEY_PRESSED);
                    continue;
                }

                if (ev.value != KEY_PRESSED) continue;

                if (sc == 0x48 && history_count > 0) {
                    if (history_pos > 0) history_pos--;
                    replace_input_line(history[history_pos]);
                    dirty = true;
                    continue;
                }
                if (sc == 0x50 && history_count > 0) {
                    if (history_pos + 1 < history_count) {
                        history_pos++;
                        replace_input_line(history[history_pos]);
                    } else {
                        history_pos = history_count;
                        replace_input_line("");
                    }
                    dirty = true;
                    continue;
                }

                /* Translate scancode to ASCII */
                char c = 0;
                if (sc < 128) {
                    c = shift_held ? sc_ascii_shift[sc] : sc_ascii[sc];
                }

                if (c == '\n') {
                    term_newline();
                    process_command();
                    show_prompt();
                    dirty = true;
                } else if (c == '\b') {
                    if (input_len > 0) {
                        input_len--;
                        term_putchar('\b', COL_FG);
                        dirty = true;
                    }
                } else if (c >= 32 && c < 127 && input_len < 126) {
                    input_buf[input_len++] = c;
                    term_putchar(c, COL_FG);
                    dirty = true;
                }
            }
        }

        cursor_on = ((pit_uptime_ms() / 500) & 1) != 0;
        if (cursor_on != last_cursor_on) {
            dirty = true;
            last_cursor_on = cursor_on;
        }
        if (dirty) {
            draw_terminal(s, cursor_on);
            wl_surface_commit(s);
        }

        task_sleep_ms(16);
    }
}
