#ifndef _LIBC_NL_TYPES_H
#define _LIBC_NL_TYPES_H

typedef int nl_catd;
typedef int nl_item;

#define NL_SETD 1
#define NL_CAT_LOCALE 1

static inline nl_catd catopen(const char *name, int flag) {
    (void)name;
    (void)flag;
    return -1;
}

static inline char *catgets(nl_catd catalog, int set_number, int message_number, const char *message) {
    (void)catalog;
    (void)set_number;
    (void)message_number;
    return (char *)message;
}

static inline int catclose(nl_catd catalog) {
    (void)catalog;
    return -1;
}

#endif
