/* ============================================================================
 * YamKernel — /proc Pseudo-filesystem (stubs)
 * Read-only runtime information exposed at /proc/
 * ============================================================================ */
#include "../../lib/kprintf.h"
#include "../../lib/string.h"
#include <nexus/types.h>

extern usize pmm_total_memory(void);
extern usize pmm_free_memory(void);
extern usize sched_task_count(void);
extern u32   smp_cpu_count(void);

static isize procfs_cpuinfo(void *buf, usize count) {
    char tmp[512];
    u32 ncpus = smp_cpu_count();
    int len = ksnprintf(tmp, sizeof(tmp),
        "processor\t: 0-%u\n"
        "vendor_id\t: YamKernel x86_64\n"
        "cpu cores\t: %u\n"
        "architecture\t: x86_64\n"
        "flags\t\t: sse sse2 nx lm\n",
        ncpus - 1, ncpus);
    if (len > (int)count) len = (int)count;
    memcpy(buf, tmp, (usize)len);
    return len;
}

static isize procfs_meminfo(void *buf, usize count) {
    char tmp[512];
    usize total_kb = pmm_total_memory() / 1024;
    usize free_kb  = pmm_free_memory()  / 1024;
    int len = ksnprintf(tmp, sizeof(tmp),
        "MemTotal:\t%lu kB\n"
        "MemFree:\t%lu kB\n"
        "MemAvailable:\t%lu kB\n"
        "Buffers:\t0 kB\n"
        "Cached:\t\t0 kB\n",
        total_kb, free_kb, free_kb);
    if (len > (int)count) len = (int)count;
    memcpy(buf, tmp, (usize)len);
    return len;
}

static isize procfs_version(void *buf, usize count) {
    const char *ver = "YamKernel v0.4.0 (Graph-Based Adaptive OS) #1 SMP\n";
    usize len = strlen(ver);
    if (len > count) len = count;
    memcpy(buf, ver, len);
    return (isize)len;
}

static isize procfs_uptime(void *buf, usize count) {
    char tmp[64];
    int len = ksnprintf(tmp, sizeof(tmp), "0.00 0.00\n");
    if (len > (int)count) len = (int)count;
    memcpy(buf, tmp, (usize)len);
    return len;
}

static isize procfs_loadavg(void *buf, usize count) {
    char tmp[64];
    usize tasks = sched_task_count();
    int len = ksnprintf(tmp, sizeof(tmp), "0.00 0.00 0.00 %lu/1 1\n", tasks);
    if (len > (int)count) len = (int)count;
    memcpy(buf, tmp, (usize)len);
    return len;
}

isize procfs_read(const char *path, void *buf, usize count) {
    if (!path || !buf) return -1;
    if (path[0] == '/') path++;
    if (strcmp(path, "cpuinfo")  == 0) return procfs_cpuinfo(buf, count);
    if (strcmp(path, "meminfo")  == 0) return procfs_meminfo(buf, count);
    if (strcmp(path, "version")  == 0) return procfs_version(buf, count);
    if (strcmp(path, "uptime")   == 0) return procfs_uptime(buf, count);
    if (strcmp(path, "loadavg")  == 0) return procfs_loadavg(buf, count);
    return -1;
}
