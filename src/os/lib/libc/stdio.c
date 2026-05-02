/* ============================================================================
 * YamKernel — POSIX libc: stdio (User-Space)
 * Full stdio implementation: printf, fopen, fread, fwrite, fclose, fgets.
 * ============================================================================ */
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "../libyam/syscall.h"
#include <stdarg.h>

/* errno per-task (simplified: global for now) */
int errno = 0;

/* ---- FILE struct ---- */
struct _FILE {
    int  fd;
    int  mode;    /* 0=r, 1=w, 2=rw, 3=a */
    char unget;
    bool has_unget;
    bool eof;
    bool err;
};

static struct _FILE g_files[64];
static bool g_files_init = false;

static void files_init(void) {
    if (g_files_init) return;
    for (int i = 0; i < 64; i++) g_files[i].fd = -1;
    g_files[0].fd = 0; /* stdin  */
    g_files[1].fd = 1; /* stdout */
    g_files[2].fd = 2; /* stderr */
    g_files_init = true;
}

FILE *fopen(const char *path, const char *mode) {
    files_init();
    int flags = 0, m = 0;
    if (mode[0] == 'r') { flags = 0; m = 0; }
    else if (mode[0] == 'w') { flags = 0x41; m = 1; } /* O_WRONLY|O_CREAT */
    else if (mode[0] == 'a') { flags = 0x401; m = 3; } /* O_WRONLY|O_APPEND|O_CREAT */
    if (mode[1] == '+') { flags |= 2; m = 2; }

    int fd = (int)syscall2(SYS_OPEN, (u64)path, (u64)flags);
    if (fd < 0) return NULL;

    for (int i = 3; i < 64; i++) {
        if (g_files[i].fd < 0) {
            g_files[i].fd = fd;
            g_files[i].mode = m;
            g_files[i].has_unget = false;
            g_files[i].eof = false;
            g_files[i].err = false;
            return &g_files[i];
        }
    }
    syscall1(SYS_CLOSE, (u64)fd);
    return NULL;
}

int fclose(FILE *fp) {
    if (!fp || fp->fd < 0) return -1;
    syscall1(SYS_CLOSE, (u64)fp->fd);
    fp->fd = -1;
    return 0;
}

FILE *fdopen(int fd, const char *mode) {
    files_init();
    if (fd < 0) return NULL;
    int m = 0;
    if (mode && mode[0] == 'w') m = 1;
    else if (mode && mode[0] == 'a') m = 3;
    if (mode && mode[1] == '+') m = 2;

    for (int i = 3; i < 64; i++) {
        if (g_files[i].fd < 0) {
            g_files[i].fd = fd;
            g_files[i].mode = m;
            g_files[i].has_unget = false;
            g_files[i].eof = false;
            g_files[i].err = false;
            return &g_files[i];
        }
    }
    return NULL;
}

usize fread(void *buf, usize size, usize count, FILE *fp) {
    if (!fp || fp->fd < 0 || fp->eof) return 0;
    usize total = size * count;
    long n = (long)syscall3(SYS_READ, (u64)fp->fd, (u64)buf, (u64)total);
    if (n < 0) { fp->err = true; return 0; }
    if (n == 0) { fp->eof = true; return 0; }
    return (usize)n / size;
}

usize fwrite(const void *buf, usize size, usize count, FILE *fp) {
    if (!fp || fp->fd < 0) return 0;
    usize total = size * count;
    long n = (long)syscall3(SYS_WRITE_FD, (u64)fp->fd, (u64)buf, (u64)total);
    if (n < 0) { fp->err = true; return 0; }
    return (usize)n / size;
}

char *fgets(char *buf, int n, FILE *fp) {
    if (!fp || fp->fd < 0 || fp->eof || n <= 0) return NULL;
    int i = 0;
    while (i < n - 1) {
        char c;
        long r = (long)syscall3(SYS_READ, (u64)fp->fd, (u64)&c, 1);
        if (r < 0) { fp->err = true; break; }
        if (r == 0) { fp->eof = true; break; }
        buf[i++] = c;
        if (c == '\n') break;
    }
    if (i == 0) return NULL;
    buf[i] = '\0';
    return buf;
}

