#ifndef _LIBC_LANGINFO_H
#define _LIBC_LANGINFO_H

#include "nl_types.h"

#define CODESET 1
#define RADIXCHAR 2
#define THOUSEP 3
#define D_T_FMT 4
#define D_FMT 5
#define T_FMT 6
#define AM_STR 7
#define PM_STR 8

static inline char *nl_langinfo(nl_item item) {
    switch (item) {
        case CODESET: return "UTF-8";
        case RADIXCHAR: return ".";
        case THOUSEP: return "";
        default: return "";
    }
}

#endif
