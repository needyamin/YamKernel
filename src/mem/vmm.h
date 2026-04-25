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
#define VMM_FLAG_NX         (1ULL << 63)

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

#endif /* _MEM_VMM_H */
