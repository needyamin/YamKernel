/* ============================================================================
 * YamKernel — Test User-Space ELF Application
 * A minimal program that writes a string via syscall and exits.
 * Compiled separately and embedded as a Limine boot module.
 * ============================================================================ */

/* Syscall numbers must match the kernel's syscall.h */
#define SYS_WRITE   1
#define SYS_EXIT    60

/* Inline syscall wrappers (uses SYSCALL instruction directly) */
static long syscall1(long nr, long a1) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static long syscall3(long nr, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static void write_serial(const char *msg) {
    unsigned long len = 0;
    while (msg[len]) len++;
    syscall3(SYS_WRITE, 1 /* stdout */, (long)msg, (long)len);
}

void _start(void) {
    write_serial("[USER-ELF] Hello from Ring 3!\n");
    write_serial("[USER-ELF] ELF loader is working correctly.\n");
    write_serial("[USER-ELF] Exiting with code 0.\n");
    syscall1(SYS_EXIT, 0);
    
    /* Should never reach here */
    for (;;) {}
}
