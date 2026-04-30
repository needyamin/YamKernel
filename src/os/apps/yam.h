#ifndef _USER_YAM_H
#define _USER_YAM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "kernel/api/syscall.h"

/* Types are inherited from nexus/types.h via syscall.h */
typedef struct {
    u32 type;
    u16 code;
    i32 value;
} input_event_t;

/* Input types */
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03
#define KEY_PRESSED  1
#define KEY_RELEASED 0

/* ---- Library Functions ---- */

static inline long syscall0(long nr) {
    long ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(nr) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall1(long nr, long a1) {
    long ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(nr), "D"(a1) : "rcx", "r11", "memory" );
    return ret;
}

static inline long syscall2(long nr, long a1, long a2) {
    long ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2) : "rcx", "r11", "memory" );
    return ret;
}

static inline long syscall3(long nr, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory" );
    return ret;
}

static inline long syscall4(long nr, long a1, long a2, long a3, long a4) {
    long ret;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10) : "rcx", "r11", "memory" );
    return ret;
}

static inline long syscall5(long nr, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8) : "rcx", "r11", "memory" );
    return ret;
}

/* API Wrappers */
static inline void exit(int code) { syscall1(SYS_EXIT, code); }
static inline void yield(void) { syscall0(SYS_YIELD); }
static inline void sleep_ms(u32 ms) { syscall1(SYS_SLEEPMS, ms); }
static inline int getrusage(yam_rusage_t *out) { return (int)syscall1(SYS_GETRUSAGE, (long)out); }
static inline int sched_setaffinity(u64 mask) { return (int)syscall1(SYS_SCHED_SETAFFINITY, (long)mask); }
static inline u64 sched_getaffinity(void) { return (u64)syscall0(SYS_SCHED_GETAFFINITY); }
static inline int sched_info(yam_sched_info_t *out) { return (int)syscall1(SYS_SCHED_INFO, (long)out); }

static inline i32 wl_create_surface(const char *title, i32 x, i32 y, u32 w, u32 h) {
    return (i32)syscall5(SYS_WL_CREATE_SURFACE, (long)title, x, y, w, h);
}

static inline int wl_map_buffer(u32 surface_id, void *vaddr) {
    return (int)syscall2(SYS_WL_MAP_BUFFER, surface_id, (long)vaddr);
}

static inline int wl_commit(u32 surface_id) {
    return (int)syscall1(SYS_WL_COMMIT, surface_id);
}

static inline bool wl_poll_event(u32 surface_id, input_event_t *ev) {
    return syscall2(SYS_WL_POLL_EVENT, surface_id, (long)ev) > 0;
}

/* String basics */
static inline size_t strlen(const char *s) {
    size_t len = 0; while (s[len]) len++; return len;
}

static inline int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static inline char *strcpy(char *dst, const char *src) {
    char *ret = dst; while ((*dst++ = *src++)); return ret;
}

static inline char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack, *n = needle;
            while (*h && *n && *h == *n) { h++; n++; }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

static inline void print(const char *s) {
    syscall3(SYS_WRITE, 1, (long)s, strlen(s));
}

#endif