int feof(FILE *fp) { return fp ? fp->eof : 1; }
int ferror(FILE *fp) { return fp ? fp->err : 1; }
int fileno(FILE *fp) { return fp ? fp->fd : -1; }

int ungetc(int c, FILE *fp) {
    if (!fp) return -1;
    fp->unget = (char)c; fp->has_unget = true;
    return c;
}

int getc(FILE *fp) {
    if (!fp || fp->fd < 0) return EOF;
    if (fp->has_unget) {
        fp->has_unget = false;
        return (unsigned char)fp->unget;
    }
    unsigned char c;
    long r = (long)syscall3(SYS_READ, (u64)fp->fd, (u64)&c, 1);
    if (r < 0) {
        fp->err = true;
        return EOF;
    }
    if (r == 0) {
        fp->eof = true;
        return EOF;
    }
    return c;
}

int getc_unlocked(FILE *fp) { return getc(fp); }
void flockfile(FILE *fp) { (void)fp; }
void funlockfile(FILE *fp) { (void)fp; }
void clearerr(FILE *fp) {
    if (!fp) return;
    fp->eof = false;
    fp->err = false;
}
long ftell(FILE *fp) {
    if (!fp || fp->fd < 0) return -1;
    return (long)syscall3(SYS_LSEEK, (u64)fp->fd, 0, 1);
}
void rewind(FILE *fp) {
    if (!fp || fp->fd < 0) return;
    syscall3(SYS_LSEEK, (u64)fp->fd, 0, 0);
    fp->eof = false;
    fp->err = false;
}

/* ---- vsprintf with full format support ---- */
static void int_to_str(unsigned long n, char *buf, int base, bool is_signed, bool upper, int width, char pad) {
    const char *chars_lower = "0123456789abcdef";
    const char *chars_upper = "0123456789ABCDEF";
    const char *chars = upper ? chars_upper : chars_lower;
    char tmp[64]; int i = 0; bool neg = false;

    if (is_signed && (long)n < 0) { neg = true; n = (unsigned long)(-(long)n); }
    if (n == 0) { tmp[i++] = '0'; }
    else { while (n > 0) { tmp[i++] = chars[n % base]; n /= base; } }

    /* Width padding */
    int len = i + (neg ? 1 : 0);
    int pad_cnt = width > len ? width - len : 0;
    char *out = buf;
    if (pad == '0' && neg) { *out++ = '-'; neg = false; }
    while (pad_cnt-- > 0) *out++ = pad;
    if (neg) *out++ = '-';
    while (i > 0) *out++ = tmp[--i];
    *out = '\0';
}

static int vsnprintf_impl(char *buf, usize size, const char *fmt, va_list args) {
    char *out = buf; usize remaining = size - 1;

    while (*fmt && remaining > 0) {
        if (*fmt != '%') { *out++ = *fmt++; remaining--; continue; }
        fmt++; /* skip '%' */

        /* Parse flags */
        char pad = ' '; int width = 0; bool long_arg = false;
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '1' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        if (*fmt == 'l') { long_arg = true; fmt++; }
        if (*fmt == 'l') fmt++; /* ll */

        char tmp[80] = {0};
        switch (*fmt) {
            case 'd': {
                long v = long_arg ? va_arg(args, long) : va_arg(args, int);
                int_to_str((unsigned long)v, tmp, 10, true, false, width, pad);
                break;
            }
            case 'u': {
                unsigned long v = long_arg ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
                int_to_str(v, tmp, 10, false, false, width, pad);
                break;
            }
            case 'x': {
                unsigned long v = long_arg ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
                int_to_str(v, tmp, 16, false, false, width, pad);
                break;
            }
            case 'X': {
                unsigned long v = long_arg ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
                int_to_str(v, tmp, 16, false, true, width, pad);
                break;
            }
            case 'o': {
                unsigned long v = long_arg ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
                int_to_str(v, tmp, 8, false, false, width, pad);
                break;
            }
            case 'p': {
                void *ptr = va_arg(args, void *);
                tmp[0] = '0'; tmp[1] = 'x';
                int_to_str((unsigned long)ptr, tmp + 2, 16, false, false, 0, '0');
                break;
            }
            case 's': {
                char *s = va_arg(args, char *);
                if (!s) s = "(null)";
                usize slen = strlen(s);
                if (slen > remaining) slen = remaining;
                memcpy(out, s, slen); out += slen; remaining -= slen;
                fmt++; continue;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                *out++ = c; remaining--; fmt++; continue;
            }
            case '%':
                *out++ = '%'; remaining--; fmt++; continue;
            case '\0':
                goto done;
            default:
                *out++ = '%'; *out++ = *fmt; remaining -= 2; fmt++; continue;
        }
        usize tlen = strlen(tmp);
        if (tlen > remaining) tlen = remaining;
        memcpy(out, tmp, tlen); out += tlen; remaining -= tlen;
        fmt++;
    }
done:
    *out = '\0';
    return (int)(out - buf);
}

