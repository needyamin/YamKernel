/* ============================================================================
 * YamKernel — POSIX libc: stdlib (User-Space) — Extended
 * malloc, free, calloc, realloc, atoi, strtol, qsort, exit
 * ============================================================================ */
#include "stdlib.h"
#include "string.h"
#include "../libyam/syscall.h"

extern int errno;

/* ---- Memory allocation (uses SYS_MMAP syscall) ---- */

/* Simple bump allocator backed by SYS_BRK/SYS_MMAP */
#define HEAP_SIZE  (1024 * 1024 * 4)  /* 4 MB user heap */

static u8  g_heap[HEAP_SIZE];
static usize g_heap_top = 0;

typedef struct chunk {
    usize         size;
    bool          free;
    struct chunk *next;
} chunk_t;

static chunk_t *g_first_chunk = NULL;

static void heap_init(void) {
    if (g_first_chunk) return;
    g_first_chunk = (chunk_t *)g_heap;
    g_first_chunk->size = HEAP_SIZE - sizeof(chunk_t);
    g_first_chunk->free = true;
    g_first_chunk->next = NULL;
    g_heap_top = HEAP_SIZE;
}

void *malloc(usize size) {
    heap_init();
    if (!size) return NULL;
    /* Align to 16 bytes */
    size = (size + 15) & ~15;

    chunk_t *c = g_first_chunk;
    while (c) {
        if (c->free && c->size >= size) {
            /* Split if large enough */
            if (c->size >= size + sizeof(chunk_t) + 16) {
                chunk_t *next = (chunk_t *)((u8 *)c + sizeof(chunk_t) + size);
                next->size = c->size - size - sizeof(chunk_t);
                next->free = true;
                next->next = c->next;
                c->next = next;
                c->size = size;
            }
            c->free = false;
            return (u8 *)c + sizeof(chunk_t);
        }
        c = c->next;
    }
    return NULL; /* OOM */
}

void free(void *ptr) {
    if (!ptr) return;
    chunk_t *c = (chunk_t *)((u8 *)ptr - sizeof(chunk_t));
    c->free = true;
    /* Merge with next if free */
    if (c->next && c->next->free) {
        c->size += sizeof(chunk_t) + c->next->size;
        c->next = c->next->next;
    }
}

void *calloc(usize n, usize size) {
    usize total = n * size;
    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, usize new_size) {
    if (!ptr) return malloc(new_size);
    if (!new_size) { free(ptr); return NULL; }
    chunk_t *c = (chunk_t *)((u8 *)ptr - sizeof(chunk_t));
    if (c->size >= new_size) return ptr;
    void *new_ptr = malloc(new_size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, c->size);
    free(ptr);
    return new_ptr;
}

/* ---- String conversions ---- */

int atoi(const char *s) {
    if (!s) return 0;
    int n = 0, sign = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return sign * n;
}

long atol(const char *s) {
    if (!s) return 0;
    long n = 0; int sign = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return sign * n;
}

long strtol(const char *s, char **end, int base) {
    while (*s == ' ') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    if (base == 0) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (*s == '0') { base = 8; s++; }
        else base = 10;
    }
    long n = 0;
    while (1) {
        int d;
        char c = *s;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        n = n * base + d; s++;
    }
    if (end) *end = (char *)s;
    return sign * n;
}

unsigned long strtoul(const char *s, char **end, int base) {
    return (unsigned long)strtol(s, end, base);
}

/* ---- qsort (non-recursive introsort simplified as insertion sort for small N) ---- */

void qsort(void *base, usize nmemb, usize size,
           int (*compar)(const void *, const void *)) {
    if (nmemb <= 1) return;
    u8 *arr = (u8 *)base;
    u8 *tmp = (u8 *)malloc(size);
    if (!tmp) return;

    /* Insertion sort (simple, correct) */
    for (usize i = 1; i < nmemb; i++) {
        memcpy(tmp, arr + i * size, size);
        intptr_t j = (intptr_t)i - 1;
        while (j >= 0 && compar(arr + j * size, tmp) > 0) {
            memcpy(arr + (j + 1) * size, arr + j * size, size);
            j--;
        }
        memcpy(arr + (j + 1) * size, tmp, size);
    }
    free(tmp);
}

void *bsearch(const void *key, const void *base, usize nmemb, usize size,
              int (*compar)(const void *, const void *)) {
    usize lo = 0, hi = nmemb;
    const u8 *arr = (const u8 *)base;
    while (lo < hi) {
        usize mid = lo + (hi - lo) / 2;
        int cmp = compar(key, arr + mid * size);
        if (cmp == 0) return (void *)(arr + mid * size);
        if (cmp < 0) hi = mid; else lo = mid + 1;
    }
    return NULL;
}

int abs(int n)    { return n < 0 ? -n : n; }
long labs(long n) { return n < 0 ? -n : n; }

void exit(int status) {
    syscall1(SYS_EXIT, (u64)status);
    __builtin_unreachable();
}
void abort(void) { exit(-1); }
