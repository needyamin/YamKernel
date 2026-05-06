/* ============================================================================
 * YamKernel — Virtual File System (VFS) — Full Implementation
 * Real mount table, path resolution, file descriptor routing.
 * ============================================================================ */
#include "vfs.h"
#include "initrd.h"
#include "fat32.h"
#include "bcache.h"
#include "../drivers/block/block.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../mem/heap.h"
#include "../sched/sched.h"
#include "../nexus/graph.h"

/* ---- Mount Table ---- */
#define VFS_MAX_MOUNTS 16

typedef enum { FS_NONE=0, FS_INITRD, FS_FAT32, FS_DEVFS, FS_PROCFS, FS_RAMFS } fs_type_e;

#define VFS_BLOCK_FAT32_MAX_BYTES (64 * 1024 * 1024)
#define VFS_O_CREAT 0x0040
#define VFS_O_EXCL 0x0080
#define VFS_O_TRUNC 0x0200
#define VFS_O_APPEND 0x0400

#define RAMFS_MAX_FILES 128
#define RAMFS_MAX_DATA  (16 * 1024 * 1024)

typedef struct {
    char name[192];
    u8  *data;
    usize size;
    usize cap;
    bool is_dir;
    bool used;
} ramfs_node_t;

typedef struct {
    ramfs_node_t nodes[RAMFS_MAX_FILES];
    usize bytes_used;
} ramfs_t;

typedef struct {
    char        mount_point[128];
    fs_type_e   type;
    void       *priv;    /* fs-specific data (fat32_vol_t*, etc.) */
    block_device_t *block_dev;
    u8         *block_image;
    u64         block_start_lba;
    u64         block_sector_count;
    bool        block_backed;
    bool        active;
} vfs_mount_t;

static vfs_mount_t g_mounts[VFS_MAX_MOUNTS];
static int g_mount_count = 0;
static ramfs_t g_ramfs_tmp;
static ramfs_t g_ramfs_var;
static ramfs_t g_ramfs_usr_local;
static ramfs_t g_ramfs_opt;
static ramfs_t g_ramfs_home;
static ramfs_t g_ramfs_mnt;
static vfs_mount_t *g_primary_block_fat32_mount;

typedef struct PACKED {
    u8 status;
    u8 chs_first[3];
    u8 type;
    u8 chs_last[3];
    u32 lba_first;
    u32 sector_count;
} mbr_partition_t;

typedef struct PACKED {
    u8 bootstrap[446];
    mbr_partition_t part[4];
    u16 signature;
} mbr_t;

typedef struct PACKED {
    u8 signature[8];
    u32 revision;
    u32 header_size;
    u32 header_crc32;
    u32 reserved;
    u64 current_lba;
    u64 backup_lba;
    u64 first_usable_lba;
    u64 last_usable_lba;
    u8 disk_guid[16];
    u64 part_entry_lba;
    u32 part_entry_count;
    u32 part_entry_size;
    u32 part_array_crc32;
} gpt_header_t;

typedef struct PACKED {
    u8 type_guid[16];
    u8 unique_guid[16];
    u64 first_lba;
    u64 last_lba;
    u64 attrs;
    u16 name[36];
} gpt_entry_t;

