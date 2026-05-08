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
extern void enter_user_mode_ldso(u64 rip, u64 rsp) NORETURN;

#define ELF_INTERP_GAP_BYTES (2u * 1024u * 1024u)

/* User stack lives at the top of the lower-half canonical address space */
#define USER_STACK_GUARD_LOW  0x00007FFEFFFFF000ULL
#define USER_STACK_BASE       0x00007FFF00000000ULL
#define USER_STACK_PAGES 256  /* 1 MB user stack for larger real-world ELF apps. */
#define USER_STACK_SIZE  (USER_STACK_PAGES * PAGE_SIZE)
#define USER_STACK_TOP   (USER_STACK_BASE + USER_STACK_SIZE)
#define USER_STACK_GUARD_HIGH USER_STACK_TOP
#define USER_ELF_DYN_BASE     0x0000000000400000ULL

/* Per-process bootstrap: switch to its page table and jump to user mode */
typedef struct {
    u64 entry;
    u64 *pml4;
    u64 stack_top;
    u64 argc;
    u64 argv;
    u64 envp;
} elf_bootstrap_arg_t;

typedef struct {
    u64 entry;
    u64 *pml4;
    u64 stack_top;
} elf_ldso_bootstrap_arg_t;

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

static void elf_user_entry_ldso(void *arg) {
    elf_ldso_bootstrap_arg_t *ba = (elf_ldso_bootstrap_arg_t *)arg;
    u64 entry = ba->entry;
    u64 *pml4 = ba->pml4;
    u64 stack_top = ba->stack_top;

    kprintf("[ELF] Transitioning to ld.so: entry=0x%lx, pml4=0x%lx\n", entry,
            (u64)pml4);

    task_t *self = sched_current();
    self->pml4 = pml4;

    u64 phys = vmm_virt_hhdm_to_phys(pml4);
    __asm__ volatile("mov %0, %%cr3" :: "r"(phys) : "memory");

    kfree(ba);

    enter_user_mode_ldso(entry, stack_top);
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

static const elf64_phdr_t *elf_phdr_at(const elf64_ehdr_t *ehdr, const u8 *base,
                                       u16 idx) {
  if (idx >= ehdr->e_phnum) return NULL;
  return (const elf64_phdr_t *)(base + ehdr->e_phoff +
                                (usize)idx * ehdr->e_phentsize);
}

static const elf64_phdr_t *elf_find_phdr(const elf64_ehdr_t *ehdr, const u8 *base,
                                         u32 kind) {
  for (u16 i = 0; i < ehdr->e_phnum; i++) {
    const elf64_phdr_t *ph = elf_phdr_at(ehdr, base, i);
    if (ph->p_type == kind) return ph;
  }
  return NULL;
}

static bool elf_va_to_file_off(const elf64_ehdr_t *ehdr, const u8 *base, u64 va,
                               u64 *off_out) {
  for (u16 i = 0; i < ehdr->e_phnum; i++) {
    const elf64_phdr_t *ph = elf_phdr_at(ehdr, base, i);
    if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;
    if (va >= ph->p_vaddr && va < ph->p_vaddr + ph->p_memsz) {
      u64 delta = va - ph->p_vaddr;
      if (delta >= ph->p_filesz)
        return false;
      *off_out = ph->p_offset + delta;
      return true;
    }
  }
  return false;
}

static u64 elf_min_load_vaddr(const elf64_ehdr_t *ehdr, const u8 *base) {
  u64 min_v = ~(u64)0;
  for (u16 i = 0; i < ehdr->e_phnum; i++) {
    const elf64_phdr_t *ph = elf_phdr_at(ehdr, base, i);
    if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;
    if (ph->p_vaddr < min_v) min_v = ph->p_vaddr;
  }
  return min_v == ~(u64)0 ? 0 : min_v;
}

static u64 elf_align_up(u64 x, u64 align) {
  if (align == 0) return x;
  return (x + align - 1) & ~(align - 1);
}

static u64 elf_max_load_end(const elf64_ehdr_t *ehdr, const u8 *base,
                            u64 load_bias) {
  u64 mx = 0;
  for (u16 i = 0; i < ehdr->e_phnum; i++) {
    const elf64_phdr_t *ph = elf_phdr_at(ehdr, base, i);
    if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;
    u64 e = load_bias + ph->p_vaddr + ph->p_memsz;
    if (e > mx) mx = e;
  }
  return mx;
}

static bool elf_phdr_runtime_addr(const elf64_ehdr_t *ehdr, const u8 *base,
                                  u64 load_bias, u64 *out) {
  u64 phoff = ehdr->e_phoff;
  for (u16 i = 0; i < ehdr->e_phnum; i++) {
    const elf64_phdr_t *ph = elf_phdr_at(ehdr, base, i);
    if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;
    if (phoff >= ph->p_offset && phoff < ph->p_offset + ph->p_filesz) {
      u64 delta = phoff - ph->p_offset;
      *out = load_bias + ph->p_vaddr + delta;
      return true;
    }
  }
  return false;
}

static bool elf_map_pt_loads(u64 *pml4, const elf64_ehdr_t *ehdr, const u8 *base,
                             u64 load_bias) {
  for (u16 i = 0; i < ehdr->e_phnum; i++) {
    const elf64_phdr_t *phdr = elf_phdr_at(ehdr, base, i);

    if (phdr->p_type != PT_LOAD) continue;
    if (phdr->p_memsz == 0) continue;

    u64 pflags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
    if (phdr->p_flags & PF_W) pflags |= VMM_FLAG_WRITE;
    if (!(phdr->p_flags & PF_X)) pflags |= VMM_FLAG_NX;

    u64 seg_base = phdr->p_vaddr + load_bias;
    u64 seg_start = seg_base & ~0xFFFULL;
    u64 seg_end = (seg_base + phdr->p_memsz + 0xFFF) & ~0xFFFULL;

    for (u64 va = seg_start; va < seg_end; va += PAGE_SIZE) {
      u64 phys = pmm_alloc_page();
      if (!phys) {
        kprintf_color(0xFFFF3333, "[ELF] OOM mapping segment\n");
        return false;
      }

      void *kva = vmm_phys_to_virt(phys);
      memset(kva, 0, PAGE_SIZE);

      u64 file_seg_start = seg_base;
      u64 file_seg_end = seg_base + phdr->p_filesz;

      if (va + PAGE_SIZE > file_seg_start && va < file_seg_end) {
        u64 copy_start = (va > file_seg_start) ? va : file_seg_start;
        u64 copy_end =
            (va + PAGE_SIZE < file_seg_end) ? va + PAGE_SIZE : file_seg_end;
        u64 dst_offset = copy_start - va;
        u64 src_offset = copy_start - seg_base;
        memcpy((u8 *)kva + dst_offset, base + phdr->p_offset + src_offset,
               copy_end - copy_start);
      }

      vmm_map_page(pml4, va, phys, pflags);
    }
  }
  return true;
}

static u8 *elf_read_whole_file(const char *path, usize *out_size) {
  int fd = sys_open(path, 0);
  if (fd < 0) {
    kprintf_color(0xFFFF3333, "[ELF] open '%s' failed\n", path ? path : "(null)");
    return NULL;
  }

  usize cap = 65536;
  usize size = 0;
  u8 *buf = (u8 *)kmalloc(cap);
  if (!buf) {
    sys_close(fd);
    return NULL;
  }

  for (;;) {
    if (size == cap) {
      usize new_cap = cap * 2;
      if (new_cap > 4 * 1024 * 1024) {
        kprintf_color(0xFFFF3333, "[ELF] file '%s' too large\n", path);
        kfree(buf);
        sys_close(fd);
        return NULL;
      }
      u8 *next = (u8 *)kmalloc(new_cap);
      if (!next) {
        kfree(buf);
        sys_close(fd);
        return NULL;
      }
      memcpy(next, buf, size);
      kfree(buf);
      buf = next;
      cap = new_cap;
    }

    isize n = sys_read(fd, buf + size, cap - size);
    if (n < 0) {
      kprintf_color(0xFFFF3333, "[ELF] read '%s' failed\n", path);
      kfree(buf);
      sys_close(fd);
      return NULL;
    }
    if (n == 0) break;
    size += (usize)n;
  }
  sys_close(fd);
  *out_size = size;
  return buf;
}

static bool elf_write_u64_user(u64 *pml4, u64 va, u64 val) {
  if ((va & 7u) != 0) return false;
  u64 phys = vmm_virt_to_phys(pml4, va);
  if (!phys) return false;
  *(volatile u64 *)(vmm_phys_to_virt(phys)) = val;
  return true;
}

static bool elf_apply_relocations(u64 *pml4, const elf64_ehdr_t *ehdr,
                                  const u8 *base, usize file_size, u64 load_base,
                                  u64 va_bias) {
  const elf64_phdr_t *dyn_ph = elf_find_phdr(ehdr, base, PT_DYNAMIC);
  if (!dyn_ph || dyn_ph->p_memsz == 0) return true;

  if (dyn_ph->p_offset + dyn_ph->p_filesz > file_size) {
    kprintf_color(0xFFFF3333, "[ELF] PT_DYNAMIC outside file\n");
    return false;
  }

  const elf64_dyn_t *dstart =
      (const elf64_dyn_t *)(base + dyn_ph->p_offset);
  const u8 *dend = base + dyn_ph->p_offset + dyn_ph->p_filesz;

  u64 rela_va = 0, relasz = 0, relaent = sizeof(elf64_rela_t);

  for (const elf64_dyn_t *cur = dstart;
       (const u8 *)cur + sizeof(elf64_dyn_t) <= dend; cur++) {
    if (cur->d_tag == DT_NULL) break;
    if (cur->d_tag == DT_RELA) rela_va = cur->d_un;
    else if (cur->d_tag == DT_RELASZ) relasz = cur->d_un;
    else if (cur->d_tag == DT_RELAENT && cur->d_un != 0) relaent = cur->d_un;
  }

  if (relasz == 0) return true;
  if (rela_va == 0 || relaent < sizeof(elf64_rela_t)) {
    kprintf_color(0xFFFF3333,
                  "[ELF] PT_DYNAMIC but invalid DT_RELA / DT_RELASZ / DT_RELAENT\n");
    return false;
  }

  u64 rela_file_off = 0;
  if (!elf_va_to_file_off(ehdr, base, rela_va, &rela_file_off)) {
    kprintf_color(0xFFFF3333, "[ELF] DT_RELA vaddr 0x%lx not covered by PT_LOAD\n",
                  (unsigned long)rela_va);
    return false;
  }

  if (rela_file_off + relasz > file_size) {
    kprintf_color(0xFFFF3333, "[ELF] RELA table extends past file\n");
    return false;
  }

  for (u64 off = 0; off + sizeof(elf64_rela_t) <= relasz; off += relaent) {
    const elf64_rela_t *rr =
        (const elf64_rela_t *)(base + rela_file_off + off);
    u32 type = ELF64_R_TYPE(rr->r_info);
    u64 loc = va_bias + rr->r_offset;

    if (type == R_X86_64_RELATIVE) {
      u64 val = load_base + (u64)rr->r_addend;
      if (!elf_write_u64_user(pml4, loc, val)) {
        kprintf_color(0xFFFF3333,
                      "[ELF] R_X86_64_RELATIVE failed at loc=0x%lx\n",
                      (unsigned long)loc);
        return false;
      }
    } else if (type == R_X86_64_NONE) {
      continue;
    } else {
      kprintf_color(0xFFFF3333,
                    "[ELF] unsupported Rela type %u at 0x%lx (need full B1)\n",
                    type, (unsigned long)loc);
      return false;
    }
  }
  kprintf_color(0xFF88DDFF, "[ELF] applied RELA relocations (%lu bytes)\n",
                (unsigned long)relasz);
  return true;
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

/*
 * Linux/musl dynamic linker startup: stack holds argc, argv, envp, auxv (pairs),
 * ending with AT_NULL. User %rsp points at argc; ld.so _dl_start(void *sp) receives
 * that pointer in %rdi.
 */
static u64 build_initial_stack_ldso(u64 *pml4, const char *name, int argc,
                                    const char *const argv[],
                                    const char *const envp[],
                                    const char *execfn, u64 at_phdr,
                                    u16 phent, u16 phnum, u64 at_entry,
                                    u64 at_base) {
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

  u64 execfn_user = 0;
  if (execfn && execfn[0]) {
    usize elen = strlen(execfn) + 1;
    if (elen > 255) elen = 255;
    sp -= elen;
    char tmp[256];
    memset(tmp, 0, sizeof(tmp));
    memcpy(tmp, execfn, elen - 1);
    if (!copy_to_user_mapping(pml4, sp, tmp, elen)) return 0;
    execfn_user = sp;
  }

  sp -= 16;
  u64 random_slot = sp;
  {
    u8 rnd[16];
    memset(rnd, 0x41, sizeof(rnd));
    if (!copy_to_user_mapping(pml4, random_slot, rnd, sizeof(rnd))) return 0;
  }

  sp &= ~0xFULL;

  u64 aux[40];
  int aw = 0;
  aux[aw++] = AT_PHDR;
  aux[aw++] = at_phdr;
  aux[aw++] = AT_PHENT;
  aux[aw++] = phent;
  aux[aw++] = AT_PHNUM;
  aux[aw++] = phnum;
  aux[aw++] = AT_PAGESZ;
  aux[aw++] = PAGE_SIZE;
  aux[aw++] = AT_BASE;
  aux[aw++] = at_base;
  aux[aw++] = AT_FLAGS;
  aux[aw++] = 0;
  aux[aw++] = AT_ENTRY;
  aux[aw++] = at_entry;
  aux[aw++] = AT_RANDOM;
  aux[aw++] = random_slot;
  if (execfn_user) {
    aux[aw++] = AT_EXECFN;
    aux[aw++] = execfn_user;
  }
  aux[aw++] = AT_NULL;
  aux[aw++] = 0;

  usize aux_bytes = (usize)aw * sizeof(u64);
  sp -= aux_bytes;
  if (!copy_to_user_mapping(pml4, sp, aux, aux_bytes)) return 0;

  sp -= (u64)(envc + 1) * sizeof(u64);
  u64 envp_user = sp;
  if (!copy_pointer_vector(pml4, envp_user, envc, env_ptrs)) return 0;

  sp -= (u64)(argc + 1) * sizeof(u64);
  u64 argv_user = sp;
  if (!copy_pointer_vector(pml4, argv_user, argc, arg_ptrs)) return 0;

  if ((sp & 0xFULL) != 8) sp -= 8;

  u64 argc_slot_val = (argc <= 0) ? 1 : (u64)argc;
  if (!elf_write_u64_user(pml4, sp, argc_slot_val)) return 0;

  return sp;
}

static bool allocate_user_stack(u64 *user_pml4) {
  for (u32 i = 0; i < USER_STACK_PAGES; i++) {
    u64 va = USER_STACK_BASE + i * PAGE_SIZE;
    u64 phys = pmm_alloc_page();
    if (!phys) {
      kprintf_color(0xFFFF3333, "[ELF] OOM allocating user stack\n");
      return false;
    }
    memset(vmm_phys_to_virt(phys), 0, PAGE_SIZE);
    vmm_map_page(user_pml4, va, phys,
                 VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_WRITE |
                     VMM_FLAG_NX);
  }
  kprintf("[ELF] user stack guards low=0x%lx high=0x%lx\n",
          USER_STACK_GUARD_LOW, USER_STACK_GUARD_HIGH);
  return true;
}

typedef struct {
  u64 *pml4;
  u64 entry;
  u64 rsp;
  u64 argc;
  u64 argv;
  u64 envp;
  u64 brk_start;
  bool ldso_entry;
} elf_exec_image_t;

static i64 elf_prepare_static_image(const void *file_data, usize file_size,
                                    const char *name, int argc,
                                    const char *const argv[],
                                    const char *const envp[],
                                    elf_exec_image_t *out) {
  if (!out) return -1;
  memset(out, 0, sizeof(*out));

  if (!file_data || file_size < sizeof(elf64_ehdr_t)) {
    kprintf_color(0xFFFF3333, "[ELF] exec: file too small\n");
    return -1;
  }

  const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)file_data;
  const u8 *base = (const u8 *)file_data;

  if (ehdr->e_magic != ELF_MAGIC || ehdr->e_class != 2 ||
      ehdr->e_machine != 0x3E) {
    kprintf_color(0xFFFF3333, "[ELF] exec: invalid x86_64 ELF\n");
    return -1;
  }
  if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
    kprintf_color(0xFFFF3333, "[ELF] exec: unsupported e_type=%u\n",
                  ehdr->e_type);
    return -1;
  }
  if (ehdr->e_phentsize != sizeof(elf64_phdr_t) ||
      ehdr->e_phoff + (u64)ehdr->e_phnum * ehdr->e_phentsize > file_size) {
    kprintf_color(0xFFFF3333, "[ELF] exec: program headers outside file\n");
    return -1;
  }

  u64 *user_pml4 = vmm_create_user_pml4();
  if (!user_pml4) {
    kprintf_color(0xFFFF3333, "[ELF] exec: failed to create user PML4\n");
    return -1;
  }

  u64 load_bias = (ehdr->e_type == ET_DYN) ? USER_ELF_DYN_BASE : 0;
  if (!elf_map_pt_loads(user_pml4, ehdr, base, load_bias)) {
    vmm_destroy_user_pml4(user_pml4);
    return -1;
  }

  u64 load_base = load_bias + elf_min_load_vaddr(ehdr, base);
  if (!elf_apply_relocations(user_pml4, ehdr, base, file_size, load_base,
                             load_bias)) {
    vmm_destroy_user_pml4(user_pml4);
    return -1;
  }

  if (!allocate_user_stack(user_pml4)) {
    vmm_destroy_user_pml4(user_pml4);
    return -1;
  }

  u64 argv_user = 0;
  u64 envp_user = 0;
  u64 user_rsp = build_initial_stack(user_pml4, name, argc, argv, envp,
                                     &argv_user, &envp_user);
  if (!user_rsp) {
    vmm_destroy_user_pml4(user_pml4);
    return -1;
  }

  out->pml4 = user_pml4;
  out->entry = load_bias + ehdr->e_entry;
  out->rsp = user_rsp;
  out->argc = (argc <= 0) ? 1 : (argc > 16 ? 16 : (u64)argc);
  out->argv = argv_user;
  out->envp = envp_user;
  out->brk_start = elf_align_up(elf_max_load_end(ehdr, base, load_bias),
                                PAGE_SIZE) + PAGE_SIZE;
  out->ldso_entry = false;
  return 0;
}

