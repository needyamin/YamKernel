/* ============================================================================
 * YamKernel — FAT32 Filesystem Driver
 * Supports read/write of FAT32 volumes (BPB, cluster chains, directories).
 * ============================================================================ */
#pragma once
#include <nexus/types.h>

/* ---- BPB (BIOS Parameter Block) — FAT32 extended ---- */
typedef struct __attribute__((packed)) {
    u8  jmp_boot[3];
    u8  oem_name[8];
    u16 bytes_per_sector;
    u8  sectors_per_cluster;
    u16 reserved_sectors;
    u8  num_fats;
    u16 root_entry_count;   /* 0 for FAT32 */
    u16 total_sectors_16;   /* 0 for FAT32 */
    u8  media_type;
    u16 fat_size_16;        /* 0 for FAT32 */
    u16 sectors_per_track;
    u16 num_heads;
    u32 hidden_sectors;
    u32 total_sectors_32;
    /* FAT32 extended */
    u32 fat_size_32;
    u16 ext_flags;
    u16 fs_version;
    u32 root_cluster;       /* Usually 2 */
    u16 fs_info_sector;
    u16 backup_boot_sector;
    u8  reserved[12];
    u8  drive_number;
    u8  reserved1;
    u8  boot_signature;     /* 0x29 */
    u32 volume_id;
    u8  volume_label[11];
    u8  fs_type[8];         /* "FAT32   " */
} fat32_bpb_t;

/* ---- Directory Entry ---- */
typedef struct __attribute__((packed)) {
    u8  name[11];           /* 8.3 format, space-padded */
    u8  attr;
    u8  nt_res;
    u8  create_time_tenth;
    u16 create_time;
    u16 create_date;
    u16 access_date;
    u16 first_cluster_hi;
    u16 write_time;
    u16 write_date;
    u16 first_cluster_lo;
    u32 file_size;
} fat32_dirent_t;

/* ---- LFN (Long File Name) Entry ---- */
typedef struct __attribute__((packed)) {
    u8  order;
    u16 name1[5];
    u8  attr;               /* Always 0x0F */
    u8  type;
    u8  checksum;
    u16 name2[6];
    u16 first_cluster;      /* Always 0 */
    u16 name3[2];
} fat32_lfn_t;

/* ---- Directory Entry Attributes ---- */
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F

/* ---- Special Cluster Values ---- */
#define FAT32_EOC           0x0FFFFFF8  /* End of Chain */
#define FAT32_BAD           0x0FFFFFF7
#define FAT32_FREE          0x00000000
#define FAT32_MASK          0x0FFFFFFF

/* ---- Mounted FAT32 Volume ---- */
typedef struct {
    struct block_device *dev;
    u64     start_lba;
    u32     bytes_per_sector;
    u32     sectors_per_cluster;
    u32     bytes_per_cluster;
    u32     reserved_sectors;
    u32     num_fats;
    u32     fat_size;       /* In sectors */
    u32     root_cluster;
    u32     data_start;     /* Sector of first data cluster */
    u32     total_clusters;
    bool    mounted;
} fat32_vol_t;

/* ---- File Info (returned by lookup) ---- */
typedef struct {
    u32  first_cluster;
    u32  file_size;
    bool is_dir;
    char name[256];
} fat32_fileinfo_t;

/* ---- Public API ---- */
bool fat32_mount(struct block_device *dev, u64 start_lba, fat32_vol_t *vol);
void fat32_unmount(fat32_vol_t *vol);

/* Returns bytes read, or -1 on error */
isize fat32_read_file(fat32_vol_t *vol, const char *path, void *buf, usize buf_size);
/* Returns bytes written, or -1 on error */
isize fat32_write_file(fat32_vol_t *vol, const char *path, const void *data, usize size);

bool fat32_lookup(fat32_vol_t *vol, const char *path, fat32_fileinfo_t *info);
bool fat32_mkdir(fat32_vol_t *vol, const char *path);
bool fat32_unlink(fat32_vol_t *vol, const char *path);

/* Read at most max_entries directory entries from path into out[] */
int  fat32_readdir(fat32_vol_t *vol, const char *path, fat32_fileinfo_t *out, int max_entries);
