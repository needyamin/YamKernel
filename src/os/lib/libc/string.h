#ifndef _LIBC_STRING_H
#define _LIBC_STRING_H

#include <nexus/types.h>

void *memset(void *dst, int c, usize n);
void *memcpy(void *dst, const void *src, usize n);
int memcmp(const void *s1, const void *s2, usize n);

usize strlen(const char *s);
char *strcpy(char *dest, const char *src);
int strcmp(const char *s1, const char *s2);

#endif