static i64 elf_prepare_dynamic_image(const void *file_data, usize file_size,
                                     const char *name, int argc,
                                     const char *const argv[],
                                     const char *const envp[],
                                     const char *execfn,
                                     elf_exec_image_t *out) {
  if (!out) return -1;
  memset(out, 0, sizeof(*out));

  if (!file_data || file_size < sizeof(elf64_ehdr_t)) return -1;

  const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)file_data;
  const u8 *base = (const u8 *)file_data;
  if (ehdr->e_magic != ELF_MAGIC || ehdr->e_class != 2 ||
      ehdr->e_machine != 0x3E ||
      (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN)) {
    return -1;
  }
  if (ehdr->e_phentsize != sizeof(elf64_phdr_t) ||
      ehdr->e_phoff + (u64)ehdr->e_phnum * ehdr->e_phentsize > file_size)
    return -1;

  char interp_path[256];
  interp_path[0] = 0;
  const elf64_phdr_t *interp_ph = elf_find_phdr(ehdr, base, PT_INTERP);
  if (!interp_ph || interp_ph->p_filesz == 0 ||
      interp_ph->p_offset + interp_ph->p_filesz > file_size) {
    return -1;
  }

  usize il = (usize)interp_ph->p_filesz;
  if (il >= sizeof(interp_path)) il = sizeof(interp_path) - 1;
  memcpy(interp_path, base + interp_ph->p_offset, il);
  interp_path[il] = 0;
  for (usize j = 0; j < il && interp_path[j]; j++) {
    if (interp_path[j] == '\n' || interp_path[j] == '\r')
      interp_path[j] = 0;
  }
  if (!interp_path[0]) return -1;

  usize interp_sz = 0;
  u8 *interp_buf = elf_read_whole_file(interp_path, &interp_sz);
  if (!interp_buf || interp_sz < sizeof(elf64_ehdr_t)) {
    kprintf_color(0xFFFF3333, "[ELF] exec: could not read interpreter '%s'\n",
                  interp_path);
    kfree(interp_buf);
    return -1;
  }

  const elf64_ehdr_t *ieh = (const elf64_ehdr_t *)(void *)interp_buf;
  if (ieh->e_magic != ELF_MAGIC || ieh->e_class != 2 ||
      ieh->e_machine != 0x3E || ieh->e_type != ET_DYN ||
      ieh->e_phentsize != sizeof(elf64_phdr_t) ||
      ieh->e_phoff + (u64)ieh->e_phnum * ieh->e_phentsize > interp_sz) {
    kprintf_color(0xFFFF3333,
                  "[ELF] exec: interpreter '%s' is not valid ET_DYN ELF\n",
                  interp_path);
    kfree(interp_buf);
    return -1;
  }

  u64 *user_pml4 = vmm_create_user_pml4();
  if (!user_pml4) {
    kfree(interp_buf);
    return -1;
  }

  u64 main_bias = (ehdr->e_type == ET_DYN) ? USER_ELF_DYN_BASE : 0;
  if (!elf_map_pt_loads(user_pml4, ehdr, base, main_bias)) {
    vmm_destroy_user_pml4(user_pml4);
    kfree(interp_buf);
    return -1;
  }

  u64 main_load_base = main_bias + elf_min_load_vaddr(ehdr, base);
  if (!elf_apply_relocations(user_pml4, ehdr, base, file_size,
                             main_load_base, main_bias)) {
    vmm_destroy_user_pml4(user_pml4);
    kfree(interp_buf);
    return -1;
  }

  u64 max_main = elf_max_load_end(ehdr, base, main_bias);
  u64 interp_bias = elf_align_up(max_main + ELF_INTERP_GAP_BYTES, PAGE_SIZE);

  if (!elf_map_pt_loads(user_pml4, ieh, interp_buf, interp_bias)) {
    vmm_destroy_user_pml4(user_pml4);
    kfree(interp_buf);
    return -1;
  }

  u64 interp_min = elf_min_load_vaddr(ieh, interp_buf);
  u64 interp_load_base = interp_bias + interp_min;
  if (!elf_apply_relocations(user_pml4, ieh, interp_buf, interp_sz,
                             interp_load_base, interp_bias)) {
    vmm_destroy_user_pml4(user_pml4);
    kfree(interp_buf);
    return -1;
  }

  u64 at_phdr = 0;
  if (!elf_phdr_runtime_addr(ehdr, base, main_bias, &at_phdr)) {
    vmm_destroy_user_pml4(user_pml4);
    kfree(interp_buf);
    return -1;
  }

  if (!allocate_user_stack(user_pml4)) {
    vmm_destroy_user_pml4(user_pml4);
    kfree(interp_buf);
    return -1;
  }

  u64 user_rsp = build_initial_stack_ldso(
      user_pml4, name, argc, argv, envp, execfn ? execfn : name, at_phdr,
      ehdr->e_phentsize, ehdr->e_phnum, main_bias + ehdr->e_entry,
      interp_load_base);
  if (!user_rsp) {
    vmm_destroy_user_pml4(user_pml4);
    kfree(interp_buf);
    return -1;
  }

  out->pml4 = user_pml4;
  out->entry = interp_bias + ieh->e_entry;
  out->rsp = user_rsp;
  out->argc = (argc <= 0) ? 1 : (argc > 16 ? 16 : (u64)argc);
  out->argv = 0;
  out->envp = 0;
  out->brk_start = elf_align_up(max_main, PAGE_SIZE) + PAGE_SIZE;
  out->ldso_entry = true;

  kprintf_color(0xFF88DDFF,
                "[ELF] exec PT_INTERP '%s' bias=0x%lx entry=0x%lx\n",
                interp_path, interp_bias, out->entry);
  kfree(interp_buf);
  return 0;
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
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
      kprintf_color(0xFFFF3333, "[ELF] unsupported e_type=%u (need ET_EXEC or ET_DYN)\n",
                    ehdr->e_type);
      return -1;
    }
    if (ehdr->e_phoff + (u64)ehdr->e_phnum * ehdr->e_phentsize > file_size) {
        kprintf_color(0xFFFF3333, "[ELF] Program headers outside file\n");
        return -1;
    }

    const u8 *base = (const u8 *)file_data;

    kprintf_color(0xFF00DDFF, "[ELF] Loading '%s': entry=0x%lx, %u program headers\n",
                  name, ehdr->e_entry, ehdr->e_phnum);

    char interp_path[256];
    interp_path[0] = 0;
    const elf64_phdr_t *interp_ph = elf_find_phdr(ehdr, base, PT_INTERP);
    if (interp_ph && interp_ph->p_filesz > 0 &&
        interp_ph->p_offset + interp_ph->p_filesz <= file_size) {
      usize il = (usize)interp_ph->p_filesz;
      if (il >= sizeof(interp_path)) il = sizeof(interp_path) - 1;
      memcpy(interp_path, base + interp_ph->p_offset, il);
      interp_path[il] = 0;
      for (usize j = 0; j < il && interp_path[j]; j++) {
        if (interp_path[j] == '\n' || interp_path[j] == '\r')
          interp_path[j] = 0;
      }
    }

    if (interp_path[0]) {
      usize interp_sz = 0;
      u8 *interp_buf = elf_read_whole_file(interp_path, &interp_sz);
      if (!interp_buf || interp_sz < sizeof(elf64_ehdr_t)) {
        kprintf_color(0xFFFF3333, "[ELF] could not read interpreter '%s'\n",
                      interp_path);
        kfree(interp_buf);
        return -1;
      }
      const elf64_ehdr_t *ieh = (const elf64_ehdr_t *)(void *)interp_buf;
      if (ieh->e_magic != ELF_MAGIC || ieh->e_class != 2 ||
          ieh->e_machine != 0x3E || ieh->e_type != ET_DYN) {
        kprintf_color(0xFFFF3333,
                      "[ELF] interpreter '%s' is not ET_DYN x86-64 ELF\n",
                      interp_path);
        kfree(interp_buf);
        return -1;
      }
      if (ieh->e_phoff + (u64)ieh->e_phnum * ieh->e_phentsize > interp_sz) {
        kprintf_color(0xFFFF3333, "[ELF] interpreter program headers invalid\n");
        kfree(interp_buf);
        return -1;
      }

      u64 *user_pml4 = vmm_create_user_pml4();
      if (!user_pml4) {
        kprintf_color(0xFFFF3333, "[ELF] Failed to create user PML4\n");
        kfree(interp_buf);
        return -1;
      }

      if (!elf_map_pt_loads(user_pml4, ehdr, base, 0)) {
        vmm_destroy_user_pml4(user_pml4);
        kfree(interp_buf);
        return -1;
      }

      u64 max_main = elf_max_load_end(ehdr, base, 0);
      u64 interp_bias =
          elf_align_up(max_main + ELF_INTERP_GAP_BYTES, PAGE_SIZE);

      if (!elf_map_pt_loads(user_pml4, ieh, interp_buf, interp_bias)) {
        vmm_destroy_user_pml4(user_pml4);
        kfree(interp_buf);
        return -1;
      }

      u64 interp_min = elf_min_load_vaddr(ieh, interp_buf);
      u64 interp_load_base = interp_bias + interp_min;

      if (!elf_apply_relocations(user_pml4, ieh, interp_buf, interp_sz,
                                 interp_load_base, interp_bias)) {
        kprintf_color(0xFFFF3333, "[ELF] interpreter relocation failed\n");
        vmm_destroy_user_pml4(user_pml4);
        kfree(interp_buf);
        return -1;
      }

      u64 interp_entry = interp_bias + ieh->e_entry;

      u64 at_phdr = 0;
      if (!elf_phdr_runtime_addr(ehdr, base, 0, &at_phdr)) {
        kprintf_color(0xFFFF3333, "[ELF] AT_PHDR: program headers not in PT_LOAD\n");
        vmm_destroy_user_pml4(user_pml4);
        kfree(interp_buf);
        return -1;
      }

      kprintf_color(0xFF88DDFF,
                    "[ELF] PT_INTERP '%s' mapped at bias 0x%lx entry=0x%lx\n",
                    interp_path, interp_bias, interp_entry);

      for (u32 i = 0; i < USER_STACK_PAGES; i++) {
        u64 va = USER_STACK_BASE + i * PAGE_SIZE;
        u64 phys = pmm_alloc_page();
        if (!phys) {
          kprintf_color(0xFFFF3333, "[ELF] OOM allocating user stack\n");
          vmm_destroy_user_pml4(user_pml4);
          kfree(interp_buf);
          return -1;
        }
        memset(vmm_phys_to_virt(phys), 0, PAGE_SIZE);
        vmm_map_page(user_pml4, va, phys,
                     VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_WRITE |
                         VMM_FLAG_NX);
      }
      kprintf("[ELF] user stack guards low=0x%lx high=0x%lx\n",
              USER_STACK_GUARD_LOW, USER_STACK_GUARD_HIGH);

      u64 user_rsp = build_initial_stack_ldso(
          user_pml4, name, argc, argv, envp, name, at_phdr, ehdr->e_phentsize,
          ehdr->e_phnum, ehdr->e_entry, interp_load_base);
      if (!user_rsp) {
        kprintf_color(0xFFFF3333, "[ELF] Failed to build ld.so initial stack\n");
        vmm_destroy_user_pml4(user_pml4);
        kfree(interp_buf);
        return -1;
      }

      elf_ldso_bootstrap_arg_t *ba =
          (elf_ldso_bootstrap_arg_t *)kmalloc(sizeof(*ba));
      if (!ba) {
        vmm_destroy_user_pml4(user_pml4);
        kfree(interp_buf);
        return -1;
      }
      ba->entry = interp_entry;
      ba->pml4 = user_pml4;
      ba->stack_top = user_rsp;

      task_t *t = sched_spawn(name, elf_user_entry_ldso, ba, 2);
      if (!t) {
        kfree(ba);
        vmm_destroy_user_pml4(user_pml4);
        kfree(interp_buf);
        return -1;
      }
      t->pml4 = user_pml4;
      t->brk_start = elf_align_up(max_main, PAGE_SIZE) + PAGE_SIZE;
      t->brk_current = t->brk_start;

      kprintf_color(0xFF00FF88,
                    "[ELF] User process '%s' spawned via ld.so (id=%lu "
                    "interp_entry=0x%lx)\n",
                    name, t->id, interp_entry);
      kfree(interp_buf);
      return (i64)t->id;
    }

    /* ---- Create isolated user address space ---- */
    u64 *user_pml4 = vmm_create_user_pml4();
    if (!user_pml4) {
        kprintf_color(0xFFFF3333, "[ELF] Failed to create user PML4\n");
        return -1;
    }

    /* ---- Load PT_LOAD segments ---- */
    if (!elf_map_pt_loads(user_pml4, ehdr, base, 0)) {
        kprintf_color(0xFFFF3333, "[ELF] Failed to map ELF segments\n");
        vmm_destroy_user_pml4(user_pml4);
        return -1;
    }

    u64 load_base = elf_min_load_vaddr(ehdr, base);
    if (!elf_apply_relocations(user_pml4, ehdr, base, file_size, load_base, 0)) {
      kprintf_color(0xFFFF3333, "[ELF] relocation failed\n");
      vmm_destroy_user_pml4(user_pml4);
      return -1;
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
    kprintf("[ELF] handoff argc=%lu argv=0x%lx envp=0x%lx rsp=0x%lx\n",
            ba->argc, ba->argv, ba->envp, ba->stack_top);

    task_t *t = sched_spawn(name, elf_user_entry, ba, 2);
    if (!t) {
        kfree(ba);
        vmm_destroy_user_pml4(user_pml4);
        return -1;
    }
    t->pml4 = user_pml4;
    t->brk_start = elf_align_up(elf_max_load_end(ehdr, base, 0), PAGE_SIZE) +
                   PAGE_SIZE;
    t->brk_current = t->brk_start;

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

/*
 * YamOS native executables use the standard ELF64 LE header (\x7FELF). The optional `.yam`
 * suffix is convention only — same bytes as `.elf`; spawn probes magic, not the extension.
 */
static bool name_already_yam_suffixed(const char *name) {
    usize n = strlen(name);
    return n > 4 && strcmp(name + n - 4, ".yam") == 0;
}

static bool try_elf_candidate_path(const char *path, char *out, usize out_cap) {
    if (!elf_path_has_magic(path)) return false;
    strncpy(out, path, out_cap - 1);
    out[out_cap - 1] = 0;
    return true;
}

static bool elf_resolve_executable(const char *name, char *out, usize out_cap) {
    static const char *search_dirs[] = {
        "/bin",
        "/usr/local/bin",
        "/opt/yamos/apps",
        "/opt/yamos/packages",
        "/home/root/bin",
    };
    if (!name || !*name || !out || out_cap == 0) return false;
    out[0] = 0;

    if (path_has_slash(name))
        return try_elf_candidate_path(name, out, out_cap);

    for (u32 i = 0; i < sizeof(search_dirs) / sizeof(search_dirs[0]); i++) {
        char candidate[256];
        ksnprintf(candidate, sizeof(candidate), "%s/%s", search_dirs[i], name);
        if (try_elf_candidate_path(candidate, out, out_cap))
            return true;

        if (!name_already_yam_suffixed(name)) {
            ksnprintf(candidate, sizeof(candidate), "%s/%s.yam", search_dirs[i], name);
            if (try_elf_candidate_path(candidate, out, out_cap))
                return true;
        }
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

i64 elf_exec_resolved_argv_envp(const char *name, int argc,
                                const char *const argv[],
                                const char *const envp[]) {
    char resolved[256];
    if (!elf_resolve_executable(name, resolved, sizeof(resolved))) {
        kprintf("[ELF] exec resolve executable '%s' failed\n",
                name ? name : "(null)");
        return -1;
    }

    const char *exec_argv[17];
    if (argc <= 0 || !argv) {
        exec_argv[0] = resolved;
        exec_argv[1] = NULL;
        argc = 1;
    } else {
        int capped = argc > 16 ? 16 : argc;
        exec_argv[0] = resolved;
        for (int i = 1; i < capped; i++) exec_argv[i] = argv[i];
        exec_argv[capped] = NULL;
        argc = capped;
    }

    usize size = 0;
    u8 *buf = elf_read_whole_file(resolved, &size);
    if (!buf) return -1;

    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)(void *)buf;
    const bool has_interp =
        size >= sizeof(*ehdr) && ehdr->e_magic == ELF_MAGIC &&
        ehdr->e_class == 2 && ehdr->e_phentsize == sizeof(elf64_phdr_t) &&
        ehdr->e_phoff + (u64)ehdr->e_phnum * ehdr->e_phentsize <= size &&
        elf_find_phdr(ehdr, buf, PT_INTERP) != NULL;

    elf_exec_image_t img;
    i64 rc = has_interp
                 ? elf_prepare_dynamic_image(buf, size, elf_basename(resolved),
                                             argc, exec_argv, envp, resolved,
                                             &img)
                 : elf_prepare_static_image(buf, size, elf_basename(resolved),
                                            argc, exec_argv, envp, &img);
    if (rc < 0) {
        kfree(buf);
        return rc;
    }

    task_t *cur = sched_current();
    if (!cur) {
        vmm_destroy_user_pml4(img.pml4);
        kfree(buf);
        return -1;
    }

    if (cur->thread_group && cur->thread_group != cur->id) {
        kprintf_color(0xFFFFAA33,
                      "[ELF] exec blocked from non-leader thread pid=%lu tgid=%lu\n",
                      cur->id, cur->thread_group);
        vmm_destroy_user_pml4(img.pml4);
        kfree(buf);
        return -38;
    }

    u64 *old_pml4 = cur->pml4;
    cur->pml4 = img.pml4;
    vmm_destroy_task_vmas(cur);
    cur->brk_start = img.brk_start;
    cur->brk_current = img.brk_start;
    cur->signal_pending = 0;
    cur->sig_mask = 0;
    for (u32 i = 0; i < 32; i++) cur->sig_handlers[i] = NULL;
    cur->tls_base = 0;
    cur->thread_group = 0;
    memset(cur->name, 0, sizeof(cur->name));
    strncpy(cur->name, elf_basename(resolved), sizeof(cur->name) - 1);

    u64 phys = vmm_virt_hhdm_to_phys(img.pml4);
    __asm__ volatile("mov %0, %%cr3" :: "r"(phys) : "memory");
    if (old_pml4 && old_pml4 != img.pml4) {
        vmm_destroy_user_pml4(old_pml4);
    }

    kprintf_color(0xFF00FF88,
                  "[ELF] execve '%s' pid=%lu entry=0x%lx rsp=0x%lx argc=%lu\n",
                  resolved, cur->id, img.entry, img.rsp, img.argc);
    kfree(buf);
    if (img.ldso_entry)
        enter_user_mode_ldso(img.entry, img.rsp);
    enter_user_mode(img.entry, img.rsp, img.argc, img.argv, img.envp);
}

i64 elf_spawn_path_argv(const char *path, int argc, const char *const argv[]) {
    return elf_spawn_path_argv_envp(path, argc, argv, NULL);
}

i64 elf_spawn_path(const char *path) {
    return elf_spawn_path_argv(path, 0, NULL);
}
