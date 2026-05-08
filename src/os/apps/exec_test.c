#include "stdio.h"
#include "unistd.h"
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

    printf("[EXEC_TEST] calling execve('/bin/hello')\n");
    execve("/bin/hello", next_argv, next_envp);
    printf("[EXEC_TEST] execve failed\n");
    return 127;
}
