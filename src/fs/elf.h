/* ============================================================================
 * YamKernel — ELF Binary Formats
 * ============================================================================ */
#ifndef _FS_ELF_H
#define _FS_ELF_H

#include <nexus/types.h>

#define ELF_MAGIC 0x464C457F /* '\x7F', 'E', 'L', 'F' */

typedef struct {
    u32 e_magic;        /* 0x7F 'E' 'L' 'F' */
    u8  e_class;        /* 2 = 64-bit */
    u8  e_data;         /* 1 = little endian */
    u8  e_version;      /* 1 = original ELF */
    u8  e_osabi;
    u8  e_abiversion;
    u8  e_pad[7];
    u16 e_type;         /* 2 = ET_EXEC */
    u16 e_machine;      /* 0x3E = x86-64 */
    u32 e_version2;
    u64 e_entry;        /* Entry point virtual address */
    u64 e_phoff;        /* Program header table file offset */
    u64 e_shoff;        /* Section header table file offset */
    u32 e_flags;
    u16 e_ehsize;       /* ELF header size in bytes */
    u16 e_phentsize;    /* Program header table entry size */
    u16 e_phnum;        /* Program header table entry count */
    u16 e_shentsize;    /* Section header table entry size */
    u16 e_shnum;        /* Section header table entry count */
    u16 e_shstrndx;     /* Section header string table index */
} __attribute__((packed)) elf64_ehdr_t;

#define PT_NULL     0
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3
#define PT_NOTE     4
#define PT_SHLIB    5
#define PT_PHDR     6

#define PF_X        1
#define PF_W        2
#define PF_R        4

/* Auxiliary Vector Types */
#define AT_NULL     0
#define AT_IGNORE   1
#define AT_EXECFD   2
#define AT_PHDR     3
#define AT_PHENT    4
#define AT_PHNUM    5
#define AT_PAGESZ   6
#define AT_BASE     7
#define AT_FLAGS    8
#define AT_ENTRY    9
#define AT_NOTELF   10
#define AT_UID      11
#define AT_EUID     12
#define AT_GID      13
#define AT_EGID     14
#define AT_PLATFORM 15
#define AT_HWCAP    16
#define AT_CLKTCK   17
#define AT_SECURE   23
#define AT_BASE_PLATFORM 24
#define AT_RANDOM   25
#define AT_HWCAP2   26
#define AT_EXECFN   31

typedef struct {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;       /* Segment file offset */
    u64 p_vaddr;        /* Segment virtual address */
    u64 p_paddr;        /* Segment physical address */
    u64 p_filesz;       /* Segment size in file */
    u64 p_memsz;        /* Segment size in memory */
    u64 p_align;        /* Segment alignment */
} __attribute__((packed)) elf64_phdr_t;

/* elf.c */
struct task;
bool elf_load(const void *file_data, usize file_size, const char *name);
i64  elf_spawn(const void *file_data, usize file_size, const char *name);
i64  elf_spawn_path(const char *path);
i64  elf_spawn_argv(const void *file_data, usize file_size, const char *name,
                    int argc, const char *const argv[]);
i64  elf_spawn_argv_envp(const void *file_data, usize file_size, const char *name,
                         int argc, const char *const argv[],
                         const char *const envp[]);
i64  elf_spawn_path_argv(const char *path, int argc, const char *const argv[]);
i64  elf_spawn_path_argv_envp(const char *path, int argc,
                              const char *const argv[],
                              const char *const envp[]);
i64  elf_spawn_resolved_argv_envp(const char *name, int argc,
                                  const char *const argv[],
                                  const char *const envp[]);

#endif /* _FS_ELF_H */
