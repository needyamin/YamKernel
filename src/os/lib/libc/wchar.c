#include "wchar.h"

usize wcslen(const wchar_t *s) {
    usize n = 0;
    while (s && s[n]) n++;
    return n;
}

int wcscmp(const wchar_t *a, const wchar_t *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(*a - *b);
}

int wcsncmp(const wchar_t *a, const wchar_t *b, usize n) {
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    return n ? (int)(*a - *b) : 0;
}

wchar_t *wcscpy(wchar_t *dest, const wchar_t *src) {
    wchar_t *d = dest;
    while ((*d++ = *src++));
    return dest;
}

wchar_t *wcsncpy(wchar_t *dest, const wchar_t *src, usize n) {
    usize i = 0;
    for (; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = 0;
    return dest;
}

wchar_t *wcschr(const wchar_t *s, wchar_t c) {
    while (*s) {
        if (*s == c) return (wchar_t *)s;
        s++;
    }
    return c == 0 ? (wchar_t *)s : NULL;
}

wchar_t *wcsrchr(const wchar_t *s, wchar_t c) {
    const wchar_t *last = NULL;
    do {
        if (*s == c) last = s;
    } while (*s++);
    return (wchar_t *)last;
}

static int wchar_digit(wchar_t c) {
    if (c >= L'0' && c <= L'9') return (int)(c - L'0');
    if (c >= L'a' && c <= L'z') return (int)(c - L'a') + 10;
    if (c >= L'A' && c <= L'Z') return (int)(c - L'A') + 10;
    return -1;
}

long wcstol(const wchar_t *nptr, wchar_t **endptr, int base) {
    const wchar_t *s = nptr;
    while (*s == L' ' || *s == L'\t') s++;
    int sign = 1;
    if (*s == L'-') { sign = -1; s++; }
    else if (*s == L'+') s++;
    if (base == 0) {
        if (*s == L'0' && (s[1] == L'x' || s[1] == L'X')) { base = 16; s += 2; }
        else if (*s == L'0') { base = 8; s++; }
        else base = 10;
    }
    long value = 0;
    int d;
    while ((d = wchar_digit(*s)) >= 0 && d < base) {
        value = value * base + d;
        s++;
    }
    if (endptr) *endptr = (wchar_t *)s;
    return sign * value;
}

static bool wchar_is_delim(wchar_t c, const wchar_t *delim) {
    while (*delim) {
        if (c == *delim++) return true;
    }
    return false;
}

wchar_t *wcstok(wchar_t *str, const wchar_t *delim, wchar_t **saveptr) {
    wchar_t *s = str ? str : (saveptr ? *saveptr : NULL);
    if (!s) return NULL;
    while (*s && wchar_is_delim(*s, delim)) s++;
    if (!*s) {
        if (saveptr) *saveptr = s;
        return NULL;
    }
    wchar_t *tok = s;
    while (*s && !wchar_is_delim(*s, delim)) s++;
    if (*s) *s++ = 0;
    if (saveptr) *saveptr = s;
    return tok;
}

wchar_t *wmemchr(const wchar_t *s, wchar_t c, usize n) {
    for (usize i = 0; i < n; i++) {
        if (s[i] == c) return (wchar_t *)&s[i];
    }
    return NULL;
}

int wmemcmp(const wchar_t *a, const wchar_t *b, usize n) {
    for (usize i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(a[i] - b[i]);
    }
    return 0;
}

usize mbrtowc(wchar_t *pwc, const char *s, usize n, mbstate_t *ps) {
    (void)ps;
    if (!s) return 0;
    if (n == 0) return (usize)-2;
    if (*s == 0) {
        if (pwc) *pwc = 0;
        return 0;
    }
    if (pwc) *pwc = (unsigned char)*s;
    return 1;
}

usize wcsftime(wchar_t *wcs, usize max, const wchar_t *format, const struct tm *tm) {
    if (!wcs || max == 0 || !format || !tm) return 0;
    char narrow_fmt[128];
    usize i = 0;
    for (; i + 1 < sizeof(narrow_fmt) && format[i]; i++) {
        wchar_t c = format[i];
        narrow_fmt[i] = (c >= 0 && c < 128) ? (char)c : '?';
    }
    narrow_fmt[i] = 0;
    char narrow_out[128];
    usize written = strftime(narrow_out, sizeof(narrow_out), narrow_fmt, tm);
    if (written == 0 || written >= max) return 0;
    for (usize j = 0; j <= written; j++) {
        wcs[j] = (unsigned char)narrow_out[j];
    }
    return written;
}
