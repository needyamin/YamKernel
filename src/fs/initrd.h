/* ============================================================================
 * YamKernel — In-Memory Initial RAM Disk (initrd)
 * A flat table of {name, data, size} embedded in the kernel for early boot.
 * ============================================================================ */
#pragma once
#include <nexus/types.h>

#define INITRD_MAX_FILES  64
#define INITRD_NAME_MAX   128

typedef struct {
    char   name[INITRD_NAME_MAX]; /* Full path, e.g. "/etc/passwd" */
    u8    *data;
    usize  size;
    bool   is_dir;
} initrd_entry_t;

/* Initialize the initrd with built-in files */
void   initrd_init(void);

/* Look up a file by path. Returns pointer to data and sets *size_out.
 * Returns NULL if not found. */
void  *initrd_lookup(const char *path, usize *size_out);

/* Check if a path is a directory in the initrd */
bool   initrd_is_dir(const char *path);

/* List directory entries — fills out[] with up to max entries, returns count */
int    initrd_readdir(const char *dirpath, initrd_entry_t *out, int max);

/* Register an initrd entry (used during kernel init to add embedded files) */
void   initrd_register(const char *path, void *data, usize size, bool is_dir);