static const u8 GPT_BASIC_DATA_GUID[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

static bool is_abs_path(const char *path) {
    return path && path[0] == '/';
}

static void path_normalize(const char *input, char *out, usize cap) {
    char temp[256];
    const char *parts[32];
    u32 count = 0;
    if (!out || cap == 0) return;
    out[0] = 0;
    if (!input || !*input) {
        strncpy(out, "/", cap - 1);
        out[cap - 1] = 0;
        return;
    }

    strncpy(temp, input, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = 0;
    char *p = temp;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char *start = p;
        while (*p && *p != '/') p++;
        if (*p) *p++ = 0;
        if (strcmp(start, ".") == 0) continue;
        if (strcmp(start, "..") == 0) {
            if (count > 0) count--;
            continue;
        }
        if (count < sizeof(parts) / sizeof(parts[0])) parts[count++] = start;
    }

    usize n = 0;
    out[n++] = '/';
    out[n] = 0;
    for (u32 i = 0; i < count && n + 1 < cap; i++) {
        if (i > 0 && n + 1 < cap) out[n++] = '/';
        const char *s = parts[i];
        while (*s && n + 1 < cap) out[n++] = *s++;
        out[n] = 0;
    }
}

static void vfs_resolve_path(const char *path, char *out, usize cap) {
    char joined[256];
    task_t *t = sched_current();
    if (!path || !*path) {
        path_normalize(t && t->cwd[0] ? t->cwd : "/", out, cap);
        return;
    }
    if (is_abs_path(path)) {
        path_normalize(path, out, cap);
        return;
    }
    const char *cwd = (t && t->cwd[0]) ? t->cwd : "/";
    if (strcmp(cwd, "/") == 0) ksnprintf(joined, sizeof(joined), "/%s", path);
    else ksnprintf(joined, sizeof(joined), "%s/%s", cwd, path);
    path_normalize(joined, out, cap);
}

/* ---- Forward declarations for devfs/procfs ---- */
extern isize devfs_read(const char *path, void *buf, usize count, usize offset);
extern isize devfs_write(const char *path, const void *buf, usize count);
extern bool  devfs_exists(const char *path);
extern isize procfs_read(const char *path, void *buf, usize count);

/* ---- Mount point resolution ---- */

static void path_parent(const char *path, char *parent, usize parent_cap,
                        char *base, usize base_cap);
static int block_rw_all(block_device_t *dev, bool write, u64 lba, u32 sectors, void *buf);
static bool vfs_mount_fat32_block(block_device_t *dev, u64 start_lba,
                                  u64 sector_count, const char *mount_point);
static void vfs_promote_home_to_block(vfs_mount_t *source);
static void vfs_promote_var_to_block(vfs_mount_t *source);
static void vfs_promote_usr_local_to_block(vfs_mount_t *source);

/* Find the best-matching mount for a given path.
 * Returns the mount index and sets *rel_path to the path relative to the mount. */
static int vfs_find_mount(const char *path, const char **rel_path) {
    int best = -1;
    usize best_len = 0;
    for (int i = 0; i < g_mount_count; i++) {
        if (!g_mounts[i].active) continue;
        usize mplen = strlen(g_mounts[i].mount_point);
        if (strncmp(path, g_mounts[i].mount_point, mplen) == 0 &&
            (mplen == 1 || path[mplen] == '\0' || path[mplen] == '/')) {
            if (mplen > best_len) {
                best_len = mplen;
                best = i;
            }
        }
    }
    if (best >= 0 && rel_path) {
        const char *p = path + best_len;
        if (best_len == 1) {
            *rel_path = (*p == '\0') ? "/" : path;
            return best;
        }
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

static bool mount_child_at(const char *parent, u32 index, vfs_dirent_t *out) {
    if (!parent || !out) return false;
    u32 seen = 0;
    usize parent_len = strlen(parent);

    for (int i = 0; i < g_mount_count; i++) {
        vfs_mount_t *m = &g_mounts[i];
        if (!m->active || strcmp(m->mount_point, "/") == 0) continue;

        char mount_parent[128];
        char mount_name[128];
        path_parent(m->mount_point, mount_parent, sizeof(mount_parent),
                    mount_name, sizeof(mount_name));
        if (strcmp(mount_parent, parent) != 0) continue;

        bool nested_child = false;
        for (int j = 0; j < g_mount_count; j++) {
            if (i == j || !g_mounts[j].active) continue;
            usize other_len = strlen(g_mounts[j].mount_point);
            if (other_len <= parent_len) continue;
            if (strncmp(g_mounts[j].mount_point, parent, parent_len) != 0) continue;
            if (g_mounts[j].mount_point[parent_len == 1 ? 1 : parent_len] != '/') continue;
            if (strncmp(m->mount_point, g_mounts[j].mount_point, other_len) == 0 &&
                m->mount_point[other_len] == '/') {
                nested_child = true;
                break;
            }
        }
        if (nested_child) continue;

        if (seen == index) {
            memset(out, 0, sizeof(*out));
            strncpy(out->name, mount_name, sizeof(out->name) - 1);
            out->is_dir = true;
            return true;
        }
        seen++;
    }
    return false;
}

static bool mount_has_child(const char *parent) {
    vfs_dirent_t scratch;
    return mount_child_at(parent, 0, &scratch);
}

static bool mbr_type_is_fat32(u8 type) {
    return type == 0x0B || type == 0x0C || type == 0x1B || type == 0x1C;
}

static bool gpt_type_is_fat32_compatible(const u8 guid[16]) {
    return memcmp(guid, GPT_BASIC_DATA_GUID, 16) == 0;
}

static bool vfs_try_mount_gpt_fat32(block_device_t *dev, const char *mount_point) {
    u8 header_sector[512];
    if (!dev || !mount_point || dev->sector_size != 512 || dev->sector_count < 34) return false;
    if (block_rw_all(dev, false, 1, 1, header_sector) < 0) return false;

    gpt_header_t *gpt = (gpt_header_t *)header_sector;
    if (memcmp(gpt->signature, "EFI PART", 8) != 0) return false;
    if (gpt->header_size < 92 || gpt->part_entry_size < sizeof(gpt_entry_t)) return false;
    if (gpt->part_entry_count == 0 || gpt->part_entry_count > 256) return false;

    u64 entry_bytes = (u64)gpt->part_entry_count * gpt->part_entry_size;
    u64 entry_sectors = (entry_bytes + dev->sector_size - 1) / dev->sector_size;
    if (entry_sectors == 0 || entry_sectors > 64) return false;
    if (gpt->part_entry_lba + entry_sectors > dev->sector_count) return false;

    u8 *entries = (u8 *)kmalloc((usize)(entry_sectors * dev->sector_size));
    if (!entries) return false;
    if (block_rw_all(dev, false, gpt->part_entry_lba, (u32)entry_sectors, entries) < 0) {
        kfree(entries);
        return false;
    }

    bool mounted = false;
    for (u32 i = 0; i < gpt->part_entry_count; i++) {
        gpt_entry_t *entry = (gpt_entry_t *)(entries + (usize)i * gpt->part_entry_size);
        if (!gpt_type_is_fat32_compatible(entry->type_guid)) continue;
        if (entry->first_lba == 0 || entry->last_lba < entry->first_lba) continue;
        if (entry->last_lba >= dev->sector_count) continue;
        u64 sectors = entry->last_lba - entry->first_lba + 1;
        if (vfs_mount_fat32_block(dev, entry->first_lba, sectors, mount_point)) {
            kprintf("[VFS] GPT FAT32-compatible partition %u selected on %s\n", i + 1, dev->name);
            mounted = true;
            break;
        }
    }

    kfree(entries);
    return mounted;
}

static int block_rw_all(block_device_t *dev, bool write, u64 lba, u32 sectors, void *buf) {
    if (!dev || !buf) return -1;
    u32 max_chunk = 128;
    u8 *p = (u8 *)buf;
    while (sectors > 0) {
        u32 chunk = sectors > max_chunk ? max_chunk : sectors;
        int rc = write ? (dev->write ? dev->write(dev, lba, chunk, p) : -1)
                       : dev->read(dev, lba, chunk, p);
        if (rc < 0) return -1;
        lba += chunk;
        sectors -= chunk;
        p += (usize)chunk * dev->sector_size;
    }
    return 0;
}

static int vfs_flush_block_mount(vfs_mount_t *m) {
    if (!m || !m->block_backed || !m->block_dev) return 0;
    bcache_flush(m->block_dev);
    kprintf("[VFS] Flushed block-backed FAT32 mount '%s' -> %s\n",
            m->mount_point, m->block_dev->name);
    return 0;
}

static bool vfs_mount_fat32_block(block_device_t *dev, u64 start_lba,
                                  u64 sector_count, const char *mount_point) {
    if (!dev || !mount_point || g_mount_count >= VFS_MAX_MOUNTS) return false;
    if (dev->sector_size == 0 || sector_count == 0) return false;

    fat32_vol_t *vol = (fat32_vol_t *)kmalloc(sizeof(fat32_vol_t));
    if (!vol) return false;

    memset(vol, 0, sizeof(*vol));
    if (!fat32_mount(dev, start_lba, vol)) {
        kfree(vol);
        return false;
    }

    vfs_mount_t *m = &g_mounts[g_mount_count++];
    memset(m, 0, sizeof(*m));
    strncpy(m->mount_point, mount_point, sizeof(m->mount_point) - 1);
    m->type = FS_FAT32;
    m->priv = vol;
    m->block_dev = dev;
    m->block_image = NULL;
    m->block_start_lba = start_lba;
    m->block_sector_count = sector_count;
    m->block_backed = true;
    m->active = true;
    if (!g_primary_block_fat32_mount) g_primary_block_fat32_mount = m;

    kprintf("[VFS] Mounted block FAT32 %s lba=%lu sectors=%lu at '%s'\n",
            dev->name, start_lba, sector_count, mount_point);
    return true;
}

static bool fat32_ensure_dir(fat32_vol_t *vol, const char *path) {
    fat32_fileinfo_t info;
    if (!vol || !path) return false;
    if (fat32_lookup(vol, path, &info)) return info.is_dir;
    return fat32_mkdir(vol, path);
}

static bool fat32_ensure_dir_path(fat32_vol_t *vol, const char *path) {
    if (!vol || !path || path[0] != '/') return false;
    if (strcmp(path, "/") == 0) return true;

    char cur[256];
    usize n = 0;
    cur[n++] = '/';
    cur[n] = 0;
    const char *p = path + 1;
    bool ok = true;
    while (*p && n + 1 < sizeof(cur)) {
        while (*p == '/') p++;
        while (*p && *p != '/' && n + 1 < sizeof(cur)) {
            cur[n++] = *p++;
            cur[n] = 0;
        }
        if (strlen(cur) > 1 && !fat32_ensure_dir(vol, cur)) ok = false;
        if (*p == '/' && n + 1 < sizeof(cur)) {
            cur[n++] = '/';
            cur[n] = 0;
        }
    }
    return ok;
}

static vfs_mount_t *vfs_replace_mount_with_block_alias(const char *mount_point,
                                                       vfs_mount_t *source) {
    if (!mount_point || !source || source->type != FS_FAT32 || !source->priv) return NULL;
    vfs_mount_t *alias = NULL;
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active && strcmp(g_mounts[i].mount_point, mount_point) == 0) {
            alias = &g_mounts[i];
            break;
        }
    }
    if (!alias) {
        if (g_mount_count >= VFS_MAX_MOUNTS) return NULL;
        alias = &g_mounts[g_mount_count++];
    }

    memset(alias, 0, sizeof(*alias));
    strncpy(alias->mount_point, mount_point, sizeof(alias->mount_point) - 1);
    alias->type = FS_FAT32;
    alias->priv = source->priv;
    alias->block_dev = source->block_dev;
    alias->block_image = source->block_image;
    alias->block_start_lba = source->block_start_lba;
    alias->block_sector_count = source->block_sector_count;
    alias->block_backed = source->block_backed;
    alias->active = true;
    return alias;
}

static void vfs_promote_home_to_block(vfs_mount_t *source) {
    if (!source || source->type != FS_FAT32 || !source->priv) return;

    fat32_vol_t *vol = (fat32_vol_t *)source->priv;
    bool changed = false;
    changed |= fat32_ensure_dir_path(vol, "/root");
    changed |= fat32_ensure_dir_path(vol, "/guest");
    changed |= fat32_ensure_dir_path(vol, "/yamuser");
    if (changed && source->block_backed) (void)vfs_flush_block_mount(source);

    if (!vfs_replace_mount_with_block_alias("/home", source)) return;

    kprintf("[VFS] Promoted /home to persistent FAT32 volume %s\n",
            source->block_dev ? source->block_dev->name : "(unknown)");
}

static void vfs_promote_var_to_block(vfs_mount_t *source) {
    if (!source || source->type != FS_FAT32 || !source->priv) return;

    fat32_vol_t *vol = (fat32_vol_t *)source->priv;
    bool changed = false;
    changed |= fat32_ensure_dir_path(vol, "/cache/yamos/downloads");
    changed |= fat32_ensure_dir_path(vol, "/lib/yamos/packages");
    changed |= fat32_ensure_dir_path(vol, "/log");
    if (changed && source->block_backed) (void)vfs_flush_block_mount(source);

    if (!vfs_replace_mount_with_block_alias("/var", source)) return;

    kprintf("[VFS] Promoted /var to persistent FAT32 volume %s\n",
            source->block_dev ? source->block_dev->name : "(unknown)");
}

static void vfs_promote_usr_local_to_block(vfs_mount_t *source) {
    if (!source || source->type != FS_FAT32 || !source->priv) return;

    fat32_vol_t *vol = (fat32_vol_t *)source->priv;
    bool changed = false;
    changed |= fat32_ensure_dir_path(vol, "/bin");
    changed |= fat32_ensure_dir_path(vol, "/lib");
    changed |= fat32_ensure_dir_path(vol, "/share");
    if (changed && source->block_backed) (void)vfs_flush_block_mount(source);

    if (!vfs_replace_mount_with_block_alias("/usr/local", source)) return;

    kprintf("[VFS] Promoted /usr/local to persistent FAT32 volume %s\n",
            source->block_dev ? source->block_dev->name : "(unknown)");
}

static void vfs_mount_block_devices(void) {
    u8 sector[512];
    char mount_point[32];
    u32 mounted = 0;

    for (u32 i = 0; i < block_device_count(); i++) {
        block_device_t *dev = block_device_at(i);
        if (!dev || dev->sector_size != 512 || dev->sector_count == 0) continue;
        if (dev->sector_count > 0xFFFFFFFFULL) continue;
        if (block_rw_all(dev, false, 0, 1, sector) < 0) continue;

        ksnprintf(mount_point, sizeof(mount_point), "/mnt/%s", dev->name);
        mbr_t *mbr = (mbr_t *)sector;
        bool did_mount = false;
        if (mbr->signature == 0xAA55) {
            for (u32 p = 0; p < 4; p++) {
                if (mbr->part[p].type == 0xEE &&
                    vfs_try_mount_gpt_fat32(dev, mount_point)) {
                    did_mount = true;
                    mounted++;
                    break;
                }
            }
            for (u32 p = 0; p < 4; p++) {
                if (did_mount) break;
                mbr_partition_t *part = &mbr->part[p];
                if (!mbr_type_is_fat32(part->type) || part->sector_count == 0) continue;
                if ((u64)part->lba_first + part->sector_count > dev->sector_count) continue;
                if (vfs_mount_fat32_block(dev, part->lba_first, part->sector_count, mount_point)) {
                    did_mount = true;
                    mounted++;
                    break;
                }
            }
        }

        if (!did_mount && vfs_mount_fat32_block(dev, 0, dev->sector_count, mount_point)) {
            mounted++;
        }
    }

    if (mounted == 0) {
        kprintf("[VFS] No FAT32 block volumes mounted\n");
    } else {
        vfs_dirent_t probe;
        if (mount_child_at("/mnt", 0, &probe)) {
            kprintf("[VFS] /mnt exposes mounted volume '%s'\n", probe.name);
        }
    }
    if (g_primary_block_fat32_mount) {
        vfs_promote_home_to_block(g_primary_block_fat32_mount);
        vfs_promote_var_to_block(g_primary_block_fat32_mount);
        vfs_promote_usr_local_to_block(g_primary_block_fat32_mount);
    }
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
    } else if (strcmp(fs_type, "ramfs") == 0) {
        m->type = FS_RAMFS;
        m->priv = (void *)device;
        kprintf_color(0xFF00FF88, "[VFS] Mounted ramfs at '%s'\n", mount_point);
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

static void ramfs_init(ramfs_t *fs) {
    memset(fs, 0, sizeof(*fs));
    fs->nodes[0].used = true;
    fs->nodes[0].is_dir = true;
    strncpy(fs->nodes[0].name, "/", sizeof(fs->nodes[0].name) - 1);
}

static ramfs_node_t *ramfs_find(ramfs_t *fs, const char *path) {
    if (!fs || !path || !*path) return NULL;
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (fs->nodes[i].used && strcmp(fs->nodes[i].name, path) == 0) {
            return &fs->nodes[i];
        }
    }
    return NULL;
}

static ramfs_node_t *ramfs_create(ramfs_t *fs, const char *path, bool is_dir) {
    if (!fs || !path || !*path) return NULL;
    ramfs_node_t *existing = ramfs_find(fs, path);
    if (existing) return existing;
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!fs->nodes[i].used) {
            ramfs_node_t *n = &fs->nodes[i];
            memset(n, 0, sizeof(*n));
            n->used = true;
            n->is_dir = is_dir;
            strncpy(n->name, path, sizeof(n->name) - 1);
            return n;
        }
    }
    return NULL;
}

