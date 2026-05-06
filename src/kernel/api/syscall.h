/* YamKernel — SYSCALL/SYSRET fast syscall path v0.3.0 */
#ifndef _CPU_SYSCALL_H
#define _CPU_SYSCALL_H

#include <nexus/types.h>

/* Syscall numbers */
#define SYS_WRITE   1
#define SYS_EXIT    2
#define SYS_GETPID  3
#define SYS_YIELD   4
#define SYS_SLEEPMS 5
#define SYS_OPEN    6
#define SYS_CLOSE   7
#define SYS_READ    8
#define SYS_WRITE_FD 9
#define SYS_MMAP    10
#define SYS_MUNMAP  11
#define SYS_PIPE    12
#define SYS_POLL    13

/* Process management (new in v0.3.0) */
#define SYS_FORK          14
#define SYS_WAITPID       15
#define SYS_BRK           16
#define SYS_MPROTECT      17
#define SYS_CLOCK_GETTIME 18
#define SYS_GETRUSAGE     19

/* Wayland / GUI Syscalls */
#define SYS_WL_CREATE_SURFACE 20
#define SYS_WL_MAP_BUFFER     21
#define SYS_WL_COMMIT         22
#define SYS_WL_POLL_EVENT     23
#define SYS_LSEEK             24
#define SYS_MKDIR             25
#define SYS_UNLINK            26
#define SYS_READDIR           27
#define SYS_CHDIR             28
#define SYS_GETCWD            29

/* Privileged Driver Syscalls (OS Level) */
#define SYS_IOPORT_READ       30
#define SYS_IOPORT_WRITE      31
#define SYS_IRQ_SUBSCRIBE     32
#define SYS_MAP_MMIO          33
#define SYS_PCI_CONFIG_READ   34

/* Scheduling (new in v0.3.0) */
#define SYS_SCHED_SETAFFINITY 40
#define SYS_SCHED_GETAFFINITY 41
#define SYS_FUTEX             42
#define SYS_KILL              43
#define SYS_GETPPID           44
#define SYS_DUP               45
#define SYS_DUP2              46
#define SYS_SCHED_INFO        47

/* AI/ML Acceleration (new in v0.3.0) */
#define SYS_AI_DEVICE_QUERY   50
#define SYS_AI_TENSOR_ALLOC   51
#define SYS_AI_TENSOR_FREE    52
#define SYS_AI_SUBMIT_JOB     53
#define SYS_AI_WAIT_JOB       54
#define SYS_AI_MAP_TENSOR     55

/* YamGraph IPC (new in v0.3.0) */
#define SYS_CHANNEL_SEND      60
#define SYS_CHANNEL_RECV      61
#define SYS_CHANNEL_LOOKUP    62

#define SYS_CLIPBOARD_SET     63
#define SYS_CLIPBOARD_GET     64
#define SYS_INSTALLER_STATUS  65
#define SYS_INSTALLER_REQUEST 66
#define SYS_OS_INFO           67
#define SYS_APP_REGISTER      68
#define SYS_APP_QUERY         69
#define SYS_SOCKET            70
#define SYS_BIND              71
#define SYS_CONNECT           72
#define SYS_LISTEN            73
#define SYS_ACCEPT            74
#define SYS_SENDTO            75
#define SYS_RECVFROM          76
#define SYS_SPAWN             77
#define SYS_STAT              78
#define SYS_FSTAT             79
#define SYS_FTRUNCATE         80
#define SYS_RENAME            81
#define SYS_OPENAT            82
#define SYS_FSTATAT           83
#define SYS_MKDIRAT           84
#define SYS_UNLINKAT          85
#define SYS_RENAMEAT          86

/* Kernel Threads & Signals (Phase 1) */
#define SYS_THREAD_CREATE     87
#define SYS_THREAD_EXIT       88
#define SYS_SIGACTION         89
#define SYS_SIGPROCMASK       90
#define SYS_SIGRETURN         91

/* Touchscreen (new in v0.3.0) */
#define SYS_TOUCH_CALIBRATE   56
#define SYS_TOUCH_GET_SLOTS   57
#define SYS_GESTURE_CONFIG    58

/* Phase 3: Non-blocking I/O */
#define SYS_SELECT            92
#define SYS_FCNTL             93

#define SYS_MAX     94

#define YAM_ABI_VERSION 1
#define YAM_OS_NAME "YamOS"
#define YAM_KERNEL_NAME "YamKernel"

#define YAM_OS_FLAG_PREEMPTIVE        (1u << 0)
#define YAM_OS_FLAG_GRAPH_IPC         (1u << 1)
#define YAM_OS_FLAG_GUI_COMPOSITOR    (1u << 2)
#define YAM_OS_FLAG_DRIVER_SYSCALLS   (1u << 3)
#define YAM_OS_FLAG_NETWORK_STACK     (1u << 4)
#define YAM_OS_FLAG_INSTALLER_SERVICE (1u << 5)
#define YAM_OS_FLAG_SOCKET_ABI        (1u << 6)
#define YAM_OS_FLAG_VFS_SPAWN         (1u << 7)
#define YAM_OS_FLAG_THREADS           (1u << 8)
#define YAM_OS_FLAG_NONBLOCK_IO       (1u << 9)

