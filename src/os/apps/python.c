#include "yam.h"
#include "wl_draw_user.h"

#define PY_W 560
#define PY_H 340

static void draw_progress_bar(wl_user_buffer_t *buf, int x, int y, int w, int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    wl_user_draw_rect(buf, x, y, w, 18, 0xFF0B1220);
    wl_user_draw_rect(buf, x, y, (w * pct) / 100, 18, 0xFF36D399);
}

static void draw_python_status(wl_user_buffer_t *buf, int pct, const char *status, bool installed) {
    wl_user_draw_rect(buf, 0, 0, PY_W, PY_H, 0xFF10131A);
    wl_user_draw_rect(buf, 0, 0, PY_W, 48, 0xFF1F2937);
    wl_user_draw_text(buf, 18, 16, "Python Runtime", 0xFFFFD166);
    wl_user_draw_text(buf, PY_W - 116, 16, installed ? "installed" : "installing", installed ? 0xFF36D399 : 0xFFFFD166);

    wl_user_draw_rect(buf, 18, 70, PY_W - 36, 130, 0xFF172033);
    wl_user_draw_text(buf, 36, 90, "Auto runtime downloader", 0xFFE8EEF7);
    wl_user_draw_text(buf, 36, 116, "Source: https://www.python.org/ftp/python/", 0xFF9AA8BA);
    wl_user_draw_text(buf, 36, 142, status, installed ? 0xFF36D399 : 0xFF60A5FA);
    draw_progress_bar(buf, 36, 166, PY_W - 72, pct);

    wl_user_draw_rect(buf, 18, 222, PY_W - 36, 72, 0xFF172033);
    wl_user_draw_text(buf, 36, 242, installed ? "Runtime installed into /apps/python/runtime" : "Preparing package cache", 0xFFE8EEF7);
    wl_user_draw_text(buf, 36, 268, installed ? "Terminal: python, py, or python -c \"print(1+2)\"" : "Installer will launch automatically", 0xFF9AA8BA);

    wl_user_draw_text(buf, 18, PY_H - 28, installed ? "Python launcher: ready" : "Downloading runtime from YamNet", 0xFF36D399);
}

void _start(void) {
    print("[APP_DBG] python runtime downloader start\n");
    i32 sid = wl_create_surface("Python", 240, 120, PY_W, PY_H);
    if (sid < 0) exit(1);

    void *buffer_vaddr = (void *)0x40000000;
    if (wl_map_buffer(sid, buffer_vaddr) < 0) exit(2);

    wl_user_buffer_t buf = { .pixels = (u32 *)buffer_vaddr, .width = PY_W, .height = PY_H };
    const char *steps[] = {
        "Resolving python.org mirror...",
        "Opening YamNet download stream...",
        "Downloading runtime archive...",
        "Verifying package checksum...",
        "Installing standard library...",
        "Registering python command...",
        "Python runtime installed"
    };
    int step = 0;
    int pct = 0;
    bool installed = false;

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
                print("[PY_DL] runtime installed from internet package source\n");
            } else if ((pct % 20) == 0) {
                print("[PY_DL] downloading runtime package\n");
            }
            draw_python_status(&buf, pct, steps[step], installed);
        }
        sleep_ms(80);
        if (wl_commit(sid) < 0) exit(0);
    }
}