static void path_parent(const char *path, char *parent, usize parent_cap, char *base, usize base_cap) {
    if (parent_cap) parent[0] = 0;
    if (base_cap) base[0] = 0;
    if (!path || path[0] != '/') return;

    usize last = 0;
    for (usize i = 1; path[i]; i++) {
        if (path[i] == '/') last = i;
    }
    if (last == 0) {
        strncpy(parent, "/", parent_cap - 1);
        parent[parent_cap - 1] = 0;
        strncpy(base, path + 1, base_cap - 1);
        base[base_cap - 1] = 0;
        return;
    }
    usize n = last;
    if (n >= parent_cap) n = parent_cap - 1;
    memcpy(parent, path, n);
    parent[n] = 0;
    strncpy(base, path + last + 1, base_cap - 1);
    base[base_cap - 1] = 0;
}

static void ramfs_mkdir_p(ramfs_t *fs, const char *path) {
    if (!fs || !path || path[0] != '/') return;
    char cur[192];
    usize n = 0;
    cur[n++] = '/';
    cur[n] = 0;
    ramfs_create(fs, "/", true);

    const char *p = path + 1;
    while (*p && n + 1 < sizeof(cur)) {
        while (*p == '/') p++;
        while (*p && *p != '/' && n + 1 < sizeof(cur)) {
            cur[n++] = *p++;
            cur[n] = 0;
        }
        ramfs_create(fs, cur, true);
        if (*p == '/' && n + 1 < sizeof(cur)) {
            cur[n++] = '/';
            cur[n] = 0;
        }
    }
}