#define YAM_APP_TYPE_PROCESS  1
#define YAM_APP_TYPE_SERVICE  2
#define YAM_APP_TYPE_GUI      3
#define YAM_APP_TYPE_DRIVER   4
#define YAM_APP_TYPE_RUNTIME  5

#define YAM_APP_PERM_NONE     0
#define YAM_APP_PERM_FS       (1u << 0)
#define YAM_APP_PERM_NET      (1u << 1)
#define YAM_APP_PERM_IPC      (1u << 2)
#define YAM_APP_PERM_GUI      (1u << 3)
#define YAM_APP_PERM_DEVICE   (1u << 4)
#define YAM_APP_PERM_DRIVER   (1u << 5)
#define YAM_APP_PERM_INSTALL  (1u << 6)
#define YAM_APP_PERM_RUNTIME  (1u << 7)

#define YAM_INSTALL_STATE_EMPTY             0
#define YAM_INSTALL_STATE_AVAILABLE         1
#define YAM_INSTALL_STATE_BLOCKED           2
#define YAM_INSTALL_STATE_READY_TO_DOWNLOAD 3
#define YAM_INSTALL_STATE_DOWNLOADING       4
#define YAM_INSTALL_STATE_INSTALLING        5
#define YAM_INSTALL_STATE_INSTALLED         6
#define YAM_INSTALL_STATE_FAILED            7

#define YAM_INSTALL_ERR_NONE               0
#define YAM_INSTALL_ERR_UNKNOWN_PACKAGE    1
#define YAM_INSTALL_ERR_MISSING_CAPABILITY 2

#define YAM_INSTALL_CAP_PACKAGE_DB         (1u << 0)
#define YAM_INSTALL_CAP_TEMP_STORAGE       (1u << 1)
#define YAM_INSTALL_CAP_PERSISTENT_STORAGE (1u << 2)
#define YAM_INSTALL_CAP_NET_IFACE          (1u << 3)
#define YAM_INSTALL_CAP_DHCP               (1u << 4)
#define YAM_INSTALL_CAP_DNS                (1u << 5)
#define YAM_INSTALL_CAP_TCP_CONNECT        (1u << 6)
#define YAM_INSTALL_CAP_HTTP_CLIENT        (1u << 7)
#define YAM_INSTALL_CAP_HTTPS_TLS          (1u << 8)
#define YAM_INSTALL_CAP_CERT_STORE         (1u << 9)

typedef struct {
    u64 pid;
    u64 ppid;
    u64 ticks;
    u64 utime;
    u64 stime;
    u64 voluntary_switches;
    u64 involuntary_switches;
    u64 start_tick;
    u64 rss_pages;
    i32 nice;
    u32 cpu;
    u64 affinity;
} yam_rusage_t;

typedef struct {
    u32 detected_cpus;
    u32 schedulable_cpus;
    u32 total_tasks;
    u32 ready_tasks;
    u32 blocked_tasks;
    u32 running_tasks;
    u64 total_switches;
    u64 ticks;
    u64 rq_load[8];
    u32 rq_ready[8];
} yam_sched_info_t;

typedef struct {
    u32 state;
    u32 last_error;
    u32 capabilities;
    u32 missing;
    u32 progress;
    char package[32];
    char display_name[64];
    char official_url[160];
    char install_path[128];
    char message[160];
} yam_installer_status_t;

typedef struct {
    u32 abi_version;
    u32 syscall_max;
    u32 page_size;
    u32 pointer_bits;
    u32 cpu_count;
    u32 flags;
    char os_name[32];
    char kernel_name[32];
} yam_os_info_t;

typedef struct {
    u32 abi_version;
    u32 app_type;
    u32 permissions;
    u32 flags;
    char name[32];
    char publisher[32];
    char version[16];
    char description[96];
} yam_app_manifest_t;

typedef struct {
    u64 pid;
    u32 graph_node;
    u32 app_type;
    u32 permissions;
    u32 flags;
    char task_name[24];
    char name[32];
    char publisher[32];
    char version[16];
    char description[96];
} yam_app_record_t;

typedef struct {
    char name[192];
    u64 size;
    u32 is_dir;
} yam_dirent_t;

#ifndef YAM_SIGACTION_T_DEFINED
#define YAM_SIGACTION_T_DEFINED
typedef struct {
    u64 sa_handler;
    u64 sa_mask;
    u32 sa_flags;
    u64 sa_sigaction;
} yam_sigaction_t;
#endif

#define YAM_S_IFMT  0170000
#define YAM_S_IFDIR 0040000
#define YAM_S_IFCHR 0020000
#define YAM_S_IFREG 0100000
#define YAM_S_IRUSR 0400
#define YAM_S_IWUSR 0200
#define YAM_S_IXUSR 0100

#ifndef YAM_STAT_T_DEFINED
#define YAM_STAT_T_DEFINED
typedef struct {
    u64 dev;
    u64 ino;
    u32 mode;
    u32 nlink;
    u32 uid;
    u32 gid;
    u64 rdev;
    i64 size;
    i64 blksize;
    i64 blocks;
    i64 atime;
    i64 mtime;
    i64 ctime;
} yam_stat_t;
#endif

void syscall_init(void);
i64 syscall_dispatch(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5);

#endif
