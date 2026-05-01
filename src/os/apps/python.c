#include "yam.h"
#include "wl_draw_user.h"

#define PY_W 560
#define PY_H 340

static void draw_python_status(wl_user_buffer_t *buf) {
    wl_user_draw_rect(buf, 0, 0, PY_W, PY_H, 0xFF10131A);
    wl_user_draw_rect(buf, 0, 0, PY_W, 48, 0xFF1F2937);
    wl_user_draw_text(buf, 18, 16, "Python Runtime", 0xFFFFD166);
    wl_user_draw_text(buf, PY_W - 116, 16, "not installed", 0xFFFF5C70);

    wl_user_draw_rect(buf, 18, 70, PY_W - 36, 104, 0xFF172033);
    wl_user_draw_text(buf, 36, 90, "Real Python port slot is ready.", 0xFFE8EEF7);
    wl_user_draw_text(buf, 36, 116, "Replace this app with your Python/MicroPython port", 0xFF9AA8BA);
    wl_user_draw_text(buf, 36, 142, "and keep the output name: build/python.elf", 0xFF9AA8BA);

    wl_user_draw_rect(buf, 18, 198, PY_W - 36, 96, 0xFF172033);
    wl_user_draw_text(buf, 36, 218, "Expected entry:", 0xFF60A5FA);
    wl_user_draw_text(buf, 36, 244, "  void _start(void)", 0xFFE8EEF7);
    wl_user_draw_text(buf, 36, 270, "Use YamOS syscalls from src/os/apps/yam.h", 0xFFE8EEF7);

    wl_user_draw_text(buf, 18, PY_H - 28, "Launcher integration: ready", 0xFF36D399);
}

void _start(void) {
    print("[APP_DBG] python placeholder start\n");
    i32 sid = wl_create_surface("Python", 240, 120, PY_W, PY_H);
    if (sid < 0) exit(1);

    void *buffer_vaddr = (void *)0x40000000;
    if (wl_map_buffer(sid, buffer_vaddr) < 0) exit(2);

    wl_user_buffer_t buf = { .pixels = (u32 *)buffer_vaddr, .width = PY_W, .height = PY_H };
    draw_python_status(&buf);
    if (wl_commit(sid) < 0) exit(0);

    while (1) {
        input_event_t ev;
        while (wl_poll_event(sid, &ev)) {
            if (ev.type == EV_CLOSE) exit(0);
            if (ev.type == EV_KEY && ev.value == KEY_PRESSED && ev.code == 1) exit(0);
        }
        sleep_ms(50);
        if (wl_commit(sid) < 0) exit(0);
    }
}