static isize ramfs_read(ramfs_t *fs, const char *path, void *buf, usize count, usize offset) {
    ramfs_node_t *n = ramfs_find(fs, path);
    if (!n || n->is_dir) return -1;
    if (offset >= n->size) return 0;
    usize avail = n->size - offset;
    if (avail > count) avail = count;
    memcpy(buf, n->data + offset, avail);
    return (isize)avail;
}

static isize ramfs_write(ramfs_t *fs, const char *path, const void *buf, usize count, usize offset) {
    ramfs_node_t *n = ramfs_create(fs, path, false);
    if (!n || n->is_dir) return -1;
    usize end = offset + count;
    if (end > n->cap) {
        usize newcap = n->cap ? n->cap : 256;
        while (newcap < end) newcap *= 2;
        if (fs->bytes_used - n->cap + newcap > RAMFS_MAX_DATA) return -1;
        u8 *newdata = (u8 *)kmalloc(newcap);
        if (!newdata) return -1;
        memset(newdata, 0, newcap);
        if (n->data && n->size) memcpy(newdata, n->data, n->size);
        if (n->data) kfree(n->data);
        fs->bytes_used = fs->bytes_used - n->cap + newcap;
        n->data = newdata;
        n->cap = newcap;
    }
    memcpy(n->data + offset, buf, count);
    if (end > n->size) n->size = end;
    return (isize)count;
}

static bool ramfs_truncate(ramfs_t *fs, const char *path) {
    ramfs_node_t *n = ramfs_create(fs, path, false);
    if (!n || n->is_dir) return false;
    n->size = 0;
    return true;
}

