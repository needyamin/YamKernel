#ifndef _LIBC_STDIO_H
#define _LIBC_STDIO_H

#include <nexus/types.h>
#include <stdarg.h>

/* FILE opaque type */
typedef struct _FILE FILE;

extern FILE *const stdin;
extern FILE *const stdout;
extern FILE *const stderr;

#define EOF (-1)
#define BUFSIZ 1024
#define _IONBF 0
#define _IOLBF 1
#define _IOFBF 2
#define L_ctermid 64

/* File operations */
FILE   *fopen(const char *path, const char *mode);
FILE   *fdopen(int fd, const char *mode);
int     fclose(FILE *fp);
usize   fread(void *buf, usize size, usize count, FILE *fp);
usize   fwrite(const void *buf, usize size, usize count, FILE *fp);
int     getc(FILE *fp);
int     getc_unlocked(FILE *fp);
void    flockfile(FILE *fp);
void    funlockfile(FILE *fp);
char   *fgets(char *buf, int n, FILE *fp);
int     fputs(const char *s, FILE *fp);
int     fputc(int c, FILE *fp);
int     putchar(int c);
int     setvbuf(FILE *fp, char *buf, int mode, usize size);
long    ftell(FILE *fp);
void    rewind(FILE *fp);
void    clearerr(FILE *fp);
int     ferror(FILE *fp);
int     feof(FILE *fp);
int     fileno(FILE *fp);
int     ungetc(int c, FILE *fp);
int     fflush(FILE *fp);

/* Formatted output */
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE *fp, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int vfprintf(FILE *fp, const char *fmt, va_list args);
int sprintf(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *buf, usize size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int vsnprintf(char *buf, usize size, const char *fmt, va_list args);
int vsprintf(char *buf, const char *fmt, va_list args);
int sscanf(const char *str, const char *fmt, ...);
void perror(const char *s);
int puts(const char *s);

#endif
