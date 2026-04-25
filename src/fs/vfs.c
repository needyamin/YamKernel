/* ============================================================================
 * YamKernel — Virtual File System (VFS) Framework
 * ============================================================================ */
#include "vfs.h"
#include "../lib/kprintf.h"
#include "../nexus/graph.h"

void fat32_init(void) { kprintf_color(0xFF888888, "[FS] FAT32 module loaded\n"); }
void ext4_init(void)  { kprintf_color(0xFF888888, "[FS] ext4 module loaded\n"); }
void ntfs_init(void)  { kprintf_color(0xFF888888, "[FS] NTFS module loaded\n"); }

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