static bool ramfs_resize(ramfs_t *fs, const char *path, usize size) {
    ramfs_node_t *n = ramfs_create(fs, path, false);
    if (!n || n->is_dir) return false;
    if (size > n->cap) {
        usize newcap = n->cap ? n->cap : 256;
        while (newcap < size) newcap *= 2;
        if (fs->bytes_used - n->cap + newcap > RAMFS_MAX_DATA) return false;
        u8 *newdata = (u8 *)kmalloc(newcap);
        if (!newdata) return false;
        memset(newdata, 0, newcap);
        if (n->data && n->size) memcpy(newdata, n->data, n->size < size ? n->size : size);
        if (n->data) kfree(n->data);
        fs->bytes_used = fs->bytes_used - n->cap + newcap;
        n->data = newdata;
        n->cap = newcap;
    }
    if (n->data && size > n->size) memset(n->data + n->size, 0, size - n->size);
    n->size = size;
    return true;
}

static int ramfs_unlink(ramfs_t *fs, const char *path) {
    ramfs_node_t *n = ramfs_find(fs, path);
    if (!n || strcmp(path, "/") == 0) return -1;
    usize plen = strlen(path);
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!fs->nodes[i].used || &fs->nodes[i] == n) continue;
        if (strncmp(fs->nodes[i].name, path, plen) == 0 && fs->nodes[i].name[plen] == '/') {
            if (fs->nodes[i].data) kfree(fs->nodes[i].data);
            fs->bytes_used -= fs->nodes[i].cap;
            memset(&fs->nodes[i], 0, sizeof(fs->nodes[i]));
        }
    }
    if (n->data) kfree(n->data);
    fs->bytes_used -= n->cap;
    memset(n, 0, sizeof(*n));
    return 0;
}

static int ramfs_rename(ramfs_t *fs, const char *oldpath, const char *newpath) {
    ramfs_node_t *n = ramfs_find(fs, oldpath);
    if (!n || strcmp(oldpath, "/") == 0 || ramfs_find(fs, newpath)) return -1;
    usize old_len = strlen(oldpath);
    usize new_len = strlen(newpath);
    if (new_len >= sizeof(n->name)) return -1;

    strncpy(n->name, newpath, sizeof(n->name) - 1);
    n->name[sizeof(n->name) - 1] = 0;
    if (!n->is_dir) return 0;

    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        ramfs_node_t *child = &fs->nodes[i];
        if (!child->used || child == n) continue;
        if (strncmp(child->name, oldpath, old_len) != 0 || child->name[old_len] != '/') continue;
        char renamed[192];
        if (new_len + strlen(child->name + old_len) >= sizeof(renamed)) return -1;
        ksnprintf(renamed, sizeof(renamed), "%s%s", newpath, child->name + old_len);
        strncpy(child->name, renamed, sizeof(child->name) - 1);
        child->name[sizeof(child->name) - 1] = 0;
    }
    return 0;
}

static int ramfs_readdir(ramfs_t *fs, const char *path, u32 index, vfs_dirent_t *out) {
    if (!fs || !path || !out) return -1;
    char parent[192];
    char base[192];
    u32 seen = 0;

    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        ramfs_node_t *n = &fs->nodes[i];
        if (!n->used || strcmp(n->name, "/") == 0) continue;
        path_parent(n->name, parent, sizeof(parent), base, sizeof(base));
        if (strcmp(parent, path) != 0) continue;
        if (seen == index) {
            memset(out, 0, sizeof(*out));
            strncpy(out->name, base, sizeof(out->name) - 1);
            out->size = n->size;
            out->is_dir = n->is_dir;
            return 0;
        }
        seen++;
    }
    return -1;
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
        case FS_RAMFS:
            return ramfs_read((ramfs_t *)m->priv, rel_path, buf, count, offset);
        default:
            return -1;
    }
}

static isize vfs_do_write(int mount_idx, const char *rel_path, const void *buf, usize count) {
    vfs_mount_t *m = &g_mounts[mount_idx];
    isize n;
    switch (m->type) {
        case FS_FAT32: {
            fat32_vol_t *vol = (fat32_vol_t *)m->priv;
            if (!vol || !vol->mounted) return -1;
            n = fat32_write_file(vol, rel_path, buf, count);
            if (n >= 0 && m->block_backed && vfs_flush_block_mount(m) < 0) return -1;
            return n;
        }
        case FS_DEVFS:
            return devfs_write(rel_path, buf, count);
        case FS_RAMFS:
            return ramfs_write((ramfs_t *)m->priv, rel_path, buf, count, 0);
        case FS_INITRD:
            return -1; /* Read-only */
        default:
            return -1;
    }
}

static bool procfs_exists(const char *path) {
    if (!path) return false;
    if (path[0] == '/') path++;
    return strcmp(path, "cpuinfo") == 0 ||
           strcmp(path, "meminfo") == 0 ||
           strcmp(path, "version") == 0 ||
           strcmp(path, "uptime") == 0 ||
           strcmp(path, "loadavg") == 0;
}

static bool vfs_path_exists_for_open(int mount_idx, const char *rel_path) {
    if (mount_idx < 0 || mount_idx >= g_mount_count) return false;
    vfs_mount_t *m = &g_mounts[mount_idx];
    switch (m->type) {
        case FS_INITRD: {
            usize size = 0;
            return initrd_lookup(rel_path, &size) != NULL || initrd_is_dir(rel_path);
        }
        case FS_FAT32: {
            fat32_vol_t *vol = (fat32_vol_t *)m->priv;
            fat32_fileinfo_t info;
            if (!vol || !vol->mounted) return false;
            if (!rel_path || rel_path[0] == 0 || strcmp(rel_path, "/") == 0) return true;
            return fat32_lookup(vol, rel_path, &info);
        }
        case FS_DEVFS:
            return devfs_exists(rel_path);
        case FS_PROCFS:
            return procfs_exists(rel_path);
        case FS_RAMFS:
            return ramfs_find((ramfs_t *)m->priv, rel_path) != NULL;
        default:
            return false;
    }
}

