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
#include "vfs.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

extern void enter_user_mode(u64 rip, u64 rsp, u64 argc, u64 argv, u64 envp) NORETURN;

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
    u64 stack_top;
    u64 argc;
    u64 argv;
    u64 envp;
} elf_bootstrap_arg_t;

static void elf_user_entry(void *arg) {
    elf_bootstrap_arg_t *ba = (elf_bootstrap_arg_t *)arg;
    u64 entry = ba->entry;
    u64 *pml4 = ba->pml4;
    u64 stack_top = ba->stack_top;
    u64 argc = ba->argc;
    u64 argv = ba->argv;
    u64 envp = ba->envp;

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
    enter_user_mode(entry, stack_top, argc, argv, envp);
}

static bool copy_to_user_mapping(u64 *pml4, u64 dst, const void *src, usize len) {
    const u8 *s = (const u8 *)src;
    while (len > 0) {
        u64 phys = vmm_virt_to_phys(pml4, dst);
        if (!phys) return false;
        usize page_off = dst & (PAGE_SIZE - 1);
        usize chunk = PAGE_SIZE - page_off;
        if (chunk > len) chunk = len;
        memcpy((u8 *)vmm_phys_to_virt(phys & ~0xFFFULL) + page_off, s, chunk);
        dst += chunk;
        s += chunk;
        len -= chunk;
    }
    return true;
}

static int count_string_vector(const char *const items[], int max_items) {
    int count = 0;
    if (!items) return 0;
    while (count < max_items && items[count]) count++;
    return count;
}

static bool copy_string_vector(u64 *pml4, u64 *sp, int count,
                               const char *const items[], u64 ptrs[]) {
    if (!pml4 || !sp || !ptrs || count < 0) return false;
    for (int i = count - 1; i >= 0; i--) {
        const char *arg = items[i] ? items[i] : "";
        usize len = strlen(arg) + 1;
        if (len > 255) len = 255;
        *sp -= len;
        char tmp[256];
        memset(tmp, 0, sizeof(tmp));
        memcpy(tmp, arg, len - 1);
        if (!copy_to_user_mapping(pml4, *sp, tmp, len)) return false;
        ptrs[i] = *sp;
    }
    return true;
}

static bool copy_pointer_vector(u64 *pml4, u64 base, int count,
                                const u64 ptrs[]) {
    if (!pml4 || !ptrs || count < 0) return false;
    for (int i = 0; i < count; i++) {
        if (!copy_to_user_mapping(pml4, base + (u64)i * sizeof(u64),
                                  &ptrs[i], sizeof(u64))) {
            return false;
        }
    }
    u64 null_ptr = 0;
    return copy_to_user_mapping(pml4, base + (u64)count * sizeof(u64),
                                &null_ptr, sizeof(null_ptr));
}

static u64 build_initial_stack(u64 *pml4, const char *name, int argc,
                               const char *const argv[],
                               const char *const envp[],
                               u64 *argv_user_out, u64 *envp_user_out) {
    const char *fallback[1];
    u64 arg_ptrs[17];
    u64 env_ptrs[17];
    u64 sp = USER_STACK_TOP - 8;
    if (!name || !*name) name = "app";
    if (argc <= 0 || !argv) {
        fallback[0] = name;
        argv = fallback;
        argc = 1;
    }
    if (argc > 16) argc = 16;
    int envc = count_string_vector(envp, 16);

    if (!copy_string_vector(pml4, &sp, envc, envp, env_ptrs)) return 0;
    if (!copy_string_vector(pml4, &sp, argc, argv, arg_ptrs)) return 0;

    sp &= ~0xFULL;
    sp -= (u64)(envc + 1) * sizeof(u64);
    u64 envp_user = sp;
    if (!copy_pointer_vector(pml4, envp_user, envc, env_ptrs)) return 0;

    sp -= (u64)(argc + 1) * sizeof(u64);
    u64 argv_user = sp;
    if (!copy_pointer_vector(pml4, argv_user, argc, arg_ptrs)) return 0;

    if ((sp & 0xFULL) != 8) sp -= 8;
    if (argv_user_out) *argv_user_out = argv_user;
    if (envp_user_out) *envp_user_out = envp_user;
    return sp;
}

