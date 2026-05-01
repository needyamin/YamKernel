/* ============================================================================
 * YamKernel — Virtual File System (VFS) — Full Implementation
 * Real mount table, path resolution, file descriptor routing.
 * ============================================================================ */
#include "vfs.h"
#include "initrd.h"
#include "fat32.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../mem/heap.h"
#include "../sched/sched.h"
#include "../nexus/graph.h"

/* ---- Mount Table ---- */
#define VFS_MAX_MOUNTS 16

typedef enum { FS_NONE=0, FS_INITRD, FS_FAT32, FS_DEVFS, FS_PROCFS } fs_type_e;

typedef struct {
    char        mount_point[128];
    fs_type_e   type;
    void       *priv;    /* fs-specific data (fat32_vol_t*, etc.) */
    bool        active;
} vfs_mount_t;

static vfs_mount_t g_mounts[VFS_MAX_MOUNTS];
static int g_mount_count = 0;

/* ---- Forward declarations for devfs/procfs ---- */
extern isize devfs_read(const char *path, void *buf, usize count, usize offset);
extern isize devfs_write(const char *path, const void *buf, usize count);
extern bool  devfs_exists(const char *path);
extern isize procfs_read(const char *path, void *buf, usize count);

/* ---- Mount point resolution ---- */

/* Find the best-matching mount for a given path.
 * Returns the mount index and sets *rel_path to the path relative to the mount. */
static int vfs_find_mount(const char *path, const char **rel_path) {
    int best = -1;
    usize best_len = 0;
    for (int i = 0; i < g_mount_count; i++) {
        if (!g_mounts[i].active) continue;
        usize mplen = strlen(g_mounts[i].mount_point);
        if (strncmp(path, g_mounts[i].mount_point, mplen) == 0) {
            if (mplen > best_len) {
                best_len = mplen;
                best = i;
            }
        }
    }
    if (best >= 0 && rel_path) {
        const char *p = path + best_len;
        /* Ensure relative path starts with / */
        if (*p != '/' && best_len > 1) {
            /* mount point is /foo, path is /foo/bar — rel = /bar */
            *rel_path = p - 0;  /* already has no slash */
        } else {
            *rel_path = (*p == '\0') ? "/" : p;
        }
        if ((*rel_path)[0] == '\0') *rel_path = "/";
    }
    return best;
}

/* ---- VFS Public API ---- */

void vfs_mount(const char *device, const char *mount_point, const char *fs_type) {
    if (g_mount_count >= VFS_MAX_MOUNTS) {
        kprintf_color(0xFFFF3333, "[VFS] Mount table full!\n");
        return;
    }
    vfs_mount_t *m = &g_mounts[g_mount_count];
    strncpy(m->mount_point, mount_point, 127);
    m->active = true;

    if (strcmp(fs_type, "initrd") == 0) {
        m->type = FS_INITRD;
        m->priv = NULL;
        kprintf_color(0xFF00FF88, "[VFS] Mounted initrd at '%s'\n", mount_point);
    } else if (strcmp(fs_type, "devfs") == 0) {
        m->type = FS_DEVFS;
        m->priv = NULL;
        kprintf_color(0xFF00FF88, "[VFS] Mounted devfs at '%s'\n", mount_point);
    } else if (strcmp(fs_type, "procfs") == 0) {
        m->type = FS_PROCFS;
        m->priv = NULL;
        kprintf_color(0xFF00FF88, "[VFS] Mounted procfs at '%s'\n", mount_point);
    } else if (strcmp(fs_type, "fat32") == 0) {
        /* device is treated as a pointer to disk data (for ramdisk scenario) */
        fat32_vol_t *vol = (fat32_vol_t *)kmalloc(sizeof(fat32_vol_t));
        if (vol && device) {
            /* device points to disk image address (uintptr cast) */
            /* For now, mark as available but not yet backed */
            memset(vol, 0, sizeof(*vol));
            m->type = FS_FAT32;
            m->priv = vol;
        }
        kprintf_color(0xFF00FF88, "[VFS] FAT32 registered at '%s'\n", mount_point);
    } else {
        kprintf_color(0xFFFF8800, "[VFS] Unknown fs type: %s\n", fs_type);
        m->active = false;
        return;
    }
    g_mount_count++;
}

