/* YamKernel — ACPI: parse RSDP/XSDT/RSDT and MADT */
#include "acpi.h"
#include "../mem/vmm.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

typedef struct PACKED {
    char sig[8]; u8 csum; char oem[6]; u8 rev;
    u32 rsdt_addr;
    u32 length; u64 xsdt_addr; u8 ext_csum; u8 reserved[3];
} rsdp_t;

typedef struct PACKED {
    char sig[4]; u32 length; u8 rev; u8 csum;
    char oem[6]; char oem_table[8]; u32 oem_rev;
    u32 creator_id; u32 creator_rev;
} sdt_hdr_t;

typedef struct PACKED {
    sdt_hdr_t hdr;
    u32 lapic_addr;
    u32 flags;
    u8  entries[];
} madt_t;

static acpi_info_t g_info;
static u64 g_hhdm = 0;

/* Limine's HHDM may not cover low/reserved phys regions where ACPI tables
 * live (e.g. 0xf0000-0x100000). Force-map a small range on demand. */
static void map_range(u64 phys, u64 bytes) {
    u64 start = phys & ~0xFFFULL;
    u64 end   = (phys + bytes + 0xFFF) & ~0xFFFULL;
    for (u64 p = start; p < end; p += 0x1000)
        vmm_map_page(vmm_get_kernel_pml4(), p + g_hhdm, p,
                     VMM_FLAG_PRESENT | VMM_FLAG_WRITE);
}
static void *p2v(u64 phys) { map_range(phys, 0x1000); return (void *)(phys + g_hhdm); }

static void parse_madt(madt_t *m) {
    g_info.lapic_addr = m->lapic_addr;
    u8 *p = m->entries;
    u8 *end = (u8 *)m + m->hdr.length;
    while (p < end) {
        u8 type = p[0], len = p[1];
        if (len == 0) break;
        switch (type) {
        case 0: {  /* Processor Local APIC */
            if (g_info.cpu_count < ACPI_MAX_CPUS) {
                acpi_cpu_t *c = &g_info.cpus[g_info.cpu_count++];
                c->acpi_id = p[2]; c->apic_id = p[3];
                c->flags = *(u32 *)&p[4];
            }
            break;
        }
        case 1: {  /* IO APIC */
            if (g_info.ioapic_count < ACPI_MAX_IOAPICS) {
                acpi_ioapic_t *io = &g_info.ioapics[g_info.ioapic_count++];
                io->id = p[2]; io->mmio_addr = *(u32 *)&p[4];
                io->gsi_base = *(u32 *)&p[8];
            }
            break;
        }
        case 5:    /* Local APIC Address Override */
            g_info.lapic_addr = *(u64 *)&p[4];
            break;
        }
        p += len;
    }
}

void acpi_init(void *rsdp_addr, u64 hhdm_offset) {
    g_hhdm = hhdm_offset;
    if (!rsdp_addr) { kprintf_color(0xFFFF3333, "[ACPI] No RSDP\n"); return; }

    /* Limine base revision 3 returns a physical address; if it looks like a
     * low phys addr (top bits clear), HHDM-translate (and map) it. */
    u64 raw = (u64)rsdp_addr;
    if (raw < g_hhdm) raw = (u64)p2v(raw);
    rsdp_t *r = (rsdp_t *)raw;
    sdt_hdr_t *root;
    bool xsdt = (r->rev >= 2 && r->xsdt_addr);
    root = (sdt_hdr_t *)p2v(xsdt ? r->xsdt_addr : (u64)r->rsdt_addr);

    /* Ensure the whole root table is mapped (it may span pages). */
    map_range((u64)root - g_hhdm, root->length);
    u32 entries = (root->length - sizeof(sdt_hdr_t)) / (xsdt ? 8 : 4);
    u8 *base = (u8 *)root + sizeof(sdt_hdr_t);

    for (u32 i = 0; i < entries; i++) {
        u64 phys = xsdt ? ((u64 *)base)[i] : (u64)((u32 *)base)[i];
        sdt_hdr_t *t = (sdt_hdr_t *)p2v(phys);
        map_range(phys, t->length);
        if (memcmp(t->sig, "APIC", 4) == 0) { parse_madt((madt_t *)t); break; }
    }

    kprintf_color(0xFF00FF88,
        "[ACPI] %s rev=%u  CPUs=%u  IOAPICs=%u  LAPIC=0x%lx\n",
        xsdt ? "XSDT" : "RSDT", r->rev,
        g_info.cpu_count, g_info.ioapic_count, g_info.lapic_addr);
}

const acpi_info_t *acpi_get(void) { return &g_info; }
