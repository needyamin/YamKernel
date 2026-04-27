/* ============================================================================
 * YamKernel — Virtual Memory Manager
 * ============================================================================ */

#ifndef _MEM_VMM_H
#define _MEM_VMM_H

#include <nexus/types.h>

/* Page table flags */
#define VMM_FLAG_PRESENT    (1ULL << 0)
#define VMM_FLAG_WRITE      (1ULL << 1)
#define VMM_FLAG_USER       (1ULL << 2)
#define VMM_FLAG_NOCACHE    (1ULL << 4)
#define VMM_FLAG_DONT_FREE  (1ULL << 10)  /* Bit 10: OS-specific, don't free physical page on PML4 destroy */
#define VMM_FLAG_NX         (1ULL << 63)

/* mmap flags */
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_ANONYMOUS   0x20
#define MAP_FAILED      ((void *)-1)

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

/* Unmap a virtual page */
void vmm_unmap_page(u64 *pml4, u64 virt);

/* Get physical address for a virtual address */
u64  vmm_virt_to_phys(u64 *pml4, u64 virt);

/* Get the current PML4 */
u64 *vmm_get_kernel_pml4(void);

/* Convert physical address to virtual (using HHDM) */
void *vmm_phys_to_virt(u64 phys);

/* Convert virtual HHDM address back to physical */
u64   vmm_virt_hhdm_to_phys(void *virt);

/* mmap / munmap syscalls */
void *sys_mmap(void *addr, usize length, u32 prot, u32 flags, int fd, usize offset);
int   sys_munmap(void *addr, usize length);

/* Demand paging: called from #PF handler to lazily allocate pages */
bool vmm_handle_page_fault(u64 fault_addr, u64 error_code);
/* User address space setup */
u64 *vmm_create_user_pml4(void);
void vmm_destroy_user_pml4(u64 *pml4);

#endif /* _MEM_VMM_H */
