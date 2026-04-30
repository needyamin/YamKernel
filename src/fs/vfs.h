/* ============================================================================
 * YamKernel — Virtual File System (VFS)
 * Defines the skeleton interfaces for FAT32, ext4, NTFS.
 * ============================================================================ */
#ifndef _FS_VFS_H
#define _FS_VFS_H

#include <nexus/types.h>

/* POSIX Standard VFS Types */
struct file;
struct inode;
struct dentry;

typedef struct file_operations {
    isize (*read)(struct file *file, void *buf, usize count);
    isize (*write)(struct file *file, const void *buf, usize count);
    int   (*mmap)(struct file *file, u64 *phys_addr, usize size, usize offset);
    int   (*poll)(struct file *file);
    int   (*close)(struct file *file);
} file_operations_t;

typedef struct inode {
    u32               ino;
    u32               mode;
    u32               uid;
    u32               gid;
    usize             size;
    file_operations_t *fops;
    void              *private_data; /* fs-specific data */
} inode_t;

typedef struct dentry {
    char            name[256];
    inode_t         *inode;
    struct dentry   *parent;
    struct dentry   *next_child;
    struct dentry   *first_child;
} dentry_t;

typedef struct file {
    dentry_t          *dentry;
    file_operations_t *fops;
    usize             offset;       /* current seek position */
    usize             pos;          /* alias kept for compatibility */
    u32               flags;        /* O_RDONLY, O_WRONLY, O_NONBLOCK */
    u32               ref_count;
    int               mount_idx;   /* which VFS mount this file belongs to */
    char              path[256];    /* full absolute path */
    void              *private_data; /* pipe/socket specific data */
} file_t;

void vfs_init(void);

void vfs_mount(const char *device, const char *mount_point, const char *fs_type);
void vfs_unmount(const char *mount_point);

/* File Descriptor APIs */
int fd_alloc(file_t *file);
file_t *fd_get(int fd);
void fd_free(int fd);

int    sys_open(const char *pathname, u32 flags);
int    sys_close(int fd);
isize  sys_read(int fd, void *buf, usize count);
isize  sys_write(int fd, const void *buf, usize count);
isize  sys_lseek(int fd, isize offset, int whence);

/* fb console output for stdout/stderr */
void fb_puts_user(const char *s, usize len);

/* Filesystem skeletons */
void fat32_init(void);
void ext4_init(void);
void ntfs_init(void);

#endif