i64 elf_spawn_argv_envp(const void *file_data, usize file_size, const char *name,
                        int argc, const char *const argv[],
                        const char *const envp[]) {
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)file_data;

    /* ---- Validate ELF header ---- */
    if (!file_data || file_size < sizeof(elf64_ehdr_t)) {
        kprintf_color(0xFFFF3333, "[ELF] File too small\n");
        return -1;
    }
    if (ehdr->e_magic != ELF_MAGIC) {
        kprintf_color(0xFFFF3333, "[ELF] Bad magic: 0x%08x\n", ehdr->e_magic);
        return -1;
    }
    if (ehdr->e_class != 2) {
        kprintf_color(0xFFFF3333, "[ELF] Not 64-bit (class=%u)\n", ehdr->e_class);
        return -1;
    }
    if (ehdr->e_machine != 0x3E) {
        kprintf_color(0xFFFF3333, "[ELF] Not x86_64 (machine=0x%x)\n", ehdr->e_machine);
        return -1;
    }
    if (ehdr->e_phoff + (u64)ehdr->e_phnum * ehdr->e_phentsize > file_size) {
        kprintf_color(0xFFFF3333, "[ELF] Program headers outside file\n");
        return -1;
    }

    kprintf_color(0xFF00DDFF, "[ELF] Loading '%s': entry=0x%lx, %u program headers\n",
                  name, ehdr->e_entry, ehdr->e_phnum);

    /* ---- Create isolated user address space ---- */
    u64 *user_pml4 = vmm_create_user_pml4();
    if (!user_pml4) {
        kprintf_color(0xFFFF3333, "[ELF] Failed to create user PML4\n");
        return -1;
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
                return -1;
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
            return -1;
        }
        memset(vmm_phys_to_virt(phys), 0, PAGE_SIZE);
        vmm_map_page(user_pml4, va, phys, VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_WRITE | VMM_FLAG_NX);
    }
    kprintf("[ELF] user stack guards low=0x%lx high=0x%lx\n",
            USER_STACK_GUARD_LOW, USER_STACK_GUARD_HIGH);

    u64 argv_user = 0;
    u64 envp_user = 0;
    u64 user_rsp = build_initial_stack(user_pml4, name, argc, argv, envp,
                                       &argv_user, &envp_user);
    if (!user_rsp) {
        kprintf_color(0xFFFF3333, "[ELF] Failed to build initial user stack\n");
        vmm_destroy_user_pml4(user_pml4);
        return -1;
    }

    /* ---- Spawn the user task ---- */
    elf_bootstrap_arg_t *ba = (elf_bootstrap_arg_t *)kmalloc(sizeof(elf_bootstrap_arg_t));
    if (!ba) {
        vmm_destroy_user_pml4(user_pml4);
        return -1;
    }
    ba->entry = ehdr->e_entry;
    ba->pml4  = user_pml4;
    ba->stack_top = user_rsp;
    ba->argc = (argc <= 0) ? 1 : (argc > 16 ? 16 : (u64)argc);
    ba->argv = argv_user;
    ba->envp = envp_user;

    task_t *t = sched_spawn(name, elf_user_entry, ba, 2);
    if (!t) {
        kfree(ba);
        vmm_destroy_user_pml4(user_pml4);
        return -1;
    }
    t->pml4 = user_pml4;

    kprintf_color(0xFF00FF88, "[ELF] User process '%s' spawned (id=%lu entry=0x%lx)\n",
                  name, t->id, ehdr->e_entry);
    return (i64)t->id;
}

i64 elf_spawn_argv(const void *file_data, usize file_size, const char *name,
                   int argc, const char *const argv[]) {
    return elf_spawn_argv_envp(file_data, file_size, name, argc, argv, NULL);
}

i64 elf_spawn(const void *file_data, usize file_size, const char *name) {
    return elf_spawn_argv(file_data, file_size, name, 0, NULL);
}

bool elf_load(const void *file_data, usize file_size, const char *name) {
    return elf_spawn(file_data, file_size, name) >= 0;
}

static const char *elf_basename(const char *path) {
    const char *base = path;
    if (!path) return "app";
    for (const char *p = path; *p; p++) {
        if (*p == '/' && p[1]) base = p + 1;
    }
    return base;
}

static bool path_has_slash(const char *path) {
    if (!path) return false;
    for (const char *p = path; *p; p++) {
        if (*p == '/') return true;
    }
    return false;
}

