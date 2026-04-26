/* YamKernel — SYSCALL setup + C-side dispatcher */
#include "syscall.h"
#include "msr.h"
#include "percpu.h"
#include "../sched/sched.h"
#include "../sched/wait.h"
#include "../lib/kprintf.h"
#include "../drivers/serial/serial.h"

extern void syscall_entry(void);   /* in syscall.asm */

static u8 syscall_stack[16384] ALIGNED(16);

void syscall_init(void) {
    /* Per-CPU syscall stack: where SYSCALL entry switches to */
    percpu_set_kernel_rsp((u64)&syscall_stack[sizeof(syscall_stack)]);

    /* EFER.SCE = 1 — enable SYSCALL */
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);

    /* STAR: bits 32-47 = kernel CS for SYSCALL  (kernel CS=0x08, SS=+8=0x10)
     *       bits 48-63 = base for SYSRET selectors. With base=0x10:
     *           SYSRET CS = 0x10+16 | 3 = 0x23 (user code, GDT idx 4)
     *           SYSRET SS = 0x10+8  | 3 = 0x1B (user data, GDT idx 3) */
    wrmsr(MSR_STAR, ((u64)0x10 << 48) | ((u64)0x08 << 32));

    /* LSTAR: 64-bit syscall entry RIP */
    wrmsr(MSR_LSTAR, (u64)syscall_entry);

    /* SFMASK: which RFLAGS bits to clear on SYSCALL.
     * Clear IF (0x200) so syscall runs with interrupts disabled until we want them,
     * also clear DF (0x400) for predictable string ops, TF (0x100) for no debug step. */
    wrmsr(MSR_SFMASK, 0x700);

    kprintf_color(0xFF00FF88, "[SYSCALL] entry=%p  STAR=0x%lx\n",
                  (void *)syscall_entry, rdmsr(MSR_STAR));
}

/* ---- Individual syscall handlers ---- */

static i64 sys_write(u64 fd, u64 ubuf, u64 len) {
    (void)fd;
    bool smap = (read_cr4() & CR4_SMAP) != 0;
    if (smap) __asm__ volatile ("stac");
    const char *p = (const char *)ubuf;
    for (u64 i = 0; i < len; i++) serial_putchar(p[i]);
    if (smap) __asm__ volatile ("clac");
    return (i64)len;
}

static i64 sys_getpid(void) {
    return (i64)this_cpu()->current->id;
}

static i64 sys_yield(void) { sched_yield(); return 0; }
static i64 sys_sleepms(u64 ms) { task_sleep_ms(ms); return 0; }

static i64 sys_exit(u64 code) {
    task_t *t = this_cpu()->current;
    t->state = TASK_DEAD;
    kprintf_color(0xFFFF8833, "[SYS] task '%s' exit(%lu)\n", t->name, code);
    sched_yield();
    for (;;) hlt();   /* should never return */
    return 0;
}

i64 syscall_dispatch(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
    (void)a4; (void)a5;
    switch (nr) {
    case SYS_WRITE:  return sys_write(a1, a2, a3);
    case SYS_EXIT:   return sys_exit(a1);
    case SYS_GETPID: return sys_getpid();
    case SYS_YIELD:  return sys_yield();
    case SYS_SLEEPMS:return sys_sleepms(a1);
    default: return -1;
    }
}
