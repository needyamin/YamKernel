#include "stdlib.h"
#include "string.h"
#include "../libyam/syscall.h"

/* Very simple bump allocator using SYS_BRK for now */
static u64 current_brk = 0;

static u64 sys_brk(u64 inc) {
    return syscall1(SYS_BRK, inc);
}

void *malloc(usize size) {
    if (size == 0) return NULL;
    
    /* 16-byte alignment */
    size = (size + 15) & ~15;
    
    if (current_brk == 0) {
        current_brk = sys_brk(0);
    }
    
    u64 ret = sys_brk(size);
    if (ret == current_brk) {
        /* Failed to expand */
        return NULL;
    }
    
    current_brk = ret + size;
    return (void *)ret;
}

void free(void *ptr) {
    /* No-op in simple bump allocator */
    (void)ptr;
}

void *calloc(usize num, usize size) {
    usize total = num * size;
    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, usize size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }
    
    void *new_ptr = malloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, size); /* Might copy garbage, but it's a bump alloc */
        free(ptr);
    }
    return new_ptr;
}

void exit(int status) {
    syscall1(SYS_EXIT, (u64)status);
    while (1);
}