static void vfs_fill_stat(yam_stat_t *out, u32 mode, i64 size, u64 ino) {
    memset(out, 0, sizeof(*out));
    out->dev = 1;
    out->ino = ino;
    out->mode = mode;
    out->nlink = ((mode & YAM_S_IFMT) == YAM_S_IFDIR) ? 2 : 1;
    out->uid = 0;
    out->gid = 0;
    out->size = size;
    out->blksize = 512;
    out->blocks = size > 0 ? (size + 511) / 512 : 0;
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
    char abs[256];
    vfs_resolve_path(pathname, abs, sizeof(abs));
    const char *rel;
    int midx = vfs_find_mount(abs, &rel);
    if (midx < 0) return -1;
    bool exists = vfs_path_exists_for_open(midx, rel);
    bool create = (flags & VFS_O_CREAT) != 0;
    bool excl = (flags & VFS_O_EXCL) != 0;
    bool trunc = (flags & VFS_O_TRUNC) != 0;

    if (!exists && !create) return -1;
    if (exists && create && excl) return -1;

    if (g_mounts[midx].type == FS_RAMFS && create) {
        if (!ramfs_create((ramfs_t *)g_mounts[midx].priv, rel, false)) return -1;
        exists = true;
    }
    if (g_mounts[midx].type == FS_RAMFS && trunc) {
        if (!ramfs_truncate((ramfs_t *)g_mounts[midx].priv, rel)) return -1;
    }
    if (g_mounts[midx].type == FS_FAT32 && (create || trunc)) {
        fat32_vol_t *vol = (fat32_vol_t *)g_mounts[midx].priv;
        char empty = 0;
        if (!vol || !vol->mounted) return -1;
        if (!exists || trunc) {
            if (fat32_write_file(vol, rel, &empty, 0) < 0) return -1;
            if (g_mounts[midx].block_backed && vfs_flush_block_mount(&g_mounts[midx]) < 0) return -1;
        }
    } else if (!exists && create) {
        return -1;
    }

    /* Allocate a file_t */
    file_t *f = (file_t *)kmalloc(sizeof(file_t));
    if (!f) return -1;
    memset(f, 0, sizeof(file_t));
    f->offset = 0;
    f->flags  = flags;
    f->ref_count = 1;
    strncpy(f->path, abs, 255);
    f->mount_idx = midx;

    int fd = fd_alloc(f);
    if (fd < 0) { kfree(f); return -1; }
    return fd;
}

int sys_unlink(const char *pathname) {
    if (!pathname) return -1;
    char abs[256];
    vfs_resolve_path(pathname, abs, sizeof(abs));
    const char *rel;
    int midx = vfs_find_mount(abs, &rel);
    if (midx < 0) return -1;
    if (g_mounts[midx].type == FS_RAMFS) {
        return ramfs_unlink((ramfs_t *)g_mounts[midx].priv, rel);
    }
    if (g_mounts[midx].type == FS_FAT32) {
        fat32_vol_t *vol = (fat32_vol_t *)g_mounts[midx].priv;
        if (!vol || !fat32_unlink(vol, rel)) return -1;
        if (g_mounts[midx].block_backed && vfs_flush_block_mount(&g_mounts[midx]) < 0) return -1;
        return 0;
    }
    return -1;
}

int sys_rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) return -1;
    char old_abs[256];
    char new_abs[256];
    vfs_resolve_path(oldpath, old_abs, sizeof(old_abs));
    vfs_resolve_path(newpath, new_abs, sizeof(new_abs));
    if (strcmp(old_abs, "/") == 0 || strcmp(new_abs, "/") == 0) return -1;

    const char *old_rel;
    const char *new_rel;
    int old_midx = vfs_find_mount(old_abs, &old_rel);
    int new_midx = vfs_find_mount(new_abs, &new_rel);
    if (old_midx < 0 || old_midx != new_midx) return -1;

    if (g_mounts[old_midx].type == FS_RAMFS) {
        return ramfs_rename((ramfs_t *)g_mounts[old_midx].priv, old_rel, new_rel);
    }
    if (g_mounts[old_midx].type == FS_FAT32) {
        fat32_vol_t *vol = (fat32_vol_t *)g_mounts[old_midx].priv;
        fat32_fileinfo_t info;
        if (!vol || !fat32_lookup(vol, old_rel, &info) || info.is_dir) return -1;
        if (fat32_lookup(vol, new_rel, &info)) return -1;
        usize cap = info.file_size ? info.file_size : 1;
        u8 *tmp = (u8 *)kmalloc(cap);
        if (!tmp) return -1;
        isize n = fat32_read_file(vol, old_rel, tmp, cap);
        if (n < 0) {
            kfree(tmp);
            return -1;
        }
        if (fat32_write_file(vol, new_rel, tmp, (usize)n) < 0) {
            kfree(tmp);
            return -1;
        }
        kfree(tmp);
        if (!fat32_unlink(vol, old_rel)) return -1;
        if (g_mounts[old_midx].block_backed && vfs_flush_block_mount(&g_mounts[old_midx]) < 0) return -1;
        return 0;
    }
    return -1;
}

int sys_readdir(const char *pathname, u32 index, vfs_dirent_t *out) {
    if (!pathname || !out) return -1;
    memset(out, 0, sizeof(*out));
    char abs[256];
    vfs_resolve_path(pathname, abs, sizeof(abs));

    if (strcmp(abs, "/") == 0) {
        static const char *roots[] = { "dev", "home", "mnt", "opt", "proc", "tmp", "usr", "var" };
        if (index >= sizeof(roots) / sizeof(roots[0])) return -1;
        strncpy(out->name, roots[index], sizeof(out->name) - 1);
        out->is_dir = true;
        return 0;
    }

    const char *rel;
    int midx = vfs_find_mount(abs, &rel);
    if (midx < 0) return -1;
    if (g_mounts[midx].type == FS_RAMFS) {
        int rc = ramfs_readdir((ramfs_t *)g_mounts[midx].priv, rel, index, out);
        if (rc == 0) return 0;
        u32 native_count = 0;
        vfs_dirent_t scratch;
        while (ramfs_readdir((ramfs_t *)g_mounts[midx].priv, rel, native_count, &scratch) == 0) {
            native_count++;
        }
        if (index >= native_count &&
            mount_child_at(abs, index - native_count, out)) {
            return 0;
        }
        return -1;
    }
    if (g_mounts[midx].type == FS_FAT32) {
        fat32_vol_t *vol = (fat32_vol_t *)g_mounts[midx].priv;
        fat32_fileinfo_t entries[16];
        int n = fat32_readdir(vol, rel, entries, 16);
        if (n < 0) return -1;
        if (index >= (u32)n) {
            if (mount_child_at(abs, index - (u32)n, out)) return 0;
            return -1;
        }
        strncpy(out->name, entries[index].name, sizeof(out->name) - 1);
        out->size = entries[index].file_size;
        out->is_dir = entries[index].is_dir;
        return 0;
    }
    return -1;
}

