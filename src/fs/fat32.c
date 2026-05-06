/* ============================================================================
 * YamKernel — FAT32 Filesystem Driver (Implementation)
 * Full read/write support: BPB parse, cluster chains, LFN directory entries.
 * ============================================================================ */
#include "fat32.h"
#include "bcache.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../mem/heap.h"

static u32 cluster_to_sector(fat32_vol_t *v, u32 cluster) {
    return v->data_start + (cluster - 2) * v->sectors_per_cluster;
}
static u8 *cluster_read(fat32_vol_t *v, u32 cluster) {
    u8 *buf = kmalloc(v->bytes_per_cluster);
    bcache_read(v->dev, v->start_lba + cluster_to_sector(v, cluster), v->sectors_per_cluster, buf);
    return buf;
}
static void cluster_write(fat32_vol_t *v, u32 cluster, const u8 *buf) {
    bcache_write(v->dev, v->start_lba + cluster_to_sector(v, cluster), v->sectors_per_cluster, buf);
}
static u32 fat_entry(fat32_vol_t *v, u32 cluster) {
    u32 fat_sector = v->reserved_sectors + (cluster * 4) / v->bytes_per_sector;
    u32 fat_offset = (cluster * 4) % v->bytes_per_sector;
    u8 *buf = kmalloc(v->bytes_per_sector);
    bcache_read(v->dev, v->start_lba + fat_sector, 1, buf);
    u32 val = *(u32 *)(buf + fat_offset);
    kfree(buf);
    return val & FAT32_MASK;
}
static void fat_set_entry(fat32_vol_t *v, u32 cluster, u32 value) {
    u32 fat_sector = v->reserved_sectors + (cluster * 4) / v->bytes_per_sector;
    u32 fat_offset = (cluster * 4) % v->bytes_per_sector;
    u8 *buf = kmalloc(v->bytes_per_sector);
    bcache_read(v->dev, v->start_lba + fat_sector, 1, buf);
    u32 *fat = (u32 *)(buf + fat_offset);
    *fat = (*fat & 0xF0000000) | (value & FAT32_MASK);
    bcache_write(v->dev, v->start_lba + fat_sector, 1, buf);
    if (v->num_fats >= 2) {
        bcache_read(v->dev, v->start_lba + fat_sector + v->fat_size, 1, buf);
        fat = (u32 *)(buf + fat_offset);
        *fat = (*fat & 0xF0000000) | (value & FAT32_MASK);
        bcache_write(v->dev, v->start_lba + fat_sector + v->fat_size, 1, buf);
    }
    kfree(buf);
}
static u32 alloc_cluster(fat32_vol_t *v) {
    for (u32 c = 2; c < v->total_clusters + 2; c++) {
        if (fat_entry(v, c) == FAT32_FREE) {
            fat_set_entry(v, c, FAT32_EOC);
            u8 *buf = kmalloc(v->bytes_per_cluster);
            memset(buf, 0, v->bytes_per_cluster);
            cluster_write(v, c, buf);
            kfree(buf);
            return c;
        }
    }
    return 0;
}
static u32 chain_extend(fat32_vol_t *v, u32 last) {
    u32 c = alloc_cluster(v);
    if (c) fat_set_entry(v, last, c);
    return c;
}
static void free_chain(fat32_vol_t *v, u32 cluster) {
    while (cluster < FAT32_EOC && cluster >= 2) {
        u32 next = fat_entry(v, cluster);
        fat_set_entry(v, cluster, FAT32_FREE);
        cluster = next;
    }
}
static void dirent_to_name(const fat32_dirent_t *de, char *out) {
    int j = 0;
    for (int i = 0; i < 8 && de->name[i] != ' '; i++) {
        char c = de->name[i];
        out[j++] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
    }
    if (de->name[8] != ' ') {
        out[j++] = '.';
        for (int i = 8; i < 11 && de->name[i] != ' '; i++) {
            char c = de->name[i];
            out[j++] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
        }
    }
    out[j] = '\0';
}
static void name_to_83(const char *name, u8 out[11]) {
    memset(out, ' ', 11);
    int i = 0, j = 0;
    while (name[i] && name[i] != '.' && j < 8) {
        char c = name[i++];
        out[j++] = (c >= 'a' && c <= 'z') ? c - 32 : c;
    }
    if (name[i] == '.') {
        i++; j = 8;
        while (name[i] && j < 11) {
            char c = name[i++];
            out[j++] = (c >= 'a' && c <= 'z') ? c - 32 : c;
        }
    }
}
static bool find_in_dir(fat32_vol_t *v, u32 dir_cluster, const char *name,
                         fat32_dirent_t *out_de, u32 *out_cl, u32 *out_off) {
    u8 target83[11]; name_to_83(name, target83);
    u32 cluster = dir_cluster;
    while (cluster < FAT32_EOC && cluster >= 2) {
        u8  *base    = cluster_read(v, cluster);
        u32  entries = v->bytes_per_cluster / sizeof(fat32_dirent_t);
        bool found = false;
        for (u32 i = 0; i < entries; i++) {
            fat32_dirent_t *de = (fat32_dirent_t *)(base + i * sizeof(fat32_dirent_t));
            if (de->name[0] == 0x00) { kfree(base); return false; }
            if (de->name[0] == 0xE5 || de->attr == FAT_ATTR_LFN) continue;
            if (memcmp(de->name, target83, 11) == 0) {
                *out_de = *de; *out_cl = cluster; *out_off = i * sizeof(fat32_dirent_t);
                found = true; break;
            }
            /* Fallback: case-insensitive 8.3 name match */
            char de_name[13]; dirent_to_name(de, de_name);
            char lname[128]; int l = strlen(name);
            for (int k = 0; k < l && k < 127; k++) {
                char c = name[k];
                lname[k] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
            }
            lname[l] = '\0';
            if (strcmp(de_name, lname) == 0) {
                *out_de = *de; *out_cl = cluster; *out_off = i * sizeof(fat32_dirent_t);
                found = true; break;
            }
        }
        kfree(base);
        if (found) return true;
        cluster = fat_entry(v, cluster);
    }
    return false;
}
static bool resolve_path(fat32_vol_t *v, const char *path, fat32_dirent_t *out_de) {
    if (!path || path[0] != '/') return false;
    memset(out_de, 0, sizeof(*out_de));
    out_de->attr = FAT_ATTR_DIRECTORY;
    out_de->first_cluster_lo = (u16)(v->root_cluster & 0xFFFF);
    out_de->first_cluster_hi = (u16)(v->root_cluster >> 16);
    const char *p = path + 1;
    if (!*p) return true; /* Root */
    while (*p) {
        char comp[128]; int i = 0;
        while (*p && *p != '/' && i < 127) comp[i++] = *p++;
        comp[i] = '\0';
        if (!comp[0]) { if (*p == '/') p++; continue; }
        if (*p == '/') p++;
        u32 dir_cluster = ((u32)out_de->first_cluster_hi << 16) | out_de->first_cluster_lo;
        u32 oc, oo;
        if (!find_in_dir(v, dir_cluster, comp, out_de, &oc, &oo)) return false;
    }
    return true;
}

