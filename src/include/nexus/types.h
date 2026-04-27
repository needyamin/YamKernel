/* ============================================================================
 * YamKernel — Core Type Definitions
 * ============================================================================ */

#ifndef _NEXUS_TYPES_H
#define _NEXUS_TYPES_H

/* ---- Fixed-width integer types ---- */
typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;

typedef signed char         i8;
typedef signed short        i16;
typedef signed int          i32;
typedef signed long long    i64;

typedef u64                 usize;
typedef i64                 isize;

/* ---- Boolean ---- */
#ifndef bool
typedef _Bool               bool;
#define true                1
#define false               0
#endif

/* ---- NULL ---- */
#ifndef NULL
#define NULL                ((void*)0)
#endif

/* ---- Attributes ---- */
#define PACKED              __attribute__((packed))
#define ALIGNED(n)          __attribute__((aligned(n)))
#define NORETURN            __attribute__((noreturn))
#define UNUSED              __attribute__((unused))
#define ALWAYS_INLINE       __attribute__((always_inline)) static inline

/* ---- YamKernel-specific types ---- */

/* Unique ID for graph nodes */
typedef u64 yam_node_id_t;

/* Unique ID for graph edges */
typedef u64 yam_edge_id_t;

/* Capability permission bitmask */
typedef u32 yam_perm_t;

/* Permission flags */
#define YAM_PERM_READ       (1 << 0)
#define YAM_PERM_WRITE      (1 << 1)
#define YAM_PERM_EXEC       (1 << 2)
#define YAM_PERM_DELEGATE   (1 << 3)   /* Can pass capability to others */
#define YAM_PERM_REVOKE     (1 << 4)   /* Can revoke downstream caps */
#define YAM_PERM_CREATE     (1 << 5)   /* Can create child nodes */
#define YAM_PERM_DESTROY    (1 << 6)   /* Can destroy nodes */
#define YAM_PERM_ALL        (0x7F)

/* Node types in the YamGraph */
typedef enum {
    YAM_NODE_TASK       = 0,    /* Process/thread */
    YAM_NODE_MEMORY     = 1,    /* Memory cell */
    YAM_NODE_DEVICE     = 2,    /* Hardware device */
    YAM_NODE_FILE       = 3,    /* File/data */
    YAM_NODE_CHANNEL    = 4,    /* IPC channel */
    YAM_NODE_IRQ        = 5,    /* Interrupt handler */
    YAM_NODE_NAMESPACE  = 6,    /* Resource namespace (like a directory) */
    YAM_NODE_MAX
} yam_node_type_t;

/* Edge types in the YamGraph */
typedef enum {
    YAM_EDGE_OWNS       = 0,    /* Ownership (parent→child) */
    YAM_EDGE_CAPABILITY = 1,    /* Capability grant */
    YAM_EDGE_CHANNEL    = 2,    /* IPC channel link */
    YAM_EDGE_MAPS       = 3,    /* Memory mapping */
    YAM_EDGE_DEPENDS    = 4,    /* Dependency */
    YAM_EDGE_MAX
} yam_edge_type_t;

/* ---- Inline helpers (Kernel Only) ---- */
#ifdef YAM_KERNEL
ALWAYS_INLINE void outb(u16 port, u8 val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

ALWAYS_INLINE u8 inb(u16 port) {
    u8 val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

ALWAYS_INLINE void outw(u16 port, u16 val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

ALWAYS_INLINE u16 inw(u16 port) {
    u16 val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

ALWAYS_INLINE void outl(u16 port, u32 val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

ALWAYS_INLINE u32 inl(u16 port) {
    u32 val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

ALWAYS_INLINE void io_wait(void) {
    outb(0x80, 0);
}

ALWAYS_INLINE void cli(void) {
    __asm__ volatile ("cli");
}

ALWAYS_INLINE void sti(void) {
    __asm__ volatile ("sti");
}

ALWAYS_INLINE void hlt(void) {
    __asm__ volatile ("hlt");
}
#endif

#endif /* _NEXUS_TYPES_H */
