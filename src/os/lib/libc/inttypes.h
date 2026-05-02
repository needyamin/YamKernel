#ifndef _LIBC_INTTYPES_H
#define _LIBC_INTTYPES_H

#include "stdint.h"

typedef struct {
    intmax_t quot;
    intmax_t rem;
} imaxdiv_t;

#define PRId64 "ld"
#define PRIi64 "li"
#define PRIu64 "lu"
#define PRIx64 "lx"
#define PRIX64 "lX"
#define PRIdPTR "ld"
#define PRIuPTR "lu"
#define PRIxPTR "lx"

static inline intmax_t imaxabs(intmax_t n) { return n < 0 ? -n : n; }
static inline imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom) {
    imaxdiv_t r = { numer / denom, numer % denom };
    return r;
}

#endif
