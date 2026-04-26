/* ============================================================================
 * YamKernel — Virtual Memory Manager Implementation
 * 4-level page tables (PML4 → PDPT → PD → PT) for x86_64
 * ============================================================================ */

#include "vmm.h"
#include "pmm.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

static u64 g_hhdm_offset = 0;

/* ---- Helpers ---- */

/* Convert physical to kernel-virtual using Higher Half Direct Map */
void *vmm_phys_to_virt(u64 phys) {
    return (void *)(phys + g_hhdm_offset);
}

u64 vmm_virt_hhdm_to_phys(void *virt) {
    return (u64)virt - g_hhdm_offset;
}

/* Extract page table indices from virtual address */
static inline u64 pml4_index(u64 virt) { return (virt >> 39) & 0x1FF; }
static inline u64 pdpt_index(u64 virt) { return (virt >> 30) & 0x1FF; }
static inline u64 pd_index(u64 virt)   { return (virt >> 21) & 0x1FF; }
static inline u64 pt_index(u64 virt)   { return (virt >> 12) & 0x1FF; }

/* Allocate a zeroed page for page table use */
static u64 *alloc_page_table(void) {
    u64 phys = pmm_alloc_page();
    if (!phys) return NULL;
    u64 *virt = (u64 *)vmm_phys_to_virt(phys);
    memset(virt, 0, PAGE_SIZE);
    return virt;
}

/* Get or create a page table entry, returning the next level table */
static u64 *get_or_create_table(u64 *table, u64 index, u64 flags) {
    if (table[index] & VMM_FLAG_PRESENT) {
        /* Huge page (PS bit) — already mapped by bootloader, don't descend */
        if (table[index] & (1ULL << 7)) return NULL;
        u64 phys = table[index] & 0x000FFFFFFFFFF000ULL;
        return (u64 *)vmm_phys_to_virt(phys);
    }

    u64 *new_table = alloc_page_table();
    if (!new_table) return NULL;

    u64 phys = vmm_virt_hhdm_to_phys(new_table);
    table[index] = phys | VMM_FLAG_PRESENT | VMM_FLAG_WRITE | flags;
    return new_table;
}

/* ---- Public API ---- */

void vmm_init(u64 hhdm_offset) {
    g_hhdm_offset = hhdm_offset;
    kprintf_color(0xFF00FF88, "[VMM] Initialized: HHDM offset = 0x%lx\n", hhdm_offset);
}

bool vmm_map_page(u64 *pml4, u64 virt, u64 phys, u64 flags) {
    u64 *pdpt = get_or_create_table(pml4, pml4_index(virt), flags & VMM_FLAG_USER);
    if (!pdpt) return false;

    u64 *pd = get_or_create_table(pdpt, pdpt_index(virt), flags & VMM_FLAG_USER);
    if (!pd) return false;

    u64 *pt = get_or_create_table(pd, pd_index(virt), flags & VMM_FLAG_USER);
    if (!pt) return false;

    pt[pt_index(virt)] = (phys & 0x000FFFFFFFFFF000ULL) | flags | VMM_FLAG_PRESENT;
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
    return true;
}

void vmm_unmap_page(u64 *pml4, u64 virt) {
    /* Walk the page table */
    if (!(pml4[pml4_index(virt)] & VMM_FLAG_PRESENT)) return;
    u64 *pdpt = (u64 *)vmm_phys_to_virt(pml4[pml4_index(virt)] & 0x000FFFFFFFFFF000ULL);

    if (!(pdpt[pdpt_index(virt)] & VMM_FLAG_PRESENT)) return;
    u64 *pd = (u64 *)vmm_phys_to_virt(pdpt[pdpt_index(virt)] & 0x000FFFFFFFFFF000ULL);

    if (!(pd[pd_index(virt)] & VMM_FLAG_PRESENT)) return;
    u64 *pt = (u64 *)vmm_phys_to_virt(pd[pd_index(virt)] & 0x000FFFFFFFFFF000ULL);

    pt[pt_index(virt)] = 0;

    /* Invalidate TLB for this page */
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

u64 vmm_virt_to_phys(u64 *pml4, u64 virt) {
    if (!(pml4[pml4_index(virt)] & VMM_FLAG_PRESENT)) return 0;
    u64 *pdpt = (u64 *)vmm_phys_to_virt(pml4[pml4_index(virt)] & 0x000FFFFFFFFFF000ULL);

    if (!(pdpt[pdpt_index(virt)] & VMM_FLAG_PRESENT)) return 0;
    u64 *pd = (u64 *)vmm_phys_to_virt(pdpt[pdpt_index(virt)] & 0x000FFFFFFFFFF000ULL);

    if (!(pd[pd_index(virt)] & VMM_FLAG_PRESENT)) return 0;
    u64 *pt = (u64 *)vmm_phys_to_virt(pd[pd_index(virt)] & 0x000FFFFFFFFFF000ULL);

    if (!(pt[pt_index(virt)] & VMM_FLAG_PRESENT)) return 0;
    return (pt[pt_index(virt)] & 0x000FFFFFFFFFF000ULL) | (virt & 0xFFF);
}

u64 *vmm_get_kernel_pml4(void) {
    u64 pml4_phys;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(pml4_phys));
    return (u64 *)vmm_phys_to_virt(pml4_phys & 0x000FFFFFFFFFF000ULL);
}