void vfs_unmount(const char *mount_point) {
    for (int i = 0; i < g_mount_count; i++) {
        if (strcmp(g_mounts[i].mount_point, mount_point) == 0) {
            g_mounts[i].active = false;
            kprintf_color(0xFF888888, "[VFS] Unmounted '%s'\n", mount_point);
            return;
        }
    }
}

/* ---- File Operations ---- */

static isize vfs_do_read(int mount_idx, const char *rel_path, void *buf, usize count, usize offset) {
    vfs_mount_t *m = &g_mounts[mount_idx];
    switch (m->type) {
        case FS_INITRD: {
            usize size;
            void *data = initrd_lookup(rel_path, &size);
            if (!data) return -1;
            if (offset >= size) return 0;
            usize avail = size - offset;
            if (avail > count) avail = count;
            memcpy(buf, (u8 *)data + offset, avail);
            return (isize)avail;
        }
        case FS_FAT32: {
            fat32_vol_t *vol = (fat32_vol_t *)m->priv;
            if (!vol || !vol->mounted) return -1;
            /* Read entire file into buf; offset handling */
            u8 *tmp = (u8 *)kmalloc(count + offset + 4096);
            if (!tmp) return -1;
            isize total = fat32_read_file(vol, rel_path, tmp, count + offset);
            if (total < 0) { kfree(tmp); return -1; }
            if ((usize)total <= offset) { kfree(tmp); return 0; }
            usize avail = (usize)total - offset;
            if (avail > count) avail = count;
            memcpy(buf, tmp + offset, avail);
            kfree(tmp);
            return (isize)avail;
        }
        case FS_DEVFS:
            return devfs_read(rel_path, buf, count, offset);
        case FS_PROCFS:
            return procfs_read(rel_path, buf, count);
        default:
            return -1;
    }
}

static isize vfs_do_write(int mount_idx, const char *rel_path, const void *buf, usize count) {
    vfs_mount_t *m = &g_mounts[mount_idx];
    switch (m->type) {
        case FS_FAT32: {
            fat32_vol_t *vol = (fat32_vol_t *)m->priv;
            if (!vol || !vol->mounted) return -1;
            return fat32_write_file(vol, rel_path, buf, count);
        }
        case FS_DEVFS:
            return devfs_write(rel_path, buf, count);
        case FS_INITRD:
            return -1; /* Read-only */
        default:
            return -1;
    }
}

/* ---- File Descriptor Layer ---- */

int fd_alloc(file_t *file) {
    task_t *t = sched_current();
    if (!t) return -1;
    for (int i = 0; i < 128; i++) {
        if (!t->fd_table[i]) { t->fd_table[i] = file; return i; }
    }
    return -1;
}
file_t *fd_get(int fd) {
    if (fd < 0 || fd >= 128) return NULL;
    task_t *t = sched_current();
    return t ? t->fd_table[fd] : NULL;
}
void fd_free(int fd) {
    if (fd < 0 || fd >= 128) return;
    task_t *t = sched_current();
    if (t) t->fd_table[fd] = NULL;
}

static int fd_alloc_from(file_t *file, int start) {
    task_t *t = sched_current();
    if (!t) return -1;
    if (start < 0) start = 0;
    if (start >= 128) return -1;
    for (int i = start; i < 128; i++) {
        if (!t->fd_table[i]) {
            t->fd_table[i] = file;
            return i;
        }
    }
    return -1;
}

static int fd_install_at(file_t *file, int fd) {
    task_t *t = sched_current();
    if (!t || fd < 0 || fd >= 128) return -1;
    t->fd_table[fd] = file;
    return fd;
}

static file_t *stdio_file_create(int fd) {
    if (fd < 0 || fd > 2) return NULL;
    file_t *f = (file_t *)kmalloc(sizeof(file_t));
    if (!f) return NULL;
    memset(f, 0, sizeof(file_t));
    f->flags = (fd == 0) ? 0 : 1;
    f->ref_count = 1;
    f->mount_idx = -1;
    strncpy(f->path, "/dev/console", sizeof(f->path) - 1);
    return f;
}

static file_t *fd_get_or_stdio(int fd) {
    file_t *f = fd_get(fd);
    if (f) return f;
    if (fd >= 0 && fd <= 2) return stdio_file_create(fd);
    return NULL;
}

/* ---- Syscall Handlers ---- */