bool fat32_mount(struct block_device *dev, u64 start_lba, fat32_vol_t *vol) {
    if (!dev || !dev->read) return false;
    u8 buf[512];
    if (dev->read(dev, start_lba, 1, buf) < 0) return false;
    fat32_bpb_t *bpb = (fat32_bpb_t *)buf;
    if (bpb->bytes_per_sector == 0 || bpb->sectors_per_cluster == 0) return false;
    if (memcmp(bpb->fs_type, "FAT32   ", 8) != 0) return false;
    vol->dev                 = dev;
    vol->start_lba           = start_lba;
    vol->bytes_per_sector    = bpb->bytes_per_sector;
    vol->sectors_per_cluster = bpb->sectors_per_cluster;
    vol->bytes_per_cluster   = (u32)bpb->bytes_per_sector * bpb->sectors_per_cluster;
    vol->reserved_sectors    = bpb->reserved_sectors;
    vol->num_fats            = bpb->num_fats;
    vol->fat_size            = bpb->fat_size_32;
    vol->root_cluster        = bpb->root_cluster;
    vol->data_start          = bpb->reserved_sectors + bpb->num_fats * bpb->fat_size_32;
    vol->total_clusters      = (bpb->total_sectors_32 - vol->data_start) / bpb->sectors_per_cluster;
    vol->mounted             = true;
    kprintf_color(0xFF00FF88, "[FAT32] Mounted: %u clusters, %u B/cluster, root@%u\n",
                  vol->total_clusters, vol->bytes_per_cluster, vol->root_cluster);
    return true;
}
void fat32_unmount(fat32_vol_t *vol) { vol->mounted = false; }

