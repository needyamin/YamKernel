/* ============================================================================
 * YamKernel — ELF64 Binary Loader
 * Parses ELF headers, creates an isolated user-space address space,
 * maps PT_LOAD segments, allocates a user stack, and spawns a Ring 3 task.
 * ============================================================================ */
#include "elf.h"
#include "../mem/vmm.h"
#include "../mem/pmm.h"
#include "../mem/heap.h"
#include "../sched/sched.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

extern void enter_user_mode(u64 rip, u64 rsp) NORETURN;

/* User stack lives at the top of the lower-half canonical address space */
#define USER_STACK_GUARD_LOW  0x00007FFEFFFFF000ULL
#define USER_STACK_BASE       0x00007FFF00000000ULL
#define USER_STACK_PAGES 256  /* 1 MB user stack for larger real-world ELF apps. */
#define USER_STACK_SIZE  (USER_STACK_PAGES * PAGE_SIZE)
#define USER_STACK_TOP   (USER_STACK_BASE + USER_STACK_SIZE)
#define USER_STACK_GUARD_HIGH USER_STACK_TOP

/* Per-process bootstrap: switch to its page table and jump to user mode */
typedef struct {
    u64 entry;
    u64 *pml4;
} elf_bootstrap_arg_t;

static void elf_user_entry(void *arg) {
    elf_bootstrap_arg_t *ba = (elf_bootstrap_arg_t *)arg;
    u64 entry = ba->entry;
    u64 *pml4 = ba->pml4;

    kprintf("[ELF] Transitioning to user mode: entry=0x%lx, pml4=0x%lx\n", entry, (u64)pml4);

    /* Assign the user pml4 to this task so the scheduler will reload CR3 */
    task_t *self = sched_current();
    self->pml4 = pml4;

    /* Manually load CR3 now — we're about to drop to Ring 3 */
    u64 phys = vmm_virt_hhdm_to_phys(pml4);
    __asm__ volatile("mov %0, %%cr3" :: "r"(phys) : "memory");

    kfree(ba);

    /*
     * SysV x86_64 functions enter with RSP % 16 == 8, matching the state after
     * a call pushes a return address. User ELFs use _start as a C function
     * today, so enter with that ABI shape or compiler-emitted aligned SSE
     * stores can fault.
     */
    enter_user_mode(entry, USER_STACK_TOP - 8);
}

