#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "fcntl.h"
#include "sys/stat.h"
#include "unistd.h"
#include "../libyam/app.h"
#include "pthread.h"

static int ptr_is_user_cstr(const char *p) {
    u64 v = (u64)p;
    return p && v >= 0x1000ULL && v < 0x0000800000000000ULL;
}

static void *thread_worker(void *arg) {
    long id = (long)arg;
    printf("[HELLO] Thread %ld running! pid=%ld pthread_self=%ld\n", id, (long)getpid(), (long)pthread_self());
    return (void *)(id * 2);
}

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
    static const char msg[] = "[HELLO] userspace bootstrap alive\n";
    syscall3(SYS_WRITE, 1, (u64)msg, sizeof(msg) - 1);
    return 0;
}
