/* YamKernel — Ring 3 user-mode loader (demo) */
#include "sched.h"
#include "../cpu/percpu.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

extern char user_demo_start[], user_demo_end[];
extern void enter_user_mode(u64 rip, u64 rsp) NORETURN;

#define USER_CODE_VIRT  0x0000000000400000ULL
#define USER_STACK_VIRT 0x0000000000800000ULL
#define USER_STACK_TOP  (USER_STACK_VIRT + 0x1000)

static void user_bootstrap(void *arg) {
    (void)arg;
    enter_user_mode(USER_CODE_VIRT, USER_STACK_TOP);
}

void user_demo_load(void) {
    u64 *pml4 = vmm_get_kernel_pml4();
    u64 size = (u64)(user_demo_end - user_demo_start);

    /* Map a code page (user, exec) and copy program into it */
    u64 code_phys = pmm_alloc_page();
    memcpy(vmm_phys_to_virt(code_phys), user_demo_start, size);
    vmm_map_page(pml4, USER_CODE_VIRT, code_phys,
                 VMM_FLAG_PRESENT | VMM_FLAG_USER);

    /* Map a stack page (user, writable, NX) */
    u64 stack_phys = pmm_alloc_page();
    memset(vmm_phys_to_virt(stack_phys), 0, 4096);
    vmm_map_page(pml4, USER_STACK_VIRT, stack_phys,
                 VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_WRITE | VMM_FLAG_NX);

    sched_spawn("user-demo", user_bootstrap, NULL, 3);
    kprintf_color(0xFF00FF88,
        "[USER] mapped %lu bytes @ 0x%lx, stack @ 0x%lx\n",
        size, USER_CODE_VIRT, USER_STACK_VIRT);
}
