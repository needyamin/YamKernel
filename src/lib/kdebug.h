/* ============================================================================
 * YamKernel — Kernel Debug Logging Subsystem
 *
 * Structured debug output that ALWAYS goes to the serial console (COM1),
 * regardless of framebuffer state.  Use these macros instead of raw
 * serial_write() so every message has a level tag, module tag, and
 * consistent formatting.
 *
 * Compile-time verbosity: set KDEBUG_LEVEL before including this header.
 *   0 = TRACE  (extremely verbose — every function entry/exit)
 *   1 = DEBUG  (detailed internal state)
 *   2 = INFO   (normal boot progress — default)
 *   3 = WARN   (recoverable issues)
 *   4 = ERROR  (fatal / critical)
 *
 * Example:
 *   KINFO("SPLASH", "wallpaper ptr=%p  logo ptr=%p", wp, lp);
 *   KWARN("MODULE", "no modules loaded by bootloader!");
 * ============================================================================ */

#ifndef _KDEBUG_H
#define _KDEBUG_H

#include <nexus/types.h>

/* Log levels */
#define KDEBUG_TRACE  0
#define KDEBUG_DEBUG  1
#define KDEBUG_INFO   2
#define KDEBUG_WARN   3
#define KDEBUG_ERROR  4

#ifndef KDEBUG_LEVEL
#define KDEBUG_LEVEL  KDEBUG_TRACE   /* Show everything by default */
#endif

/* Core logging function (implemented in kdebug.c) */
void kdebug_log(int level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* Convenience macros — compile out messages below KDEBUG_LEVEL */
#define KTRACE(tag, fmt, ...) do { \
    if (KDEBUG_TRACE >= KDEBUG_LEVEL) \
        kdebug_log(KDEBUG_TRACE, tag, fmt, ##__VA_ARGS__); \
} while(0)

#define KDBG(tag, fmt, ...) do { \
    if (KDEBUG_DEBUG >= KDEBUG_LEVEL) \
        kdebug_log(KDEBUG_DEBUG, tag, fmt, ##__VA_ARGS__); \
} while(0)

#define KINFO(tag, fmt, ...) do { \
    if (KDEBUG_INFO >= KDEBUG_LEVEL) \
        kdebug_log(KDEBUG_INFO, tag, fmt, ##__VA_ARGS__); \
} while(0)

#define KWARN(tag, fmt, ...) do { \
    if (KDEBUG_WARN >= KDEBUG_LEVEL) \
        kdebug_log(KDEBUG_WARN, tag, fmt, ##__VA_ARGS__); \
} while(0)

#define KERR(tag, fmt, ...) do { \
    if (KDEBUG_ERROR >= KDEBUG_LEVEL) \
        kdebug_log(KDEBUG_ERROR, tag, fmt, ##__VA_ARGS__); \
} while(0)

/* Hex dump: prints `len` bytes at `addr` in classic hex-dump format */
void kdebug_hexdump(const char *tag, const void *addr, u32 len);

/* Assert macro — use KASSERT from <nexus/panic.h> */

#endif /* _KDEBUG_H */
