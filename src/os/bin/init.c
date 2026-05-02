/* ============================================================================
 * YamKernel — PID 1 Init Process
 * Spawns services, mounts filesystems, manages runlevel transitions.
 * ============================================================================ */
#include "../../sched/sched.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"
#include "../../fs/vfs.h"
#include "../services/installer/installer.h"

extern void dhcp_start(void);
extern void vtty_init(void);
extern void devfs_init(void);
extern void vtty_write(int id, const char *s, usize len);
extern int  g_active_vtty;

/* ---- Service table ---- */
typedef struct {
    const char *name;
    void      (*fn)(void *);
    bool       autostart;
    bool       running;
} service_t;

/* Forward declarations for services */
static void init_net_task(void *arg);
static void init_shell_task(void *arg);

static service_t g_services[] = {
    { "net-init", init_net_task,   true, false },
    { "shell",    init_shell_task, true, false },
};

/* ---- Service implementations ---- */

static void init_net_task(void *arg) {
    (void)arg;
    kprintf_color(0xFF00DDFF, "[INIT] Starting network...\n");
    dhcp_start();
    kprintf_color(0xFF00FF88, "[INIT] Network ready\n");
    yam_installer_status_t st;
    if (installer_request("kernel-net", &st) < 0) {
        kprintf("[INIT] kernel capability refresh: missing=0x%x blocker='%s'\n",
                st.missing, st.message);
    }
    /* Task exits after setup */
}

static void init_shell_task(void *arg) {
    (void)arg;
    /* Write a shell prompt to each VTY */
    for (int i = 0; i < 6; i++) {
        const char *banner =
            "\033[1;32mYamOS\033[0m v0.4.0 — Virtual Console\n"
            "Type 'help' for available commands.\n\n"
            "\033[1;34mroot@yamkernel\033[0m:\033[1;33m/\033[0m$ ";
        vtty_write(i, banner, strlen(banner));
    }
}

/* ---- Init Main (runs as PID 1) ---- */

extern bool elf_load(const void *data, usize size, const char *name);
extern void *g_authd_module;
extern usize g_authd_module_size;

void init_task(void *arg) {
    (void)arg;
    kprintf_color(0xFF00FF88, "[INIT] PID 1 starting...\n");

    /* Initialize virtual consoles */
    vtty_init();
    devfs_init();
    
    kprintf_color(0xFF888888, "[INIT] authd_module=%p size=%lu\n", g_authd_module, (u64)g_authd_module_size);
    if (g_authd_module) {
        kprintf_color(0xFF00FF88, "[INIT] Spawning authd...\n");
        elf_load(g_authd_module, g_authd_module_size, "authd");
    }

    /* Mount default filesystems */
    kprintf_color(0xFF00DDFF, "[INIT] Mounting filesystems...\n");
    /* initrd is already mounted by vfs_init(), add more here */

    /* Spawn autostart services */
    for (usize i = 0; i < sizeof(g_services)/sizeof(g_services[0]); i++) {
        if (g_services[i].autostart) {
            kprintf_color(0xFF888888, "[INIT] Starting service: %s\n", g_services[i].name);
            sched_spawn(g_services[i].name, g_services[i].fn, NULL, 1);
            g_services[i].running = true;
        }
    }

    kprintf_color(0xFF00FF88, "[INIT] System ready — %lu services started\n",
                  sizeof(g_services)/sizeof(g_services[0]));

    /* Idle loop — PID 1 must never exit */
    for (;;) {
        sched_yield();
        __asm__ volatile("hlt");
    }
}
