/* ============================================================================
 * YamKernel — Virtual File System (VFS) Framework
 * ============================================================================ */
#include "vfs.h"
#include "../lib/kprintf.h"
#include "../nexus/graph.h"

void fat32_init(void) { kprintf_color(0xFF888888, "[FS] FAT32 module loaded\n"); }
void ext4_init(void)  { kprintf_color(0xFF888888, "[FS] ext4 module loaded\n"); }
void ntfs_init(void)  { kprintf_color(0xFF888888, "[FS] NTFS module loaded\n"); }

#include "../sched/sched.h"
#include "../mem/heap.h"

int fd_alloc(file_t *file) {
    task_t *t = sched_current();
    if (!t) return -1;
    for (int i = 0; i < 128; i++) {
        if (t->fd_table[i] == NULL) {
            t->fd_table[i] = file;
            return i;
        }
    }
    return -1;
}

file_t *fd_get(int fd) {
    if (fd < 0 || fd >= 128) return NULL;
    task_t *t = sched_current();
    if (!t) return NULL;
    return t->fd_table[fd];
}

void fd_free(int fd) {
    if (fd < 0 || fd >= 128) return;
    task_t *t = sched_current();
    if (!t) return;
    t->fd_table[fd] = NULL;
}

int sys_open(const char *pathname, u32 flags) {
    /* Stub for path resolution */
    (void)pathname; (void)flags;
    return -1;
}

int sys_close(int fd) {
    file_t *f = fd_get(fd);
    if (!f) return -1;
    if (f->fops && f->fops->close) {
        f->fops->close(f);
    }
    kfree(f);
    fd_free(fd);
    return 0;
}

isize sys_read(int fd, void *buf, usize count) {
    file_t *f = fd_get(fd);
    if (!f || !f->fops || !f->fops->read) return -1;
    return f->fops->read(f, buf, count);
}

isize sys_write(int fd, const void *buf, usize count) {
    file_t *f = fd_get(fd);
    if (!f || !f->fops || !f->fops->write) return -1;
    return f->fops->write(f, buf, count);
}

void vfs_mount(const char *device, const char *mount_point, const char *fs_type) {
    (void)device; (void)mount_point; (void)fs_type;
}

void vfs_unmount(const char *mount_point) {
    (void)mount_point;
}

void vfs_init(void) {
    kprintf_color(0xFF00DDFF, "[VFS] Initializing Virtual File System...\n");
    
    fat32_init();
    ext4_init();
    ntfs_init();

    /* Register VFS in YamGraph */
    yam_node_id_t vfs_node = yamgraph_node_create(YAM_NODE_NAMESPACE, "fs", NULL);
    yamgraph_edge_link(0, vfs_node, YAM_EDGE_OWNS, YAM_PERM_ALL);
}
