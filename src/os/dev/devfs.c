/* ============================================================================
 * YamKernel — /dev Device Filesystem
 * Provides /dev/null, /dev/tty0-5, /dev/fb0, /dev/keyboard
 * ============================================================================ */
#include "../../lib/kprintf.h"
#include "../../lib/string.h"
#include "../../mem/heap.h"
#include <nexus/types.h>

/* Forward declarations */
extern void vtty_write_char(int tty_id, char c);
extern int  vtty_read_char(int tty_id);
extern int  g_active_vtty;

/* ---- /dev/null ---- */
static isize devnull_read(const char *p, void *buf, usize count, usize off) {
    (void)p; (void)buf; (void)count; (void)off; return 0;
}
static isize devnull_write(const char *p, const void *buf, usize count) {
    (void)p; (void)buf; return (isize)count;
}

/* ---- /dev/tty0..5 ---- */
static int tty_id_from_path(const char *path) {
    /* path = "/tty0" etc. */
    if (path[0] == '/') path++;
    if (path[0] == 't' && path[1] == 't' && path[2] == 'y' && path[3] >= '0' && path[3] <= '5')
        return path[3] - '0';
    if (strcmp(path, "tty") == 0) return g_active_vtty;
    if (strcmp(path, "console") == 0) return g_active_vtty;
    return -1;
}

static isize devtty_read(const char *path, void *buf, usize count, usize off) {
    (void)off;
    int id = tty_id_from_path(path);
    if (id < 0) return -1;
    u8 *out = (u8 *)buf; usize i = 0;
    while (i < count) {
        int c = vtty_read_char(id);
        if (c < 0) break;
        out[i++] = (u8)c;
        if (c == '\n') break;
    }
    return (isize)i;
}

static isize devtty_write(const char *path, const void *buf, usize count) {
    int id = tty_id_from_path(path);
    if (id < 0) id = g_active_vtty;
    const char *s = (const char *)buf;
    for (usize i = 0; i < count; i++) vtty_write_char(id, s[i]);
    return (isize)count;
}

/* ---- /dev/fb0 (stub) ---- */
static isize devfb_read(const char *p, void *buf, usize count, usize off) {
    (void)p; (void)buf; (void)count; (void)off; return -1;
}
static isize devfb_write(const char *p, const void *buf, usize count) {
    (void)p; (void)buf; (void)count; return -1;
}

/* ---- Dispatch ---- */

bool devfs_exists(const char *path) {
    if (path[0] == '/') path++;
    if (strcmp(path, "null") == 0)    return true;
    if (strcmp(path, "zero") == 0)    return true;
    if (strcmp(path, "fb0") == 0)     return true;
    if (strcmp(path, "tty") == 0)     return true;
    if (strcmp(path, "console") == 0) return true;
    if (path[0]=='t'&&path[1]=='t'&&path[2]=='y'&&path[3]>='0'&&path[3]<='5') return true;
    return false;
}

isize devfs_read(const char *path, void *buf, usize count, usize offset) {
    if (path[0] == '/') path++;
    if (strcmp(path, "null") == 0 || strcmp(path, "zero") == 0)
        return devnull_read(path, buf, count, offset);
    if (strcmp(path, "fb0") == 0) return devfb_read(path, buf, count, offset);
    /* tty devices */
    return devtty_read(path, buf, count, offset);
}

isize devfs_write(const char *path, const void *buf, usize count) {
    if (path[0] == '/') path++;
    if (strcmp(path, "null") == 0) return devnull_write(path, buf, count);
    if (strcmp(path, "fb0") == 0)  return devfb_write(path, buf, count);
    return devtty_write(path, buf, count);
}

void devfs_init(void) {
    kprintf_color(0xFF00FF88, "[DEVFS] /dev ready: null, tty0-5, fb0, console\n");
}
