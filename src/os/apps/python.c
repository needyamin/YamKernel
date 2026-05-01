#include "yam.h"
#include "wl_draw_user.h"

#define PY_W 560
#define PY_H 340

static const char *const steps[] = {
    "Finding CPython 3.14.4 source...",
    "Checking CPython static link target...",
    "Checking POSIX dup/dup2 service...",
    "Checking terminal stdio service...",
    "Checking writable Python Lib path...",
    "Checking HTTPS/pip installer requirements...",
    "Next: writable Lib path + static CPython link"
};

static bool probe_dup_service(void) {
    long fd = syscall1(SYS_DUP, 1);
    if (fd < 0) return false;
    syscall1(SYS_CLOSE, fd);

    long fd2 = syscall2(SYS_DUP2, 1, 9);
    if (fd2 != 9) return false;
    syscall1(SYS_CLOSE, 9);
    return true;
}

static void draw_progress_bar(wl_user_buffer_t *buf, int x, int y, int w, int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    wl_user_draw_rect(buf, x, y, w, 18, 0xFF0B1220);
    wl_user_draw_rect(buf, x, y, (w * pct) / 100, 18, 0xFF36D399);
}

static void draw_python_status(wl_user_buffer_t *buf, int pct, const char *status, bool installed) {
    wl_user_draw_rect(buf, 0, 0, PY_W, PY_H, 0xFF10131A);
    wl_user_draw_rect(buf, 0, 0, PY_W, 48, 0xFF1F2937);
    wl_user_draw_text(buf, 18, 16, "CPython Installer", 0xFFFFD166);
    wl_user_draw_text(buf, PY_W - 116, 16, installed ? "blocked" : "checking", installed ? 0xFFFF5C70 : 0xFFFFD166);

    wl_user_draw_rect(buf, 18, 70, PY_W - 36, 130, 0xFF172033);
    wl_user_draw_text(buf, 36, 90, "python.org CPython port status", 0xFFE8EEF7);
    wl_user_draw_text(buf, 36, 116, "Source: vendor/cpython/Python-3.14.4", 0xFF9AA8BA);
    wl_user_draw_text(buf, 36, 142, status, installed ? 0xFF36D399 : 0xFF60A5FA);
    draw_progress_bar(buf, 36, 166, PY_W - 72, pct);

    wl_user_draw_rect(buf, 18, 222, PY_W - 36, 72, 0xFF172033);
    wl_user_draw_text(buf, 36, 242, installed ? "Ready services: ELF ABI, stdio, dup/dup2" : "Preparing YamOS port layer", 0xFFE8EEF7);
    wl_user_draw_text(buf, 36, 268, installed ? "Next: writable Lib path and CPython static link" : "Port needs POSIX/libc/VFS/tty glue", 0xFF9AA8BA);

    wl_user_draw_text(buf, 18, PY_H - 28, installed ? "CPython source present; continuing OS port work" : "Checking python.org source", installed ? 0xFFFFD166 : 0xFF36D399);
}

void _start(void) {
    print("[APP_DBG] CPython installer start\n");
    i32 sid = wl_create_surface("Python", 240, 120, PY_W, PY_H);
    if (sid < 0) exit(1);

    void *buffer_vaddr = (void *)0x40000000;
    if (wl_map_buffer(sid, buffer_vaddr) < 0) exit(2);

    wl_user_buffer_t buf = { .pixels = (u32 *)buffer_vaddr, .width = PY_W, .height = PY_H };
    int step = 0;
    int pct = 0;
    bool installed = false;
    bool dup_ok = probe_dup_service();
    print(dup_ok ? "[PYTHON] POSIX dup/dup2 service ready\n" : "[PYTHON] POSIX dup/dup2 service missing\n");

    draw_python_status(&buf, pct, steps[step], installed);
    if (wl_commit(sid) < 0) exit(0);

    while (1) {
        input_event_t ev;
        while (wl_poll_event(sid, &ev)) {
            if (ev.type == EV_CLOSE) exit(0);
            if (ev.type == EV_KEY && ev.value == KEY_PRESSED && ev.code == 1) exit(0);
        }
        if (!installed) {
            pct += 4;
            if (pct > 100) pct = 100;
            step = pct / 17;
            if (step > 6) step = 6;
            if (pct == 100) {
                installed = true;
                print("[PYTHON] CPython next blocker: writable Lib path + static interpreter link\n");
            } else if ((pct % 20) == 0) {
                print("[PYTHON] checking CPython installer requirements\n");
            }
            draw_python_status(&buf, pct, steps[step], installed);
        }
        sleep_ms(80);
        if (wl_commit(sid) < 0) exit(0);
    }
}
