/* ============================================================================
 * YamKernel — Kernel Panic Header
 * ============================================================================ */

#ifndef _NEXUS_PANIC_H
#define _NEXUS_PANIC_H

#include <nexus/types.h>

/* Halt the system with an error message */
NORETURN void kpanic(const char *fmt, ...);

struct interrupt_frame;
NORETURN void kpanic_with_frame(const struct interrupt_frame *frame, const char *fmt, ...);

/* Assert macro */
#define KASSERT(cond, msg) \
    do { if (!(cond)) kpanic("ASSERT FAILED: %s\n  %s:%d", msg, __FILE__, __LINE__); } while(0)

#endif /* _NEXUS_PANIC_H */