bool elf_load(const void *file_data, usize file_size, const char *name) {
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)file_data;

    /* ---- Validate ELF header ---- */
    if (file_size < sizeof(elf64_ehdr_t)) {
        kprintf_color(0xFFFF3333, "[ELF] File too small\n");
        return false;
    }
    if (ehdr->e_magic != ELF_MAGIC) {
        kprintf_color(0xFFFF3333, "[ELF] Bad magic: 0x%08x\n", ehdr->e_magic);
        return false;
    }
    if (ehdr->e_class != 2) {
        kprintf_color(0xFFFF3333, "[ELF] Not 64-bit (class=%u)\n", ehdr->e_class);
        return false;
    }
    if (ehdr->e_machine != 0x3E) {
        kprintf_color(0xFFFF3333, "[ELF] Not x86_64 (machine=0x%x)\n", ehdr->e_machine);
        return false;
    }

    kprintf_color(0xFF00DDFF, "[ELF] Loading '%s': entry=0x%lx, %u program headers\n",
                  name, ehdr->e_entry, ehdr->e_phnum);

    /* ---- Create isolated user address space ---- */
    u64 *user_pml4 = vmm_create_user_pml4();
    if (!user_pml4) {
        kprintf_color(0xFFFF3333, "[ELF] Failed to create user PML4\n");
        return false;
    }

    /* ---- Load PT_LOAD segments ---- */
    const u8 *base = (const u8 *)file_data;
    for (u16 i = 0; i < ehdr->e_phnum; i++) {
        const elf64_phdr_t *phdr = (const elf64_phdr_t *)(base + ehdr->e_phoff + i * ehdr->e_phentsize);

        if (phdr->p_type != PT_LOAD) continue;
        if (phdr->p_memsz == 0) continue;

        /* kprintf_color(0xFF88DDFF, "[ELF]   LOAD vaddr=0x%lx filesz=%lu memsz=%lu flags=%u\n",
                      phdr->p_vaddr, phdr->p_filesz, phdr->p_memsz, phdr->p_flags); */

        /* Calculate VMM flags */
        u64 pflags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
        if (phdr->p_flags & PF_W) pflags |= VMM_FLAG_WRITE;
        if (!(phdr->p_flags & PF_X)) pflags |= VMM_FLAG_NX;

        /* Map and copy pages */
        u64 seg_start = phdr->p_vaddr & ~0xFFFULL;
        u64 seg_end   = (phdr->p_vaddr + phdr->p_memsz + 0xFFF) & ~0xFFFULL;

        for (u64 va = seg_start; va < seg_end; va += PAGE_SIZE) {
            u64 phys = pmm_alloc_page();
            if (!phys) {
                kprintf_color(0xFFFF3333, "[ELF] OOM mapping segment\n");
                vmm_destroy_user_pml4(user_pml4);
                return false;
            }

            /* Zero the page first, then copy file data where applicable */
            void *kva = vmm_phys_to_virt(phys);
            memset(kva, 0, PAGE_SIZE);

            /* Calculate how much file data overlaps with this page */
            u64 file_seg_start = phdr->p_vaddr;
            u64 file_seg_end   = phdr->p_vaddr + phdr->p_filesz;

            if (va + PAGE_SIZE > file_seg_start && va < file_seg_end) {
                u64 copy_start = (va > file_seg_start) ? va : file_seg_start;
                u64 copy_end   = (va + PAGE_SIZE < file_seg_end) ? va + PAGE_SIZE : file_seg_end;
                u64 dst_offset = copy_start - va;
                u64 src_offset = copy_start - phdr->p_vaddr;
                memcpy((u8 *)kva + dst_offset, base + phdr->p_offset + src_offset, copy_end - copy_start);
            }

            vmm_map_page(user_pml4, va, phys, pflags);
        }
    }

    /* ---- Allocate user stack; adjacent pages stay unmapped as guards ---- */
    for (u32 i = 0; i < USER_STACK_PAGES; i++) {
        u64 va = USER_STACK_BASE + i * PAGE_SIZE;
        u64 phys = pmm_alloc_page();
        if (!phys) {
            kprintf_color(0xFFFF3333, "[ELF] OOM allocating user stack\n");
            vmm_destroy_user_pml4(user_pml4);
            return false;
        }
        memset(vmm_phys_to_virt(phys), 0, PAGE_SIZE);
        vmm_map_page(user_pml4, va, phys, VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_WRITE | VMM_FLAG_NX);
    }
    kprintf("[ELF] user stack guards low=0x%lx high=0x%lx\n",
            USER_STACK_GUARD_LOW, USER_STACK_GUARD_HIGH);

    /* ---- Spawn the user task ---- */
    elf_bootstrap_arg_t *ba = (elf_bootstrap_arg_t *)kmalloc(sizeof(elf_bootstrap_arg_t));
    if (!ba) {
        vmm_destroy_user_pml4(user_pml4);
        return false;
    }
    ba->entry = ehdr->e_entry;
    ba->pml4  = user_pml4;

    task_t *t = sched_spawn(name, elf_user_entry, ba, 2);
    if (!t) {
        kfree(ba);
        vmm_destroy_user_pml4(user_pml4);
        return false;
    }
    t->pml4 = user_pml4;

    kprintf_color(0xFF00FF88, "[ELF] User process '%s' spawned (id=%lu entry=0x%lx)\n",
                  name, t->id, ehdr->e_entry);
    return true;
}