int vsnprintf(char *buf, usize size, const char *fmt, va_list args) {
    return vsnprintf_impl(buf, size, fmt, args);
}
int vsprintf(char *buf, const char *fmt, va_list args) {
    return vsnprintf_impl(buf, 4096, fmt, args);
}
int sprintf(char *buf, const char *fmt, ...) {
    va_list a; va_start(a, fmt); int n = vsprintf(buf, fmt, a); va_end(a); return n;
}
int snprintf(char *buf, usize size, const char *fmt, ...) {
    va_list a; va_start(a, fmt); int n = vsnprintf(buf, size, fmt, a); va_end(a); return n;
}
int sscanf(const char *str, const char *fmt, ...) {
    (void)str;
    (void)fmt;
    return 0;
}
int printf(const char *fmt, ...) {
    char buf[2048]; va_list a; va_start(a, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, a); va_end(a);
    syscall3(SYS_WRITE, 1, (u64)buf, (u64)n);
    return n;
}
int fprintf(FILE *fp, const char *fmt, ...) {
    char buf[2048]; va_list a; va_start(a, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, a); va_end(a);
    if (!fp || fp->fd < 0) return -1;
    syscall3(SYS_WRITE_FD, (u64)fp->fd, (u64)buf, (u64)n);
    return n;
}
int vfprintf(FILE *fp, const char *fmt, va_list args) {
    char buf[2048];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    if (!fp || fp->fd < 0) return -1;
    syscall3(SYS_WRITE_FD, (u64)fp->fd, (u64)buf, (u64)n);
    return n;
}
int fputs(const char *s, FILE *fp) {
    if (!fp || fp->fd < 0 || !s) return -1;
    usize l = strlen(s);
    return (int)syscall3(SYS_WRITE_FD, (u64)fp->fd, (u64)s, (u64)l);
}
int fputc(int c, FILE *fp) {
    unsigned char ch = (unsigned char)c;
    return fwrite(&ch, 1, 1, fp) == 1 ? ch : EOF;
}
int putchar(int c) { return fputc(c, stdout); }
int setvbuf(FILE *fp, char *buf, int mode, usize size) {
    (void)fp;
    (void)buf;
    (void)mode;
    (void)size;
    return 0;
}
int puts(const char *s) {
    if (!s) return -1;
    syscall3(SYS_WRITE, 1, (u64)s, (u64)strlen(s));
    syscall3(SYS_WRITE, 1, (u64)"\n", 1);
    return 0;
}
void perror(const char *s) {
    if (s && *s) {
        fputs(s, stderr);
        fputs(": ", stderr);
    }
    fputs("YamOS libc error\n", stderr);
}
int fflush(FILE *fp) { (void)fp; return 0; }

/* stdin/stdout/stderr file pointers */
FILE *const stdin  = &g_files[0];
FILE *const stdout = &g_files[1];
FILE *const stderr = &g_files[2];
