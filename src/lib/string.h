/* ============================================================================
 * YamKernel — String Library Header
 * ============================================================================ */

#ifndef _LIB_STRING_H
#define _LIB_STRING_H

#include <nexus/types.h>

void  *memset(void *dest, int val, usize count);
void  *memcpy(void *dest, const void *src, usize count);
void  *memmove(void *dest, const void *src, usize count);
int    memcmp(const void *a, const void *b, usize count);

usize  strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, usize n);
char  *strcpy(char *dest, const char *src);
char  *strncpy(char *dest, const char *src, usize n);
char  *strstr(const char *haystack, const char *needle);

#endif /* _LIB_STRING_H */
