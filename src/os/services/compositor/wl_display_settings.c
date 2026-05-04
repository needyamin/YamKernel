/* YamKernel - compositor-native Display Settings */
#include <nexus/types.h>
#include "sched/sched.h"
#include "sched/wait.h"
#include "compositor.h"
#include "wl_draw.h"
#include "drivers/video/framebuffer.h"
#include "lib/kprintf.h"
#include "lib/string.h"

#define DISP_W 540
#define DISP_H 380

#define COL_BG     0xFFF4F7FB
#define COL_PANEL  0xFFFFFFFF
#define COL_LINE   0xFFD8E0EA
#define COL_TEXT   0xFF172033
#define COL_MUTED  0xFF657186
#define COL_BLUE   0xFF2563EB
#define COL_GREEN  0xFF16A34A

static i32 display_mouse_x = -1;
static i32 display_mouse_y = -1;

static void draw_fit(wl_surface_t *s, i32 x, i32 y, const char *text, i32 max_w, u32 color) {
    char buf[96];
    int max_chars = max_w / 8;
    if (max_chars > 95) max_chars = 95;
    if (max_chars < 1) return;
    int i = 0;
    while (text && text[i] && i < max_chars) {
        buf[i] = text[i];
        i++;
    }
    if (text && text[i] && i > 1) {
        buf[i - 1] = '>';
        buf[i] = 0;
    } else {
        buf[i] = 0;
    }
    wl_draw_text(s, x, y, buf, color, 0);
}

static void draw_button(wl_surface_t *s, i32 x, i32 y, i32 w, const char *label, bool primary) {
    wl_draw_rounded_rect(s, x, y, w, 34, 8, primary ? COL_BLUE : COL_PANEL);
    wl_draw_rounded_outline(s, x, y, w, 34, 8, primary ? COL_BLUE : COL_LINE);
    i32 tx = x + (w - (i32)strlen(label) * 8) / 2;
    if (tx < x + 10) tx = x + 10;
    wl_draw_text(s, tx, y + 10, label, primary ? 0xFFFFFFFF : COL_TEXT, 0);
}

static void draw_row(wl_surface_t *s, i32 x, i32 y, const char *label, const char *value) {
    wl_draw_text(s, x, y, label, COL_MUTED, 0);
    draw_fit(s, x + 144, y, value, 210, COL_TEXT);
}

static const char *focused_title(void) {
    wl_compositor_t *comp = wl_get_compositor();
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (comp->surfaces[i].state == WL_SURFACE_ACTIVE &&
            comp->surfaces[i].id == comp->focused_id) {
            return comp->surfaces[i].title;
        }
    }
    return "Desktop";
}

static u32 active_window_count(void) {
    wl_compositor_t *comp = wl_get_compositor();
    u32 count = 0;
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (comp->surfaces[i].state == WL_SURFACE_ACTIVE ||
            comp->surfaces[i].state == WL_SURFACE_MINIMIZED) {
            count++;
        }
    }
    return count;
}

static void draw_display_settings(wl_surface_t *s) {
    wl_compositor_t *comp = wl_get_compositor();
    wl_draw_rect(s, 0, 0, DISP_W, DISP_H, COL_BG);

    wl_draw_rect(s, 0, 0, DISP_W, 64, COL_PANEL);
    wl_draw_rect(s, 0, 63, DISP_W, 1, COL_LINE);
    wl_draw_text(s, 24, 22, "Display Settings", COL_TEXT, 0);
    wl_draw_text(s, 224, 22, "Compositor and framebuffer", COL_MUTED, 0);

    wl_draw_rounded_rect(s, 24, 88, 492, 114, 10, COL_PANEL);
    wl_draw_rounded_outline(s, 24, 88, 492, 114, 10, COL_LINE);
    char resolution[48];
    ksnprintf(resolution, sizeof(resolution), "%ux%u", comp->display_w, comp->display_h);
    char pitch[48];
    ksnprintf(pitch, sizeof(pitch), "%u px stride", fb_get_pitch() / 4);
    draw_row(s, 48, 112, "Resolution", resolution);
    draw_row(s, 48, 142, "Framebuffer", pitch);
    draw_row(s, 48, 172, "Scale", "100% software compositor");

    wl_draw_rounded_rect(s, 24, 224, 492, 74, 10, COL_PANEL);
    wl_draw_rounded_outline(s, 24, 224, 492, 74, 10, COL_LINE);
    char windows[32];
    ksnprintf(windows, sizeof(windows), "%u windows", active_window_count());
    draw_row(s, 48, 248, "Focused", focused_title());
    draw_row(s, 48, 274, "Open surfaces", windows);

    wl_draw_text(s, 32, 326, comp->show_debug_overlay ? "Debug overlay is on" : "Debug overlay is off",
                 comp->show_debug_overlay ? COL_GREEN : COL_MUTED, 0);
    draw_button(s, 248, 318, 118, comp->show_debug_overlay ? "Hide debug" : "Show debug", true);
    draw_button(s, 384, 318, 112, "Refresh", false);
}

static bool hit(i32 x, i32 y, i32 rx, i32 ry, i32 rw, i32 rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void handle_display_click(i32 x, i32 y) {
    wl_compositor_t *comp = wl_get_compositor();
    if (hit(x, y, 248, 318, 118, 34)) {
        comp->show_debug_overlay = !comp->show_debug_overlay;
        kprintf("[DISPLAY] debug overlay from Display Settings: %s\n",
                comp->show_debug_overlay ? "on" : "off");
    } else if (hit(x, y, 384, 318, 112, 34)) {
        fb_clear(0xFF111827);
        kprintf("[DISPLAY] refresh requested from Display Settings\n");
    }
}

void wl_display_settings_task(void *arg) {
    (void)arg;
    task_sleep_ms(120);
    wl_surface_t *s = wl_surface_create("Display Settings", 360, 96, DISP_W, DISP_H, sched_current()->id);
    if (!s) return;
    draw_display_settings(s);
    wl_surface_commit(s);
    u32 my_id = s->id;
    while (s->state == WL_SURFACE_ACTIVE && s->id == my_id) {
        input_event_t ev;
        bool dirty = false;
        while (wl_surface_pop_event(s, &ev)) {
            if (ev.type == EV_ABS && ev.code == 0) display_mouse_x = ev.value;
            else if (ev.type == EV_ABS && ev.code == 1) display_mouse_y = ev.value;
            else if (ev.type == EV_KEY && ev.value == KEY_PRESSED && ev.code >= 0x110 &&
                     display_mouse_x >= 0 && display_mouse_y >= 0) {
                handle_display_click(display_mouse_x, display_mouse_y);
                dirty = true;
            }
        }
        if (dirty) {
            draw_display_settings(s);
            wl_surface_commit(s);
        }
        task_sleep_ms(16);
    }
}