int sys_open(const char *pathname, u32 flags) {
    if (!pathname) return -1;
    const char *rel;
    int midx = vfs_find_mount(pathname, &rel);
    if (midx < 0) return -1;

    /* Allocate a file_t */
    file_t *f = (file_t *)kmalloc(sizeof(file_t));
    if (!f) return -1;
    memset(f, 0, sizeof(file_t));
    f->offset = 0;
    f->flags  = flags;
    f->ref_count = 1;
    strncpy(f->path, pathname, 255);
    f->mount_idx = midx;

    int fd = fd_alloc(f);
    if (fd < 0) { kfree(f); return -1; }
    return fd;
}

int sys_close(int fd) {
    file_t *f = fd_get(fd);
    if (!f) return -1;
    fd_free(fd);
    if (f->ref_count > 1) {
        f->ref_count--;
        return 0;
    }
    if (f->fops && f->fops->close) f->fops->close(f);
    kfree(f);
    return 0;
}

int sys_dup(int fd) {
    bool created_stdio_file = fd_get(fd) == NULL;
    file_t *f = fd_get_or_stdio(fd);
    if (!f) return -1;

    int newfd = fd_alloc_from(f, fd <= 2 ? 3 : 0);
    if (newfd < 0) {
        if (created_stdio_file) kfree(f);
        return -1;
    }
    if (!created_stdio_file) f->ref_count++;
    kprintf("[VFS] dup fd%d -> fd%d path='%s' refs=%u\n", fd, newfd, f->path, f->ref_count);
    return newfd;
}

int sys_dup2(int oldfd, int newfd) {
    if (newfd < 0 || newfd >= 128) return -1;
    if (oldfd == newfd) return fd_get(oldfd) || (oldfd >= 0 && oldfd <= 2) ? newfd : -1;

    bool created_stdio_file = fd_get(oldfd) == NULL;
    file_t *f = fd_get_or_stdio(oldfd);
    if (!f) return -1;

    if (fd_get(newfd)) sys_close(newfd);
    if (fd_install_at(f, newfd) < 0) {
        if (created_stdio_file) kfree(f);
        return -1;
    }
    if (!created_stdio_file) f->ref_count++;
    kprintf("[VFS] dup2 fd%d -> fd%d path='%s' refs=%u\n", oldfd, newfd, f->path, f->ref_count);
    return newfd;
}

isize sys_read(int fd, void *buf, usize count) {
    file_t *f = fd_get(fd);
    if (!f) return -1;
    /* If custom fops, use them (for device files) */
    if (f->fops && f->fops->read) return f->fops->read(f, buf, count);
    /* Otherwise route through VFS mount */
    const char *rel;
    int midx = vfs_find_mount(f->path, &rel);
    if (midx < 0) return -1;
    isize n = vfs_do_read(midx, rel, buf, count, f->offset);
    if (n > 0) f->offset += (usize)n;
    return n;
}

isize sys_write(int fd, const void *buf, usize count) {
    file_t *f = fd_get(fd);
    if (!f) {
        /* fd 1 and 2 are stdout/stderr — write to framebuffer console */
        if (fd == 1 || fd == 2) {
            extern void fb_puts_user(const char *s, usize len);
            fb_puts_user((const char *)buf, count);
            return (isize)count;
        }
        return -1;
    }
    if (f->fops && f->fops->write) return f->fops->write(f, buf, count);
    const char *rel;
    int midx = vfs_find_mount(f->path, &rel);
    if (midx < 0) return -1;
    isize n = vfs_do_write(midx, rel, buf, count);
    if (n > 0) f->offset += (usize)n;
    return n;
}

/* ---- VFS Init ---- */

void vfs_init(void) {
    kprintf_color(0xFF00DDFF, "[VFS] Initializing Virtual File System...\n");
    memset(g_mounts, 0, sizeof(g_mounts));
    g_mount_count = 0;

    /* Initialize and mount initrd as root */
    initrd_init();
    vfs_mount("initrd", "/", "initrd");
    vfs_mount("devfs",  "/dev",  "devfs");
    vfs_mount("procfs", "/proc", "procfs");

    /* Register in YamGraph */
    yam_node_id_t vfs_node = yamgraph_node_create(YAM_NODE_NAMESPACE, "fs", NULL);
    yamgraph_edge_link(0, vfs_node, YAM_EDGE_OWNS, YAM_PERM_ALL);

    kprintf_color(0xFF00FF88, "[VFS] Ready: %d mount points active\n", g_mount_count);
}