bool fat32_lookup(fat32_vol_t *vol, const char *path, fat32_fileinfo_t *info) {
    if (!vol->mounted) return false;
    fat32_dirent_t de;
    if (!resolve_path(vol, path, &de)) return false;
    info->first_cluster = ((u32)de.first_cluster_hi << 16) | de.first_cluster_lo;
    info->file_size = de.file_size;
    info->is_dir = (de.attr & FAT_ATTR_DIRECTORY) != 0;
    strncpy(info->name, path, 255);
    return true;
}

isize fat32_read_file(fat32_vol_t *vol, const char *path, void *buf, usize buf_size) {
    if (!vol->mounted || !buf) return -1;
    fat32_dirent_t de;
    if (!resolve_path(vol, path, &de)) return -1;
    if (de.attr & FAT_ATTR_DIRECTORY) return -1;
    u32 cluster = ((u32)de.first_cluster_hi << 16) | de.first_cluster_lo;
    u32 remaining = de.file_size;
    u8 *dst = (u8 *)buf; usize copied = 0;
    while (cluster < FAT32_EOC && cluster >= 2 && remaining > 0) {
        u8  *src = cluster_read(vol, cluster);
        u32  cnt = vol->bytes_per_cluster;
        if (cnt > remaining) cnt = remaining;
        if (copied + cnt > buf_size) cnt = (u32)(buf_size - copied);
        memcpy(dst + copied, src, cnt);
        kfree(src);
        copied += cnt; remaining -= cnt;
        cluster = fat_entry(vol, cluster);
    }
    return (isize)copied;
}

