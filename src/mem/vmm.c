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

u64 *vmm_create_user_pml4(void) {
    u64 *new_pml4 = alloc_page_table();
    if (!new_pml4) return NULL;
    
    u64 *kernel_pml4 = vmm_get_kernel_pml4();
    /* Copy the top half (entries 256-511) to map kernel and HHDM into user space */
    for (int i = 256; i < 512; i++) {
        new_pml4[i] = kernel_pml4[i];
    }
    return new_pml4;
}

static void free_pd(u64 *pd) {
    for (int i = 0; i < 512; i++) {
        if (pd[i] & VMM_FLAG_PRESENT) {
            if (!(pd[i] & (1ULL << 7))) { /* Not a huge page */
                if (!(pd[i] & VMM_FLAG_DONT_FREE)) {
                    u64 phys = pd[i] & 0x000FFFFFFFFFF000ULL;
                    pmm_free_page(phys);
                }
            }
        }
    }
}

static void free_pdpt(u64 *pdpt) {
    for (int i = 0; i < 512; i++) {
        if (pdpt[i] & VMM_FLAG_PRESENT) {
            u64 phys = pdpt[i] & 0x000FFFFFFFFFF000ULL;
            u64 *pd = (u64 *)vmm_phys_to_virt(phys);
            free_pd(pd);
            pmm_free_page(phys);
        }
    }
}

void vmm_destroy_user_pml4(u64 *pml4) {
    if (!pml4) return;
    /* Free user half (0-255) */
    for (int i = 0; i < 256; i++) {
        if (pml4[i] & VMM_FLAG_PRESENT) {
            u64 phys = pml4[i] & 0x000FFFFFFFFFF000ULL;
            u64 *pdpt = (u64 *)vmm_phys_to_virt(phys);
            free_pdpt(pdpt);
            pmm_free_page(phys);
        }
    }
    pmm_free_page(vmm_virt_hhdm_to_phys(pml4));
}

/* ---- mmap subsystem ---- */
#include "../sched/sched.h"
#include "../mem/heap.h"

/* User virtual address space for mmap starts at 0x7F0000000000 */
#define MMAP_BASE   0x7F0000000000ULL

static u64 next_mmap_addr = MMAP_BASE;

void *sys_mmap(void *addr, usize length, u32 prot, u32 flags, int fd, usize offset) {
    (void)addr; (void)fd; (void)offset;

    if (length == 0) return MAP_FAILED;

    /* Round up to page boundary */
    usize pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
    usize size  = pages * PAGE_SIZE;

    /* Allocate a virtual address range */
    u64 vaddr = next_mmap_addr;
    next_mmap_addr += size;

    /* Create a VMA to track this region */
    vma_t *vma = (vma_t *)kmalloc(sizeof(vma_t));
    if (!vma) return MAP_FAILED;
    memset(vma, 0, sizeof(*vma));
    vma->start = vaddr;
    vma->end   = vaddr + size;
    vma->flags = flags;
    vma->prot  = prot;
    vma->file  = NULL;

    /* Link into current task's VMA list */
    task_t *t = sched_current();
    if (t) {
        vma->next = (vma_t *)t->vma_head;
        t->vma_head = (struct vma *)vma;
    }

    /* If MAP_ANONYMOUS: pages are NOT mapped yet.
     * They will be mapped lazily by vmm_handle_page_fault on first access. */
    if (!(flags & MAP_ANONYMOUS)) {
        /* File-backed mmap: eagerly map for now (to be refined later) */
        for (usize i = 0; i < pages; i++) {
            u64 phys = pmm_alloc_page();
            if (!phys) return MAP_FAILED;
            u64 pflags = VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER;
            if (!(prot & PROT_EXEC)) pflags |= VMM_FLAG_NX;
            vmm_map_page(vmm_get_kernel_pml4(), vaddr + i * PAGE_SIZE, phys, pflags);
            void *mapped = vmm_phys_to_virt(phys);
            memset(mapped, 0, PAGE_SIZE);
        }
    }

    return (void *)vaddr;
}

int sys_munmap(void *addr, usize length) {
    u64 vaddr = (u64)addr;
    usize pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;

    /* Unmap pages and free physical memory */
    u64 *pml4 = vmm_get_kernel_pml4();
    for (usize i = 0; i < pages; i++) {
        u64 va = vaddr + i * PAGE_SIZE;
        u64 phys = vmm_virt_to_phys(pml4, va);
        if (phys) {
            vmm_unmap_page(pml4, va);
            pmm_free_page(phys & ~0xFFFULL);
        }
    }

    /* Remove VMA from task list */
    task_t *t = sched_current();
    if (t) {
        vma_t **prev = (vma_t **)&t->vma_head;
        for (vma_t *v = (vma_t *)t->vma_head; v; v = v->next) {
            if (v->start == vaddr) {
                *prev = v->next;
                kfree(v);
                break;
            }
            prev = &v->next;
        }
    }

    return 0;
}

/* ---- Demand Paging: Page Fault Handler ---- */
bool vmm_handle_page_fault(u64 fault_addr, u64 error_code) {
    (void)error_code;

    task_t *t = sched_current();
    if (!t) return false;

    /* Search VMA list for the faulting address */
    for (vma_t *v = (vma_t *)t->vma_head; v; v = v->next) {
        if (fault_addr >= v->start && fault_addr < v->end) {
            /* Valid region — lazily allocate a physical page */
            u64 page_addr = fault_addr & ~0xFFFULL;
            u64 phys = pmm_alloc_page();
            if (!phys) return false;

            u64 pflags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
            if (v->prot & PROT_WRITE) pflags |= VMM_FLAG_WRITE;
            if (!(v->prot & PROT_EXEC)) pflags |= VMM_FLAG_NX;

            vmm_map_page(vmm_get_kernel_pml4(), page_addr, phys, pflags);

            /* Zero the page */
            void *mapped = vmm_phys_to_virt(phys);
            memset(mapped, 0, PAGE_SIZE);

            return true;  /* Fault handled */
        }
    }

    return false;  /* Not our VMA — let the default handler panic */
}
