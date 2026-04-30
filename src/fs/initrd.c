/* ============================================================================
 * YamKernel — In-Memory Initial RAM Disk (initrd)
 * ============================================================================ */
#include "initrd.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

static initrd_entry_t g_entries[INITRD_MAX_FILES];
static int g_entry_count = 0;

/* Built-in /etc/passwd content */
static const char g_passwd_content[] =
    "root:x:0:0:root:/root:/bin/sh\n"
    "guest:x:1000:1000:guest:/home/guest:/bin/sh\n"
    "yamuser:x:1001:1001:YamOS User:/home/yamuser:/bin/sh\n";

/* Built-in /etc/fstab content */
static const char g_fstab_content[] =
    "# <device> <mountpoint> <type> <options>\n"
    "initrd / initrd defaults 0 0\n"
    "/dev/sda1 /mnt/disk fat32 defaults 0 1\n";

/* Built-in /etc/hostname */
static const char g_hostname_content[] = "yamkernel\n";

/* Built-in /etc/motd */
static const char g_motd_content[] =
    "\n"
    "  Welcome to YamOS v0.4.0\n"
    "  Graph-Based Adaptive Operating System\n"
    "  Type 'help' for available commands.\n\n";

void initrd_register(const char *path, void *data, usize size, bool is_dir) {
    if (g_entry_count >= INITRD_MAX_FILES) return;
    initrd_entry_t *e = &g_entries[g_entry_count++];
    strncpy(e->name, path, INITRD_NAME_MAX - 1);
    e->name[INITRD_NAME_MAX - 1] = '\0';
    e->data = (u8 *)data;
    e->size = size;
    e->is_dir = is_dir;
}

void initrd_init(void) {
    g_entry_count = 0;

    /* Register directory structure */
    initrd_register("/",             NULL, 0, true);
    initrd_register("/bin",          NULL, 0, true);
    initrd_register("/etc",          NULL, 0, true);
    initrd_register("/dev",          NULL, 0, true);
    initrd_register("/proc",         NULL, 0, true);
    initrd_register("/tmp",          NULL, 0, true);
    initrd_register("/home",         NULL, 0, true);
    initrd_register("/home/root",    NULL, 0, true);
    initrd_register("/home/guest",   NULL, 0, true);
    initrd_register("/mnt",          NULL, 0, true);
    initrd_register("/var",          NULL, 0, true);
    initrd_register("/var/log",      NULL, 0, true);

    /* Register built-in config files */
    initrd_register("/etc/passwd",   (void*)g_passwd_content,   sizeof(g_passwd_content)-1,  false);
    initrd_register("/etc/fstab",    (void*)g_fstab_content,    sizeof(g_fstab_content)-1,   false);
    initrd_register("/etc/hostname", (void*)g_hostname_content, sizeof(g_hostname_content)-1, false);
    initrd_register("/etc/motd",     (void*)g_motd_content,     sizeof(g_motd_content)-1,    false);

    kprintf_color(0xFF00FF88, "[INITRD] Initialized: %d entries (/etc/passwd, /etc/fstab, ...)\n",
                  g_entry_count);
}

void *initrd_lookup(const char *path, usize *size_out) {
    for (int i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].name, path) == 0) {
            if (size_out) *size_out = g_entries[i].size;
            return g_entries[i].data;
        }
    }
    return NULL;
}

bool initrd_is_dir(const char *path) {
    for (int i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].name, path) == 0)
            return g_entries[i].is_dir;
    }
    return false;
}

int initrd_readdir(const char *dirpath, initrd_entry_t *out, int max) {
    int count = 0;
    usize dirlen = strlen(dirpath);
    /* Normalize: strip trailing slash unless root */
    char norm[INITRD_NAME_MAX];
    strncpy(norm, dirpath, INITRD_NAME_MAX - 1);
    usize nlen = strlen(norm);
    if (nlen > 1 && norm[nlen-1] == '/') norm[--nlen] = '\0';

    for (int i = 0; i < g_entry_count && count < max; i++) {
        const char *name = g_entries[i].name;
        usize nlen2 = strlen(name);
        /* Entry must start with dirpath+/ and have no further slash */
        if (nlen2 <= nlen) continue;
        if (strncmp(name, norm, nlen) != 0) continue;
        if (name[nlen] != '/') continue;
        /* Check no further slash after the separator */
        const char *rest = name + nlen + 1;
        bool deeper = false;
        for (const char *p = rest; *p; p++) { if (*p == '/') { deeper = true; break; } }
        if (deeper) continue;
        out[count++] = g_entries[i];
    }
    (void)dirlen;
    return count;
}
