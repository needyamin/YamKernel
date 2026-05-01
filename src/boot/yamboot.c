/* ============================================================================
 * YamBoot - pre-kernel boot picker
 * Runs after Limine and before full kernel initialization.
 * ============================================================================ */

#include "yamboot.h"
#include "../drivers/video/framebuffer.h"
#include <nexus/types.h>

int g_yamboot_safe = 0;

#define C_BG_TOP      0xFF090B10
#define C_BG_BOTTOM   0xFF121722
#define C_PANEL       0xFF1C2028
#define C_PANEL_EDGE  0xFF3A414D
#define C_TEXT        0xFFE8ECF4
#define C_MUTED       0xFF9AA3AF
#define C_DIM         0xFF687180
#define C_ACCENT      0xFFFFFFFF
#define C_CHIP        0xFF2B313C

static int kbd_poll(void) {
    if (!(inb(0x64) & 0x01)) return -1;
    return inb(0x60);
}

static void fill_rect(u32 *pixels, u32 pitch, u32 width, u32 height,
                      int x, int y, int w, int h, u32 color) {
    if (!pixels || w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > (int)width ? (int)width : x + w;
    int y1 = y + h > (int)height ? (int)height : y + h;
    for (int yy = y0; yy < y1; yy++) {
        u32 *row = pixels + yy * (pitch / 4);
        for (int xx = x0; xx < x1; xx++) row[xx] = color;
    }
}

static void fill_round_rect(u32 *pixels, u32 pitch, u32 width, u32 height,
                            int x, int y, int w, int h, int radius, u32 color) {
    if (!pixels || w <= 0 || h <= 0) return;
    if (radius < 1) {
        fill_rect(pixels, pitch, width, height, x, y, w, h, color);
        return;
    }

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > (int)width ? (int)width : x + w;
    int y1 = y + h > (int)height ? (int)height : y + h;
    int rr = radius * radius;
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            int dx = 0, dy = 0;
            if (xx < x + radius) dx = x + radius - xx;
            else if (xx >= x + w - radius) dx = xx - (x + w - radius - 1);
            if (yy < y + radius) dy = y + radius - yy;
            else if (yy >= y + h - radius) dy = yy - (y + h - radius - 1);
            if (dx && dy && dx * dx + dy * dy > rr) continue;
            pixels[yy * (pitch / 4) + xx] = color;
        }
    }
}

static void draw_text(u32 *pixels, u32 pitch, int x, int y, const char *s, u32 fg, u32 bg) {
    while (*s) {
        fb_draw_char_at(pixels, pitch / 4, x, y, *s++, fg, bg);
        x += 8;
    }
}

static void draw_text_center(u32 *pixels, u32 pitch, u32 width,
                             int y, const char *s, u32 fg, u32 bg) {
    int len = 0;
    while (s[len]) len++;
    int x = ((int)width - len * 8) / 2;
    draw_text(pixels, pitch, x, y, s, fg, bg);
}

static void draw_mark(u32 *pixels, u32 pitch, u32 width, u32 height, int cx, int y) {
    int mark_w = 86;
    int x = cx - mark_w / 2;
    fill_round_rect(pixels, pitch, width, height, x + 18, y + 8, 50, 58, 18, C_TEXT);
    fill_round_rect(pixels, pitch, width, height, x + 4, y + 24, 34, 44, 16, C_TEXT);
    fill_round_rect(pixels, pitch, width, height, x + 52, y + 22, 34, 46, 16, C_TEXT);
    fill_rect(pixels, pitch, width, height, x + 40, y + 24, 10, 44, C_BG_TOP);
    fill_round_rect(pixels, pitch, width, height, x + 51, y, 29, 16, 8, C_TEXT);
}

static void draw_option(u32 *pixels, u32 pitch, u32 width, u32 height,
                        int x, int y, const char *key, const char *title,
                        const char *subtitle, bool primary) {
    u32 panel = primary ? 0xFF232A36 : C_PANEL;
    fill_round_rect(pixels, pitch, width, height, x, y, 420, 50, 10, panel);
    fill_round_rect(pixels, pitch, width, height, x, y, 420, 1, 1, C_PANEL_EDGE);
    fill_round_rect(pixels, pitch, width, height, x + 14, y + 13, 25, 24, 7, C_CHIP);
    draw_text(pixels, pitch, x + 22, y + 17, key, C_ACCENT, C_CHIP);
    draw_text(pixels, pitch, x + 52, y + 9, title, C_TEXT, panel);
    draw_text(pixels, pitch, x + 52, y + 27, subtitle, C_MUTED, panel);
}

static void draw(void) {
    u32 *pixels = fb_get_pixels();
    u32 width = fb_get_width();
    u32 height = fb_get_height();
    u32 pitch = fb_get_pitch();
    if (!pixels || width == 0 || height == 0) return;

    fb_enable_text(false);
    for (u32 y = 0; y < height; y++) {
        u32 t = y * 255 / height;
        u32 r = (((C_BG_TOP >> 16) & 0xFF) * (255 - t) + ((C_BG_BOTTOM >> 16) & 0xFF) * t) / 255;
        u32 g = (((C_BG_TOP >> 8) & 0xFF) * (255 - t) + ((C_BG_BOTTOM >> 8) & 0xFF) * t) / 255;
        u32 b = ((C_BG_TOP & 0xFF) * (255 - t) + (C_BG_BOTTOM & 0xFF) * t) / 255;
        for (u32 x = 0; x < width; x++) pixels[y * (pitch / 4) + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }

    int cx = (int)width / 2;
    int top = (int)height / 2 - 210;
    if (top < 34) top = 34;

    draw_mark(pixels, pitch, width, height, cx, top);
    draw_text_center(pixels, pitch, width, top + 88, "YamOS", C_TEXT, C_BG_TOP);
    draw_text_center(pixels, pitch, width, top + 110, "Choose a startup disk", C_MUTED, C_BG_TOP);

    int panel_x = cx - 210;
    int panel_y = top + 154;
    draw_option(pixels, pitch, width, height, panel_x, panel_y,
                "1", "YamOS", "Normal startup", true);
    draw_option(pixels, pitch, width, height, panel_x, panel_y + 62,
                "2", "Safe Mode", "Minimal drivers and services", false);
    draw_option(pixels, pitch, width, height, panel_x, panel_y + 124,
                "3", "Restart", "Reboot this machine", false);

    draw_text_center(pixels, pitch, width, panel_y + 196,
                     "Press Enter for YamOS. Auto-starts in 5 seconds.", C_DIM, C_BG_BOTTOM);
}

yamboot_choice_t yamboot_show(void) {
    while (inb(0x64) & 0x01) inb(0x60);
    draw();

    for (int tick = 50; tick > 0; tick--) {
        int sc = kbd_poll();
        if (sc >= 0 && !(sc & 0x80)) {
            switch (sc) {
                case 0x02:
                case 0x1C:
                    fb_enable_text(true);
                    return YAMBOOT_NORMAL;
                case 0x03:
                    g_yamboot_safe = 1;
                    fb_enable_text(true);
                    return YAMBOOT_SAFE;
                case 0x04:
                    fb_enable_text(true);
                    return YAMBOOT_REBOOT;
            }
        }

        for (volatile int d = 0; d < 100000; d++) {
            __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
        }
    }

    fb_enable_text(true);
    return YAMBOOT_NORMAL;
}