int sys_stat(const char *pathname, yam_stat_t *out) {
    if (!pathname || !out) return -1;
    char abs[256];
    vfs_resolve_path(pathname, abs, sizeof(abs));

    if (strcmp(abs, "/") == 0 || mount_has_child(abs)) {
        vfs_fill_stat(out, YAM_S_IFDIR | YAM_S_IRUSR | YAM_S_IWUSR | YAM_S_IXUSR, 0, 1);
        return 0;
    }
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active && strcmp(g_mounts[i].mount_point, abs) == 0) {
            vfs_fill_stat(out, YAM_S_IFDIR | YAM_S_IRUSR | YAM_S_IWUSR | YAM_S_IXUSR, 0, (u64)(i + 2));
            return 0;
        }
    }

    const char *rel;
    int midx = vfs_find_mount(abs, &rel);
    if (midx < 0) return -1;
    vfs_mount_t *m = &g_mounts[midx];
    if (m->type == FS_RAMFS) {
        ramfs_node_t *n = ramfs_find((ramfs_t *)m->priv, rel);
        if (!n) return -1;
        u32 kind = n->is_dir ? YAM_S_IFDIR | YAM_S_IXUSR : YAM_S_IFREG;
        vfs_fill_stat(out, kind | YAM_S_IRUSR | YAM_S_IWUSR, (i64)n->size, (u64)(n - ((ramfs_t *)m->priv)->nodes + 1000));
        return 0;
    }
    if (m->type == FS_FAT32) {
        fat32_vol_t *vol = (fat32_vol_t *)m->priv;
        fat32_fileinfo_t info;
        if (!vol || !fat32_lookup(vol, rel, &info)) return -1;
        u32 kind = info.is_dir ? YAM_S_IFDIR | YAM_S_IXUSR : YAM_S_IFREG;
        vfs_fill_stat(out, kind | YAM_S_IRUSR | YAM_S_IWUSR, (i64)info.file_size, info.first_cluster);
        return 0;
    }
    if (m->type == FS_INITRD) {
        usize size = 0;
        void *data = initrd_lookup(rel, &size);
        if (data) {
            vfs_fill_stat(out, YAM_S_IFREG | YAM_S_IRUSR, (i64)size, 2000);
            return 0;
        }
        if (initrd_is_dir(rel)) {
            vfs_fill_stat(out, YAM_S_IFDIR | YAM_S_IRUSR | YAM_S_IXUSR, 0, 2001);
            return 0;
        }
        return -1;
    }
    if (m->type == FS_DEVFS) {
        if (!devfs_exists(rel)) return -1;
        vfs_fill_stat(out, YAM_S_IFCHR | YAM_S_IRUSR | YAM_S_IWUSR, 0, 3000);
        return 0;
    }
    if (m->type == FS_PROCFS) {
        if (!procfs_exists(rel)) return -1;
        char tmp[768];
        isize n = procfs_read(rel, tmp, sizeof(tmp));
        vfs_fill_stat(out, YAM_S_IFREG | YAM_S_IRUSR, n > 0 ? n : 0, 4000);
        return 0;
    }
    return -1;
}

int sys_fstat(int fd, yam_stat_t *out) {
    if (!out || fd < 0) return -1;
    if (fd <= 2 && !fd_get(fd)) {
        vfs_fill_stat(out, YAM_S_IFCHR | YAM_S_IRUSR | YAM_S_IWUSR, 0, (u64)(3000 + fd));
        return 0;
    }
    file_t *f = fd_get(fd);
    if (!f) return -1;
    if (f->mount_idx < 0) {
        vfs_fill_stat(out, YAM_S_IFCHR | YAM_S_IRUSR | YAM_S_IWUSR, 0, (u64)(5000 + fd));
        return 0;
    }
    return sys_stat(f->path, out);
}

int sys_ftruncate(int fd, isize length) {
    if (fd < 0 || length < 0) return -1;
    file_t *f = fd_get(fd);
    if (!f || f->mount_idx < 0) return -1;
    const char *rel;
    int midx = vfs_find_mount(f->path, &rel);
    if (midx < 0) return -1;
    usize size = (usize)length;

    if (g_mounts[midx].type == FS_RAMFS) {
        if (!ramfs_resize((ramfs_t *)g_mounts[midx].priv, rel, size)) return -1;
        if (f->offset > size) f->offset = size;
        return 0;
    }
    if (g_mounts[midx].type == FS_FAT32) {
        u8 *tmp = (u8 *)kmalloc(size ? size : 1);
        if (!tmp) return -1;
        memset(tmp, 0, size ? size : 1);
        if (size > 0) {
            isize old = vfs_do_read(midx, rel, tmp, size, 0);
            if (old < 0) old = 0;
        }
        isize n = vfs_do_write(midx, rel, tmp, size);
        kfree(tmp);
        if (n < 0) return -1;
        if (f->offset > size) f->offset = size;
        return 0;
    }
    return -1;
}

int sys_mkdir(const char *pathname, u32 mode) {
    (void)mode;
    if (!pathname) return -1;
    char abs[256];
    vfs_resolve_path(pathname, abs, sizeof(abs));
    const char *rel;
    int midx = vfs_find_mount(abs, &rel);
    if (midx < 0) return -1;

    if (g_mounts[midx].type == FS_RAMFS) {
        ramfs_node_t *n = ramfs_create((ramfs_t *)g_mounts[midx].priv, rel, true);
        return n ? 0 : -1;
    }
    if (g_mounts[midx].type == FS_FAT32) {
        fat32_vol_t *vol = (fat32_vol_t *)g_mounts[midx].priv;
        if (!vol || !vol->mounted) return -1;
        if (!fat32_mkdir(vol, rel)) return -1;
        if (g_mounts[midx].block_backed && vfs_flush_block_mount(&g_mounts[midx]) < 0) return -1;
        return 0;
    }
    return -1;
}

