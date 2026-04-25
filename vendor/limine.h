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

#endif /* _LIMINE_H */
