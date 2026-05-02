/* ============================================================================
 * Limine Boot Protocol Header (v7 compatible)
 * Vendored from: https://github.com/limine-bootloader/limine
 * License: BSD-2-Clause
 *
 * This is a minimal version with only the requests YamKernel needs.
 * ============================================================================ */

#ifndef _LIMINE_H
#define _LIMINE_H

#include <nexus/types.h>

/* ---- Magic Numbers ---- */

#define LIMINE_COMMON_MAGIC_0 0xc7b1dd30df4c8b88
#define LIMINE_COMMON_MAGIC_1 0x0a82e883a194f07b

/* ---- Base Revision ---- */

#define LIMINE_BASE_REVISION(N) \
    UNUSED static volatile u64 limine_base_revision[3] = \
        { 0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, (N) };

/* ---- Request Macros ---- */

#define LIMINE_REQUESTS_START \
    __attribute__((used, section(".limine_requests_start"))) \
    static volatile LIMINE_REQUESTS_START_MARKER[2] = \
        { 0xf9562b2d5c95a6c8, 0x6a7b384944536bdc };

#define LIMINE_REQUESTS_END \
    __attribute__((used, section(".limine_requests_end"))) \
    static volatile LIMINE_REQUESTS_END_MARKER[2] = \
        { 0xadc0e0531bb10d03, 0x9572709f31764c62 };

/* ---- Framebuffer ---- */

struct limine_framebuffer {
    void     *address;
    u64       width;
    u64       height;
    u64       pitch;
    u16       bpp;
    u8        memory_model;
    u8        red_mask_size;
    u8        red_mask_shift;
    u8        green_mask_size;
    u8        green_mask_shift;
    u8        blue_mask_size;
    u8        blue_mask_shift;
    u8        unused[7];
    u64       edid_size;
    void     *edid;
    u64       mode_count;
    void    **modes;
};

struct limine_framebuffer_response {
    u64       revision;
    u64       framebuffer_count;
    struct limine_framebuffer **framebuffers;
};

struct limine_framebuffer_request {
    u64       id[4];
    u64       revision;
    struct limine_framebuffer_response *response;
};

#define LIMINE_FRAMEBUFFER_REQUEST \
    { LIMINE_COMMON_MAGIC_0, LIMINE_COMMON_MAGIC_1, \
      0x9d5827dcd881dd75, 0xa3148604f6fab11b }

/* ---- Memory Map ---- */

#define LIMINE_MEMMAP_USABLE                 0
#define LIMINE_MEMMAP_RESERVED               1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE       2
#define LIMINE_MEMMAP_ACPI_NVS               3
#define LIMINE_MEMMAP_BAD_MEMORY             4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_KERNEL_AND_MODULES     6
#define LIMINE_MEMMAP_FRAMEBUFFER            7

struct limine_memmap_entry {
    u64 base;
    u64 length;
    u64 type;
};

struct limine_memmap_response {
    u64       revision;
    u64       entry_count;
    struct limine_memmap_entry **entries;
};

struct limine_memmap_request {
    u64       id[4];
    u64       revision;
    struct limine_memmap_response *response;
};

#define LIMINE_MEMMAP_REQUEST \
    { LIMINE_COMMON_MAGIC_0, LIMINE_COMMON_MAGIC_1, \
      0x67cf3d9d378a806f, 0xe304acdfc50c3c62 }

/* ---- HHDM (Higher Half Direct Map) ---- */

struct limine_hhdm_response {
    u64 revision;
    u64 offset;
};

struct limine_hhdm_request {
    u64       id[4];
    u64       revision;
    struct limine_hhdm_response *response;
};

#define LIMINE_HHDM_REQUEST \
    { LIMINE_COMMON_MAGIC_0, LIMINE_COMMON_MAGIC_1, \
      0x48dcf1cb8ad2b852, 0x63984e959a98244b }

/* ---- Kernel Address ---- */

struct limine_kernel_address_response {
    u64 revision;
    u64 physical_base;
    u64 virtual_base;
};

struct limine_kernel_address_request {
    u64       id[4];
    u64       revision;
    struct limine_kernel_address_response *response;
};

#define LIMINE_KERNEL_ADDRESS_REQUEST \
    { LIMINE_COMMON_MAGIC_0, LIMINE_COMMON_MAGIC_1, \
      0x71ba76863cc55f63, 0xb2644a48c516a487 }

/* ---- Modules ---- */

struct limine_file {
    u64   revision;
    void *address;
    u64   size;
    char *path;
    char *cmdline;
    u32   media_type;
    u32   unused;
    u32   tftp_ip;
    u32   tftp_port;
    u32   partition_index;
    u32   mbr_disk_id;
    u8    gpt_disk_uuid[16];
    u8    gpt_part_uuid[16];
    u8    part_uuid[16];
};

struct limine_module_response {
    u64   revision;
    u64   module_count;
    struct limine_file **modules;
};

struct limine_module_request {
    u64       id[4];
    u64       revision;
    struct limine_module_response *response;
};

#define LIMINE_MODULE_REQUEST \
    { LIMINE_COMMON_MAGIC_0, LIMINE_COMMON_MAGIC_1, \
      0x3e7e279702be32af, 0xca1c4f3bd1280cee }

/* ---- RSDP (ACPI) ---- */

struct limine_rsdp_response {
    u64   revision;
    void *address;
};

struct limine_rsdp_request {
    u64       id[4];
    u64       revision;
    struct limine_rsdp_response *response;
};

#define LIMINE_RSDP_REQUEST \
    { LIMINE_COMMON_MAGIC_0, LIMINE_COMMON_MAGIC_1, \
      0xc5e77b6b397e7b43, 0x27637845accdcf3c }

/* ---- SMP ---- */

struct limine_smp_info {
    u32 processor_id;
    u32 lapic_id;
    u64 reserved;
    void (*goto_address)(struct limine_smp_info *);
    u64 extra_argument;
};

struct limine_smp_response {
    u64 revision;
    u32 flags;
    u32 bsp_lapic_id;
    u64 cpu_count;
    struct limine_smp_info **cpus;
};

struct limine_smp_request {
    u64 id[4];
    u64 revision;
    struct limine_smp_response *response;
    u64 flags;
};

#define LIMINE_SMP_REQUEST \
    { LIMINE_COMMON_MAGIC_0, LIMINE_COMMON_MAGIC_1, \
      0x95a67b819a1b857e, 0xa0b61b723b6a73e0 }

#endif /* _LIMINE_H */
