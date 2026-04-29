#ifndef _LIBC_STDLIB_H
#define _LIBC_STDLIB_H

#include <nexus/types.h>

void *malloc(usize size);
void free(void *ptr);
void *calloc(usize num, usize size);
void *realloc(void *ptr, usize size);

void exit(int status);

#endif