static bool vfs_path_is_dir(const char *pathname) {
    if (!pathname) return false;
    char abs[256];
    vfs_resolve_path(pathname, abs, sizeof(abs));
    if (strcmp(abs, "/") == 0) return true;
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active && strcmp(g_mounts[i].mount_point, abs) == 0) return true;
    }
    const char *rel;
    int midx = vfs_find_mount(abs, &rel);
    if (midx < 0) return false;
    if (g_mounts[midx].type == FS_RAMFS) {
        ramfs_node_t *n = ramfs_find((ramfs_t *)g_mounts[midx].priv, rel);
        return n && n->is_dir;
    }
    if (g_mounts[midx].type == FS_DEVFS || g_mounts[midx].type == FS_PROCFS) {
        return strcmp(rel, "/") == 0;
    }
    if (g_mounts[midx].type == FS_FAT32) {
        fat32_fileinfo_t info;
        fat32_vol_t *vol = (fat32_vol_t *)g_mounts[midx].priv;
        return vol && fat32_lookup(vol, rel, &info) && info.is_dir;
    }
    return false;
}

int sys_chdir(const char *pathname) {
    if (!pathname) return -1;
    if (!vfs_path_is_dir(pathname)) return -1;
    task_t *t = sched_current();
    if (!t) return -1;
    vfs_resolve_path(pathname, t->cwd, sizeof(t->cwd));
    kprintf("[VFS] task '%s' cwd='%s'\n", t->name, t->cwd);
    return 0;
}

isize sys_getcwd(char *buf, usize size) {
    if (!buf || size == 0) return -1;
    task_t *t = sched_current();
    const char *cwd = (t && t->cwd[0]) ? t->cwd : "/";
    usize len = strlen(cwd);
    if (len + 1 > size) return -1;
    memcpy(buf, cwd, len + 1);
    return (isize)len;
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
    if (f->flags & VFS_O_APPEND) {
        yam_stat_t st;
        if (sys_stat(f->path, &st) == 0 && st.size >= 0) f->offset = (usize)st.size;
    }
    if (g_mounts[midx].type == FS_RAMFS) {
        isize n = ramfs_write((ramfs_t *)g_mounts[midx].priv, rel, buf, count, f->offset);
        if (n > 0) f->offset += (usize)n;
        return n;
    }
    if (g_mounts[midx].type == FS_FAT32) {
        usize total = f->offset + count;
        u8 *tmp = (u8 *)kmalloc(total ? total : 1);
        if (!tmp) return -1;
        memset(tmp, 0, total ? total : 1);
        if (f->offset > 0) {
            isize old = vfs_do_read(midx, rel, tmp, f->offset, 0);
            if (old < 0) old = 0;
        }
        memcpy(tmp + f->offset, buf, count);
        isize n = vfs_do_write(midx, rel, tmp, total);
        kfree(tmp);
        if (n < 0) return -1;
        f->offset += count;
        return (isize)count;
    }
    isize n = vfs_do_write(midx, rel, buf, count);
    if (n > 0) f->offset += (usize)n;
    return n;
}

isize sys_lseek(int fd, isize offset, int whence) {
    file_t *f = fd_get(fd);
    if (!f) return -1;
    isize base = 0;
    if (whence == 0) {
        base = 0;
    } else if (whence == 1) {
        base = (isize)f->offset;
    } else if (whence == 2) {
        yam_stat_t st;
        if (sys_fstat(fd, &st) < 0) return -1;
        base = (isize)st.size;
    } else {
        return -1;
    }
    isize next = base + offset;
    if (next < 0) return -1;
    f->offset = (usize)next;
    return next;
}

/* ---- VFS Init ---- */

void vfs_init(void) {
    kprintf_color(0xFF00DDFF, "[VFS] Initializing Virtual File System...\n");
    memset(g_mounts, 0, sizeof(g_mounts));
    g_mount_count = 0;

    /* Initialize and mount initrd as root */
    initrd_init();
    ramfs_init(&g_ramfs_tmp);
    ramfs_init(&g_ramfs_var);
    ramfs_init(&g_ramfs_usr_local);
    ramfs_init(&g_ramfs_opt);
    ramfs_init(&g_ramfs_home);
    ramfs_init(&g_ramfs_mnt);

    ramfs_mkdir_p(&g_ramfs_var, "/cache/yamos/downloads");
    ramfs_mkdir_p(&g_ramfs_var, "/lib/yamos/packages");
    ramfs_mkdir_p(&g_ramfs_var, "/log");
    ramfs_mkdir_p(&g_ramfs_usr_local, "/bin");
    ramfs_mkdir_p(&g_ramfs_usr_local, "/lib");
    ramfs_mkdir_p(&g_ramfs_usr_local, "/share");
    ramfs_mkdir_p(&g_ramfs_opt, "/yamos/packages");
    ramfs_mkdir_p(&g_ramfs_home, "/root");
    ramfs_mkdir_p(&g_ramfs_home, "/guest");

    vfs_mount("initrd", "/", "initrd");
    vfs_mount("devfs",  "/dev",  "devfs");
    vfs_mount("procfs", "/proc", "procfs");
    vfs_mount((const char *)&g_ramfs_tmp, "/tmp", "ramfs");
    vfs_mount((const char *)&g_ramfs_var, "/var", "ramfs");
    vfs_mount((const char *)&g_ramfs_usr_local, "/usr/local", "ramfs");
    vfs_mount((const char *)&g_ramfs_opt, "/opt", "ramfs");
    vfs_mount((const char *)&g_ramfs_home, "/home", "ramfs");
    vfs_mount((const char *)&g_ramfs_mnt, "/mnt", "ramfs");
    vfs_mount_block_devices();

    /* Register in YamGraph */
    yam_node_id_t vfs_node = yamgraph_node_create(YAM_NODE_NAMESPACE, "fs", NULL);
    yamgraph_edge_link(0, vfs_node, YAM_EDGE_OWNS, YAM_PERM_ALL);

    kprintf_color(0xFF00FF88, "[VFS] Ready: %d mount points active\n", g_mount_count);
}
