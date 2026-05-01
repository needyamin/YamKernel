#ifndef _LIBC_STRING_H
#define _LIBC_STRING_H

#include <nexus/types.h>

void *memset(void *dst, int c, usize n);
void *memcpy(void *dst, const void *src, usize n);
void *memmove(void *dst, const void *src, usize n);
int memcmp(const void *s1, const void *s2, usize n);
void *memchr(const void *s, int c, usize n);

usize strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, usize n);
char *strcat(char *dest, const char *src);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, usize n);

#endif
