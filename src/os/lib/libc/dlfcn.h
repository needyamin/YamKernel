#ifndef _LIBC_DLFCN_H
#define _LIBC_DLFCN_H

#define RTLD_LAZY 0x1
#define RTLD_NOW 0x2
#define RTLD_GLOBAL 0x100
#define RTLD_LOCAL 0
#define RTLD_DEFAULT ((void *)0)
#define RTLD_NEXT ((void *)-1)
#define RTLD_DL_LINKMAP 2

typedef struct {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
} Dl_info;

static inline void *dlopen(const char *file, int mode) {
    (void)file;
    (void)mode;
    return (void *)0;
}

static inline int dlclose(void *handle) {
    (void)handle;
    return -1;
}

static inline void *dlsym(void *handle, const char *name) {
    (void)handle;
    (void)name;
    return (void *)0;
}

static inline char *dlerror(void) {
    return "dynamic loading unavailable";
}

static inline int dladdr1(const void *addr, Dl_info *info, void **extra_info, int flags) {
    (void)addr;
    (void)flags;
    if (info) {
        info->dli_fname = "";
        info->dli_fbase = (void *)0;
        info->dli_sname = "";
        info->dli_saddr = (void *)0;
    }
    if (extra_info) *extra_info = (void *)0;
    return 0;
}

#endif
