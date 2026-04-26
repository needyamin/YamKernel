/* ============================================================================
 * YamKernel — String Library Implementation
 * Freestanding — no libc dependency
 * ============================================================================ */

#include "string.h"

void *memset(void *dest, int val, usize count) {
    __asm__ volatile (
        "rep stosb"
        : "+D"(dest), "+c"(count)
        : "a"(val)
        : "memory"
    );
    return dest;
}

void *memcpy(void *dest, const void *src, usize count) {
    void *d = dest;
    __asm__ volatile (
        "rep movsb"
        : "+D"(d), "+S"(src), "+c"(count)
        :
        : "memory"
    );
    return dest;
}

void *memmove(void *dest, const void *src, usize count) {
    u8 *d = (u8 *)dest;
    const u8 *s = (const u8 *)src;
    if (d < s) {
        for (usize i = 0; i < count; i++)
            d[i] = s[i];
    } else {
        for (usize i = count; i > 0; i--)
            d[i - 1] = s[i - 1];
    }
    return dest;
}

int memcmp(const void *a, const void *b, usize count) {
    const u8 *pa = (const u8 *)a;
    const u8 *pb = (const u8 *)b;
    for (usize i = 0; i < count; i++) {
        if (pa[i] != pb[i])
            return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

usize strlen(const char *s) {
    usize len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(u8)*a - (int)(u8)*b;
}

int strncmp(const char *a, const char *b, usize n) {
    for (usize i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(u8)a[i] - (int)(u8)b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, usize n) {
    usize i;
    for (i = 0; i < n && src[i]; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = 0;
    return dest;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && *h == *n) {
                h++;
                n++;
            }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}