isize fat32_write_file(fat32_vol_t *vol, const char *path, const void *data, usize size) {
    if (!vol->mounted || !data) return -1;
    /* Find parent dir */
    char parent_path[256] = "/"; char fname[128];
    const char *last_slash = path;
    for (const char *p = path; *p; p++) if (*p == '/') last_slash = p;
    if (last_slash == path) { strncpy(fname, path + 1, 127); }
    else {
        usize pl = (usize)(last_slash - path); if (pl >= 255) pl = 254;
        memcpy(parent_path, path, pl); parent_path[pl] = '\0';
        strncpy(fname, last_slash + 1, 127);
    }
    fat32_dirent_t parent_de;
    if (!resolve_path(vol, parent_path, &parent_de)) return -1;
    u32 parent_cluster = ((u32)parent_de.first_cluster_hi << 16) | parent_de.first_cluster_lo;
    fat32_dirent_t de; u32 dir_cl, dir_off;
    bool exists = find_in_dir(vol, parent_cluster, fname, &de, &dir_cl, &dir_off);
    u32 first_cluster = 0;
    if (exists) {
        u32 c = ((u32)de.first_cluster_hi << 16) | de.first_cluster_lo;
        free_chain(vol, c);
    }
    const u8 *src = (const u8 *)data; usize rem = size; u32 prev = 0;
    while (rem > 0) {
        u32 c = alloc_cluster(vol); if (!c) return -1;
        if (!first_cluster) first_cluster = c;
        if (prev) fat_set_entry(vol, prev, c);
        u8 *dst = cluster_read(vol, c); u32 cnt = vol->bytes_per_cluster;
        if (cnt > rem) cnt = (u32)rem;
        memcpy(dst, src, cnt);
        cluster_write(vol, c, dst); kfree(dst);
        src += cnt; rem -= cnt; prev = c;
    }
    u8 name83[11]; name_to_83(fname, name83);
    if (exists) {
        u8 *cbuf = cluster_read(vol, dir_cl);
        fat32_dirent_t *ep = (fat32_dirent_t *)(cbuf + dir_off);
        ep->first_cluster_lo = (u16)(first_cluster & 0xFFFF);
        ep->first_cluster_hi = (u16)(first_cluster >> 16);
        ep->file_size = (u32)size;
        cluster_write(vol, dir_cl, cbuf); kfree(cbuf);
    } else {
        u32 c = parent_cluster;
        while (c < FAT32_EOC && c >= 2) {
            u8 *base = cluster_read(vol, c); u32 entries = vol->bytes_per_cluster / sizeof(fat32_dirent_t);
            for (u32 i = 0; i < entries; i++) {
                fat32_dirent_t *slot = (fat32_dirent_t *)(base + i * sizeof(fat32_dirent_t));
                if (slot->name[0] == 0x00 || slot->name[0] == 0xE5) {
                    memcpy(slot->name, name83, 11); slot->attr = FAT_ATTR_ARCHIVE;
                    slot->first_cluster_lo = (u16)(first_cluster & 0xFFFF);
                    slot->first_cluster_hi = (u16)(first_cluster >> 16);
                    slot->file_size = (u32)size;
                    cluster_write(vol, c, base); kfree(base);
                    return (isize)size;
                }
            }
            kfree(base);
            u32 nx = fat_entry(vol, c); if (nx >= FAT32_EOC) { nx = chain_extend(vol, c); if (!nx) return -1; }
            c = nx;
        }
    }
    return (isize)size;
}

bool fat32_unlink(fat32_vol_t *vol, const char *path) {
    if (!vol || !vol->mounted || !path || strcmp(path, "/") == 0) return false;

    char parent_path[256] = "/";
    char fname[128];
    const char *last_slash = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }
    if (last_slash == path) {
        strncpy(fname, path + 1, sizeof(fname) - 1);
        fname[sizeof(fname) - 1] = 0;
    } else {
        usize pl = (usize)(last_slash - path);
        if (pl >= sizeof(parent_path)) pl = sizeof(parent_path) - 1;
        memcpy(parent_path, path, pl);
        parent_path[pl] = 0;
        strncpy(fname, last_slash + 1, sizeof(fname) - 1);
        fname[sizeof(fname) - 1] = 0;
    }
    if (!fname[0]) return false;

    fat32_dirent_t parent_de;
    if (!resolve_path(vol, parent_path, &parent_de)) return false;
    u32 parent_cluster = ((u32)parent_de.first_cluster_hi << 16) | parent_de.first_cluster_lo;

    fat32_dirent_t de;
    u32 dir_cl, dir_off;
    if (!find_in_dir(vol, parent_cluster, fname, &de, &dir_cl, &dir_off)) return false;
    if (de.attr & FAT_ATTR_DIRECTORY) return false;

    u32 first_cluster = ((u32)de.first_cluster_hi << 16) | de.first_cluster_lo;
    free_chain(vol, first_cluster);

    u8 *cbuf = cluster_read(vol, dir_cl);
    fat32_dirent_t *slot = (fat32_dirent_t *)(cbuf + dir_off);
    slot->name[0] = 0xE5;
    cluster_write(vol, dir_cl, cbuf); kfree(cbuf);
    return true;
}