static bool elf_path_has_magic(const char *path) {
    u8 magic[4];
    int fd = sys_open(path, 0);
    if (fd < 0) return false;
    isize n = sys_read(fd, magic, sizeof(magic));
    sys_close(fd);
    if (n != (isize)sizeof(magic)) return false;
    u32 value = (u32)magic[0] |
                ((u32)magic[1] << 8) |
                ((u32)magic[2] << 16) |
                ((u32)magic[3] << 24);
    return value == ELF_MAGIC;
}

static bool elf_resolve_executable(const char *name, char *out, usize out_cap) {
    static const char *search_dirs[] = {
        "/bin",
        "/usr/local/bin",
        "/opt/yamos/packages",
        "/home/root/bin",
    };
    if (!name || !*name || !out || out_cap == 0) return false;
    out[0] = 0;

    if (path_has_slash(name)) {
        if (!elf_path_has_magic(name)) return false;
        strncpy(out, name, out_cap - 1);
        out[out_cap - 1] = 0;
        return true;
    }

    for (u32 i = 0; i < sizeof(search_dirs) / sizeof(search_dirs[0]); i++) {
        char candidate[256];
        ksnprintf(candidate, sizeof(candidate), "%s/%s", search_dirs[i], name);
        if (!elf_path_has_magic(candidate)) continue;
        strncpy(out, candidate, out_cap - 1);
        out[out_cap - 1] = 0;
        return true;
    }
    return false;
}

i64 elf_spawn_path_argv_envp(const char *path, int argc,
                             const char *const argv[],
                             const char *const envp[]) {
    if (!path || !*path) return -1;

    int fd = sys_open(path, 0);
    if (fd < 0) {
        kprintf("[ELF] spawn path='%s' open failed\n", path);
        return -1;
    }

    usize cap = 65536;
    usize size = 0;
    u8 *buf = (u8 *)kmalloc(cap);
    if (!buf) {
        sys_close(fd);
        return -1;
    }

    for (;;) {
        if (size == cap) {
            usize new_cap = cap * 2;
            if (new_cap > 4 * 1024 * 1024) {
                kprintf("[ELF] spawn path='%s' too large cap=%lu\n", path, cap);
                kfree(buf);
                sys_close(fd);
                return -1;
            }
            u8 *next = (u8 *)kmalloc(new_cap);
            if (!next) {
                kfree(buf);
                sys_close(fd);
                return -1;
            }
            memcpy(next, buf, size);
            kfree(buf);
            buf = next;
            cap = new_cap;
        }

        isize n = sys_read(fd, buf + size, cap - size);
        if (n < 0) {
            kprintf("[ELF] spawn path='%s' read failed\n", path);
            kfree(buf);
            sys_close(fd);
            return -1;
        }
        if (n == 0) break;
        size += (usize)n;
    }
    sys_close(fd);

    const char *fallback[1];
    if (argc <= 0 || !argv) {
        fallback[0] = path;
        argv = fallback;
        argc = 1;
    }
    i64 pid = elf_spawn_argv_envp(buf, size, elf_basename(path), argc, argv, envp);
    kfree(buf);
    if (pid >= 0) {
        kprintf("[ELF] spawn path='%s' -> pid=%ld bytes=%lu\n", path, pid, size);
    }
    return pid;
}

i64 elf_spawn_resolved_argv_envp(const char *name, int argc,
                                 const char *const argv[],
                                 const char *const envp[]) {
    char resolved[256];
    if (!elf_resolve_executable(name, resolved, sizeof(resolved))) {
        kprintf("[ELF] resolve executable '%s' failed\n", name ? name : "(null)");
        return -1;
    }

    const char *fallback[17];
    if (argc <= 0 || !argv) {
        fallback[0] = resolved;
        fallback[1] = NULL;
        return elf_spawn_path_argv_envp(resolved, 1, fallback, envp);
    }

    fallback[0] = resolved;
    int capped = argc > 16 ? 16 : argc;
    for (int i = 1; i < capped; i++) fallback[i] = argv[i];
    fallback[capped] = NULL;
    return elf_spawn_path_argv_envp(resolved, capped, fallback, envp);
}

i64 elf_spawn_path_argv(const char *path, int argc, const char *const argv[]) {
    return elf_spawn_path_argv_envp(path, argc, argv, NULL);
}

i64 elf_spawn_path(const char *path) {
    return elf_spawn_path_argv(path, 0, NULL);
}
