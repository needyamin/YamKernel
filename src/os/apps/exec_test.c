#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "../libyam/syscall.h"

int main(int argc, char **argv, char **envp);

void _start(int argc, char **argv, char **envp) {
    int rc = main(argc, argv, envp);
    syscall1(SYS_EXIT, (u64)rc);
    for (;;) {
        syscall0(SYS_YIELD);
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    static char *const next_argv[] = {
        "/bin/hello",
        "from-execve",
        NULL,
    };
    static char *const next_envp[] = {
        "YAMOS_EXEC_TEST=1",
        NULL,
    };

    int fd = open("/bin/hello", O_RDONLY);
    if (fd >= 0) {
        if (fcntl(fd, F_SETFD, FD_CLOEXEC) == 0) {
            int flags = fcntl(fd, F_GETFD);
            printf("[EXEC_TEST] fd %d flags before exec=0x%x\n", fd, flags);
        } else {
            printf("[EXEC_TEST] F_SETFD failed on fd %d\n", fd);
        }
    } else {
        printf("[EXEC_TEST] open('/bin/hello') failed; continuing exec test\n");
    }

    printf("[EXEC_TEST] calling execve('/bin/hello')\n");
    execve("/bin/hello", next_argv, next_envp);
    printf("[EXEC_TEST] execve failed\n");
    return 127;
}