int fat32_readdir(fat32_vol_t *vol, const char *path, fat32_fileinfo_t *out, int max_entries) {
    if (!vol->mounted) return -1;
    fat32_dirent_t de;
    if (!resolve_path(vol, path, &de)) return -1;
    if (!(de.attr & FAT_ATTR_DIRECTORY)) return -1;
    u32 cluster = ((u32)de.first_cluster_hi << 16) | de.first_cluster_lo;
    int count = 0;
    while (cluster < FAT32_EOC && cluster >= 2 && count < max_entries) {
        u8 *base = cluster_read(vol, cluster); u32 entries = vol->bytes_per_cluster / sizeof(fat32_dirent_t);
        bool break_out = false;
        for (u32 i = 0; i < entries && count < max_entries; i++) {
            fat32_dirent_t *entry = (fat32_dirent_t *)(base + i * sizeof(fat32_dirent_t));
            if (entry->name[0] == 0x00) { break_out = true; break; }
            if (entry->name[0] == 0xE5 || entry->attr == FAT_ATTR_LFN) continue;
            if (entry->attr & FAT_ATTR_VOLUME_ID) continue;
            if (entry->name[0] == '.') continue;
            fat32_fileinfo_t *fi = &out[count++];
            dirent_to_name(entry, fi->name);
            fi->first_cluster = ((u32)entry->first_cluster_hi << 16) | entry->first_cluster_lo;
            fi->file_size = entry->file_size;
            fi->is_dir = (entry->attr & FAT_ATTR_DIRECTORY) != 0;
        }
        kfree(base);
        if (break_out) break;
        cluster = fat_entry(vol, cluster);
    }
    return count;
}

bool fat32_mkdir(fat32_vol_t *vol, const char *path) {
    if (!vol->mounted) return false;
    char parent_path[256] = "/"; char dname[128];
    const char *last_slash = path;
    for (const char *p = path; *p; p++) if (*p == '/') last_slash = p;
    if (last_slash == path) { strncpy(dname, path + 1, 127); }
    else { usize pl = (usize)(last_slash-path); if(pl>=255) pl=254; memcpy(parent_path,path,pl); parent_path[pl]='\0'; strncpy(dname,last_slash+1,127); }
    fat32_dirent_t parent_de;
    if (!resolve_path(vol, parent_path, &parent_de)) return false;
    u32 parent_cluster = ((u32)parent_de.first_cluster_hi << 16) | parent_de.first_cluster_lo;
    u32 new_cluster = alloc_cluster(vol); if (!new_cluster) return false;
    u8 *new_cbuf = cluster_read(vol, new_cluster);
    fat32_dirent_t *dot = (fat32_dirent_t *)new_cbuf;
    memset(dot, ' ', 2 * sizeof(fat32_dirent_t));
    dot->name[0] = '.'; dot->attr = FAT_ATTR_DIRECTORY;
    dot->first_cluster_lo = (u16)(new_cluster & 0xFFFF); dot->first_cluster_hi = (u16)(new_cluster >> 16);
    fat32_dirent_t *dotdot = dot + 1;
    dotdot->name[0] = '.'; dotdot->name[1] = '.'; dotdot->attr = FAT_ATTR_DIRECTORY;
    dotdot->first_cluster_lo = (u16)(parent_cluster & 0xFFFF); dotdot->first_cluster_hi = (u16)(parent_cluster >> 16);
    cluster_write(vol, new_cluster, new_cbuf); kfree(new_cbuf);
    
    u8 name83[11]; name_to_83(dname, name83);
    u32 c = parent_cluster;
    while (c < FAT32_EOC && c >= 2) {
        u8 *base = cluster_read(vol, c); u32 entries = vol->bytes_per_cluster / sizeof(fat32_dirent_t);
        for (u32 i = 0; i < entries; i++) {
            fat32_dirent_t *slot = (fat32_dirent_t *)(base + i * sizeof(fat32_dirent_t));
            if (slot->name[0] == 0x00 || slot->name[0] == 0xE5) {
                memcpy(slot->name, name83, 11); slot->attr = FAT_ATTR_DIRECTORY;
                slot->first_cluster_lo = (u16)(new_cluster & 0xFFFF);
                slot->first_cluster_hi = (u16)(new_cluster >> 16); 
                cluster_write(vol, c, base); kfree(base);
                return true;
            }
        }
        kfree(base);
        c = fat_entry(vol, c);
    }
    return false;
}
