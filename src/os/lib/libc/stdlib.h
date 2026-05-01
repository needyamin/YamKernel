#ifndef _LIBC_STDLIB_H
#define _LIBC_STDLIB_H

#include <nexus/types.h>

void  *malloc(usize size);
void   free(void *ptr);
void  *calloc(usize num, usize size);
void  *realloc(void *ptr, usize new_size);

int    atoi(const char *s);
long   atol(const char *s);
long   strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);
double strtod(const char *s, char **end);

char  *getenv(const char *name);
int    setenv(const char *name, const char *value, int overwrite);
int    unsetenv(const char *name);

void   qsort(void *base, usize nmemb, usize size,
             int (*compar)(const void *, const void *));
void  *bsearch(const void *key, const void *base, usize nmemb, usize size,
               int (*compar)(const void *, const void *));

int    abs(int n);
long   labs(long n);

void   exit(int status) __attribute__((noreturn));
void   abort(void) __attribute__((noreturn));

#endif
