#ifndef _LIBC_SYS_UTSNAME_H
#define _LIBC_SYS_UTSNAME_H

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

static inline int uname(struct utsname *buf) {
    if (!buf) return -1;
    const char *sys = "YamOS";
    const char *mach = "x86_64";
    int i = 0;
    for (; sys[i]; i++) buf->sysname[i] = sys[i];
    buf->sysname[i] = 0;
    buf->nodename[0] = 0;
    buf->release[0] = '0'; buf->release[1] = 0;
    buf->version[0] = '0'; buf->version[1] = 0;
    for (i = 0; mach[i]; i++) buf->machine[i] = mach[i];
    buf->machine[i] = 0;
    return 0;
}

#endif
