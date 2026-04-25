/* ============================================================================
 * YamKernel — Kernel Printf
 * ============================================================================ */

#ifndef _LIB_KPRINTF_H
#define _LIB_KPRINTF_H

#include <nexus/types.h>

/* Printf to both serial and framebuffer */
void kprintf(const char *fmt, ...);

/* Printf with color (framebuffer only, serial gets plain text) */
void kprintf_color(u32 color, const char *fmt, ...);

/* Low-level: printf to a buffer */
int ksnprintf(char *buf, usize size, const char *fmt, ...);

#endif /* _LIB_KPRINTF_H */
