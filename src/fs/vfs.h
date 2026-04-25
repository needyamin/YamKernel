/* ============================================================================
 * YamKernel — Virtual File System (VFS)
 * Defines the skeleton interfaces for FAT32, ext4, NTFS.
 * ============================================================================ */
#ifndef _FS_VFS_H
#define _FS_VFS_H

#include <nexus/types.h>

void vfs_init(void);

void vfs_mount(const char *device, const char *mount_point, const char *fs_type);
void vfs_unmount(const char *mount_point);

/* Filesystem skeletons */
void fat32_init(void);
void ext4_init(void);
void ntfs_init(void);

#endif
