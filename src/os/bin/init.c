/* ============================================================================
 * YamKernel — PID 1 Init Process
 * Spawns services, mounts filesystems, manages runlevel transitions.
 * ============================================================================ */
#include "../../sched/sched.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"
#include "../../fs/vfs.h"
#include "../../fs/elf.h"
#include "../../fs/initrd.h"
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

extern void *g_authd_module;
extern usize g_authd_module_size;
extern void *g_hello_module;
extern usize g_hello_module_size;

static bool init_write_file(const char *path, const void *data, usize size) {
    int fd = sys_open(path, 0x40 | 0x200 | 0x2);
    if (fd < 0) return false;
    isize n = sys_write(fd, data, size);
    sys_close(fd);
    return n == (isize)size;
}

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
    if (g_hello_module) {
        initrd_register("/bin/hello", g_hello_module, g_hello_module_size, false);
        kprintf_color(0xFF00FF88, "[INIT] Registered /bin/hello from boot module (%lu bytes)\n",
                      (u64)g_hello_module_size);
        const char *argv[] = { "/bin/hello", "--spawn-probe", NULL };
        const char *envp[] = { "YAMOS_BOOT=1", "YAMOS_USER=root", NULL };
        i64 pid = elf_spawn_resolved_argv_envp("hello", 2, argv, envp);
        kprintf_color(pid >= 0 ? 0xFF00FF88 : 0xFFFF3333,
                      "[INIT] VFS spawn hello -> pid=%ld\n", pid);
        if (pid > 0) {
            i32 status = 0;
            i64 waited = sched_waitpid(pid, &status, 0);
            kprintf_color(waited == pid ? 0xFF00FF88 : 0xFFFF3333,
                          "[INIT] waitpid hello -> pid=%ld status=0x%x exit=%d\n",
                          waited, status, (status >> 8) & 0xFF);
        }
        if (init_write_file("/usr/local/bin/hello-local", g_hello_module, g_hello_module_size)) {
            kprintf_color(0xFF00FF88,
                          "[INIT] Installed persistent sample app /usr/local/bin/hello-local (%lu bytes)\n",
                          (u64)g_hello_module_size);
            const char *local_argv[] = { "hello-local", "--local-bin", NULL };
            const char *local_envp[] = { "YAMOS_BOOT=1", "YAMOS_APP_SOURCE=/usr/local/bin", NULL };
            i64 local_pid = elf_spawn_resolved_argv_envp("hello-local", 2, local_argv, local_envp);
            kprintf_color(local_pid >= 0 ? 0xFF00FF88 : 0xFFFF3333,
                          "[INIT] VFS spawn hello-local -> pid=%ld\n", local_pid);
            if (local_pid > 0) {
                i32 status = 0;
                i64 waited = sched_waitpid(local_pid, &status, 0);
                kprintf_color(waited == local_pid ? 0xFF00FF88 : 0xFFFF3333,
                              "[INIT] waitpid hello-local -> pid=%ld status=0x%x exit=%d\n",
                              waited, status, (status >> 8) & 0xFF);
            }
        } else {
            kprintf_color(0xFFFF3333,
                          "[INIT] Could not install /usr/local/bin/hello-local\n");
        }
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
