/* ============================================================================
 * YamKernel — Virtual Memory Manager
 * v0.3.0: CoW, huge pages, mprotect, brk, VMA merge/split
 * ============================================================================ */

#ifndef _MEM_VMM_H
#define _MEM_VMM_H

#include <nexus/types.h>

/* Page table flags */
#define VMM_FLAG_PRESENT    (1ULL << 0)
#define VMM_FLAG_WRITE      (1ULL << 1)
#define VMM_FLAG_USER       (1ULL << 2)
#define VMM_FLAG_PWT        (1ULL << 3)
#define VMM_FLAG_NOCACHE    (1ULL << 4)
#define VMM_FLAG_ACCESSED   (1ULL << 5)
#define VMM_FLAG_DIRTY      (1ULL << 6)
#define VMM_FLAG_HUGE       (1ULL << 7)   /* PS bit — 2MB page at PDE level */
#define VMM_FLAG_GLOBAL     (1ULL << 8)
#define VMM_FLAG_COW        (1ULL << 9)   /* Bit 9: OS-defined — Copy-on-Write */
#define VMM_FLAG_DONT_FREE  (1ULL << 10)  /* Bit 10: OS-specific, don't free phys */
#define VMM_FLAG_NX         (1ULL << 63)

/* Huge page size */
#define HUGE_PAGE_SIZE      (2 * 1024 * 1024)  /* 2 MB */
#define HUGE_PAGE_SHIFT     21

/* mmap flags */
#define PROT_NONE       0x0
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20
#define MAP_GROWSDOWN   0x100   /* Stack-like: grows downward */
#define MAP_FAILED      ((void *)-1)

/* VMA flags */
#define VMA_FLAG_READ       0x01
#define VMA_FLAG_WRITE      0x02
#define VMA_FLAG_EXEC       0x04
#define VMA_FLAG_SHARED     0x08
#define VMA_FLAG_GROWSDOWN  0x10
#define VMA_FLAG_COW        0x20   /* Created by fork — CoW semantics */
#define VMA_FLAG_HUGEPAGE   0x40   /* Use 2MB huge pages */

struct file;

typedef struct vma {
    u64         start;
    u64         end;
    u32         flags;
    u32         prot;
    struct file *file;      /* If file-backed */
    usize       pgoff;      /* Offset into file */
    struct vma  *next;
} vma_t;

/* Initialize VMM with the HHDM offset from Limine */
void vmm_init(u64 hhdm_offset);

/* Map a virtual page to physical page */
bool vmm_map_page(u64 *pml4, u64 virt, u64 phys, u64 flags);

/* Map a 2MB huge page */
bool vmm_map_huge_page(u64 *pml4, u64 virt, u64 phys, u64 flags);

/* Unmap a virtual page */
void vmm_unmap_page(u64 *pml4, u64 virt);

/* Get physical address for a virtual address */
u64  vmm_virt_to_phys(u64 *pml4, u64 virt);

/* Get PTE flags for a virtual address */
u64  vmm_get_pte_flags(u64 *pml4, u64 virt);

/* Update PTE flags without changing physical mapping */
bool vmm_update_flags(u64 *pml4, u64 virt, u64 new_flags);

/* Get the current PML4 */
u64 *vmm_get_kernel_pml4(void);

/* Convert physical address to virtual (using HHDM) */
void *vmm_phys_to_virt(u64 phys);

/* Convert virtual HHDM address back to physical */
u64   vmm_virt_hhdm_to_phys(void *virt);

/* mmap / munmap / mprotect / brk syscalls */
void *sys_mmap(void *addr, usize length, u32 prot, u32 flags, int fd, usize offset);
int   sys_munmap(void *addr, usize length);
int   sys_mprotect(void *addr, usize length, u32 prot);
u64   sys_brk(u64 new_brk);

/* Demand paging: called from #PF handler to lazily allocate pages */
bool vmm_handle_page_fault(u64 fault_addr, u64 error_code);

/* Copy-on-Write: handle write fault on CoW page */
bool vmm_handle_cow_fault(u64 *pml4, u64 fault_addr);

/* Clone address space for fork (marks writable pages as CoW) */
u64 *vmm_fork_address_space(u64 *src_pml4);

/* User address space setup */
u64 *vmm_create_user_pml4(void);
void vmm_destroy_user_pml4(u64 *pml4);

/* TLB management */
void vmm_flush_tlb_page(u64 virt);
void vmm_flush_tlb_all(void);
void vmm_shootdown_page(u64 virt);
void vmm_shootdown_all(void);

/* Kernel stacks with unmapped guard pages on both ends */
void *vmm_alloc_kernel_stack(usize usable_size);

#endif /* _MEM_VMM_H */
