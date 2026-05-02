#ifndef _LIBC_WCHAR_H
#define _LIBC_WCHAR_H

#include <nexus/types.h>
#include "time.h"

typedef int wchar_t;
typedef unsigned int wint_t;
typedef struct {
    unsigned int __state;
} mbstate_t;

#define WEOF ((wint_t)-1)

usize wcslen(const wchar_t *s);
int wcscmp(const wchar_t *a, const wchar_t *b);
int wcsncmp(const wchar_t *a, const wchar_t *b, usize n);
wchar_t *wcscpy(wchar_t *dest, const wchar_t *src);
wchar_t *wcsncpy(wchar_t *dest, const wchar_t *src, usize n);
wchar_t *wcschr(const wchar_t *s, wchar_t c);
wchar_t *wcsrchr(const wchar_t *s, wchar_t c);
wchar_t *wmemchr(const wchar_t *s, wchar_t c, usize n);
int wmemcmp(const wchar_t *a, const wchar_t *b, usize n);
usize mbrtowc(wchar_t *pwc, const char *s, usize n, mbstate_t *ps);
usize wcsftime(wchar_t *wcs, usize max, const wchar_t *format, const struct tm *tm);
long wcstol(const wchar_t *nptr, wchar_t **endptr, int base);
wchar_t *wcstok(wchar_t *str, const wchar_t *delim, wchar_t **saveptr);

#endif
