#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "fcntl.h"
#include "sys/stat.h"
#include "unistd.h"
#include "../libyam/app.h"

int main(int argc, char **argv, char **envp);

void _start(int argc, char **argv, char **envp) {
    exit(main(argc, argv, envp));
}

int main(int argc, char **argv, char **envp) {
    yam_app_manifest_t manifest = YAM_APP_MANIFEST(
        "hello",
        YAM_APP_TYPE_PROCESS,
        YAM_APP_PERM_FS,
        "VFS-launched sample application"
    );
    yam_app_register(&manifest);

    yam_os_info_t info;
    if (yam_os_info(&info) == 0) {
        printf("[HELLO] Hello from /bin/hello on %s ABI=%u pid=%lu argc=%d\n",
               info.os_name, info.abi_version, yam_pid(), argc);
    } else {
        printf("[HELLO] Hello from /bin/hello pid=%lu argc=%d\n", yam_pid(), argc);
    }
    for (int i = 0; i < argc; i++) {
        printf("[HELLO] argv[%d]=%s\n", i, argv && argv[i] ? argv[i] : "(null)");
    }
    for (int i = 0; envp && envp[i] && i < 4; i++) {
        printf("[HELLO] envp[%d]=%s\n", i, envp[i]);
    }
    struct stat st;
    const char *self = (argc > 0 && argv && argv[0]) ? argv[0] : "/bin/hello";
    if (stat(self, &st) == 0) {
        printf("[HELLO] stat %s mode=0%o size=%ld\n", self, st.st_mode, (long)st.st_size);
    } else {
        printf("[HELLO] stat %s failed\n", self);
    }
    if (stat("/usr/local/bin/hello-local", &st) == 0) {
        printf("[HELLO] stat /usr/local/bin/hello-local mode=0%o size=%ld\n",
               st.st_mode, (long)st.st_size);
    }
    int fd = open(self, O_RDONLY);
    if (fd >= 0) {
        if (fstat(fd, &st) == 0) {
            printf("[HELLO] fstat %s mode=0%o size=%ld\n", self, st.st_mode, (long)st.st_size);
        }
        off_t end = lseek(fd, 0, SEEK_END);
        if (end >= 0) {
            printf("[HELLO] seek_end %s offset=%ld\n", self, (long)end);
        }
        close(fd);
    }
    int tfd = open("/tmp/hello-truncate.txt", O_CREAT | O_TRUNC | O_RDWR);
    if (tfd >= 0) {
        write(tfd, "abcdef", 6);
        if (fstat(tfd, &st) == 0) {
            printf("[HELLO] fstat /tmp/hello-truncate.txt before=%ld\n", (long)st.st_size);
        }
        if (ftruncate(tfd, 2) == 0 && fstat(tfd, &st) == 0) {
            printf("[HELLO] ftruncate /tmp/hello-truncate.txt size=%ld\n", (long)st.st_size);
        }
        close(tfd);
    }
    int rfd = open("/tmp/hello-rename.tmp", O_CREAT | O_TRUNC | O_RDWR);
    if (rfd >= 0) {
        write(rfd, "rename", 6);
        close(rfd);
        unlink("/tmp/hello-rename.txt");
        if (rename("/tmp/hello-rename.tmp", "/tmp/hello-rename.txt") == 0 &&
            stat("/tmp/hello-rename.txt", &st) == 0) {
            printf("[HELLO] rename /tmp/hello-rename.txt size=%ld\n", (long)st.st_size);
        }
    }
    int dfd = open("/tmp", O_RDONLY);
    if (dfd >= 0) {
        int afd = openat(dfd, "hello-at.tmp", O_CREAT | O_TRUNC | O_RDWR);
        if (afd >= 0) {
            write(afd, "at", 2);
            close(afd);
            if (fstatat(dfd, "hello-at.tmp", &st, 0) == 0) {
                printf("[HELLO] openat/fstatat /tmp/hello-at.tmp size=%ld\n", (long)st.st_size);
            }
            unlinkat(dfd, "hello-at.txt", 0);
            if (renameat(dfd, "hello-at.tmp", dfd, "hello-at.txt") == 0 &&
                fstatat(dfd, "hello-at.txt", &st, 0) == 0) {
                printf("[HELLO] renameat /tmp/hello-at.txt size=%ld\n", (long)st.st_size);
            }
        }
        close(dfd);
    }
    int apfd = open("/tmp/hello-append.txt", O_CREAT | O_TRUNC | O_RDWR);
    if (apfd >= 0) {
        write(apfd, "A", 1);
        close(apfd);
        apfd = open("/tmp/hello-append.txt", O_WRONLY | O_APPEND);
        if (apfd >= 0) {
            write(apfd, "B", 1);
            close(apfd);
        }
        if (stat("/tmp/hello-append.txt", &st) == 0) {
            printf("[HELLO] append /tmp/hello-append.txt size=%ld\n", (long)st.st_size);
        }
    }
    unlink("/tmp/hello-excl.txt");
    int xfd = open("/tmp/hello-excl.txt", O_CREAT | O_EXCL | O_RDWR);
    if (xfd >= 0) {
        close(xfd);
        int xfd2 = open("/tmp/hello-excl.txt", O_CREAT | O_EXCL | O_RDWR);
        if (xfd2 < 0) {
            printf("[HELLO] o_excl /tmp/hello-excl.txt second_open_failed\n");
        } else {
            close(xfd2);
        }
    }
    if (argc > 1 && argv && argv[1] && strcmp(argv[1], "--spawn-probe") == 0) {
        char *child_argv[] = { "hello", "--child", NULL };
        char *child_envp[] = { "YAMOS_PARENT=hello", "YAMOS_WAIT_PROBE=1", NULL };
        pid_t child = spawnve("hello", child_argv, child_envp);
        printf("[HELLO] spawn child -> pid=%ld\n", (long)child);
        if (child > 0) {
            int status = 0;
            pid_t waited = waitpid(child, &status, 0);
            printf("[HELLO] waitpid child -> pid=%ld status=0x%x exit=%d\n",
                   (long)waited, status, WEXITSTATUS(status));
        }
    }
    return 0;
}
