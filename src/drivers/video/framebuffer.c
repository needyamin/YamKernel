/* ============================================================================
 * YamKernel - Framebuffer Driver
 * Software text rendering plus boot UI drawing.
 * ============================================================================ */

#include "framebuffer.h"
#include "../../lib/string.h"
#include "../../lib/font.h"

#define FONT_W 8
#define FONT_H 16

static struct limine_framebuffer *g_fb = NULL;
static u32 *g_pixels = NULL;
static u32 g_pitch4 = 0;
static u32 g_cursor_col = 0;
static u32 g_cursor_row = 0;
static u32 g_max_cols = 0;
static u32 g_max_rows = 0;
static bool g_fb_text_enabled = true;

void fb_init(struct limine_framebuffer *fb) {
    g_fb = fb;
    g_pixels = (u32 *)fb->address;
    g_pitch4 = (u32)(fb->pitch / 4);
    g_max_cols = (u32)(fb->width / FONT_W);
    g_max_rows = (u32)(fb->height / FONT_H);
    g_cursor_col = 0;
    g_cursor_row = 0;
    g_fb_text_enabled = true;
    fb_clear(FB_COLOR_DARK_BG);
}

void fb_put_pixel(u32 x, u32 y, u32 color) {
    if (!g_pixels || x >= g_fb->width || y >= g_fb->height) return;
    g_pixels[y * g_pitch4 + x] = color;
}

void fb_clear(u32 color) {
    if (!g_pixels) return;
    u64 total = (u64)g_pitch4 * g_fb->height;
    u32 *p = g_pixels;
    u32 *end = p + total;
    while (p + 8 <= end) {
        p[0] = color; p[1] = color; p[2] = color; p[3] = color;
        p[4] = color; p[5] = color; p[6] = color; p[7] = color;
        p += 8;
    }
    while (p < end) *p++ = color;
    g_cursor_col = 0;
    g_cursor_row = 0;
}

static void fb_draw_char(u32 col, u32 row, char c, u32 fg, u32 bg) {
    if (!g_pixels) return;
    fb_draw_char_at(g_pixels, g_pitch4, col * FONT_W, row * FONT_H, c, fg, bg);
}

void fb_draw_char_at(u32 *pixels, u32 pitch_pixels, int x0, int y0, char c, u32 fg, u32 bg) {
    if (!pixels) return;
    int idx = (int)c - 32;
    if (idx < 0 || idx >= 95) idx = 0;

    for (int y = 0; y < FONT_H; y++) {
        u8 row_bits = yam_font_data[idx][y];
        u32 *scanline = pixels + (y0 + y) * pitch_pixels + x0;
        scanline[0] = (row_bits & 0x80) ? fg : bg;
        scanline[1] = (row_bits & 0x40) ? fg : bg;
        scanline[2] = (row_bits & 0x20) ? fg : bg;
        scanline[3] = (row_bits & 0x10) ? fg : bg;
        scanline[4] = (row_bits & 0x08) ? fg : bg;
        scanline[5] = (row_bits & 0x04) ? fg : bg;
        scanline[6] = (row_bits & 0x02) ? fg : bg;
        scanline[7] = (row_bits & 0x01) ? fg : bg;
    }
}

void fb_scroll(void) {
    if (!g_pixels) return;
    u32 line_height = FONT_H;
    u64 copy_size = (u64)g_pitch4 * (g_fb->height - line_height) * sizeof(u32);
    memmove(g_pixels, g_pixels + g_pitch4 * line_height, copy_size);

    u32 *clear_ptr = g_pixels + g_pitch4 * (g_fb->height - line_height);
    u64 clear_count = (u64)g_pitch4 * line_height;
    for (u64 i = 0; i < clear_count; i++) clear_ptr[i] = FB_COLOR_DARK_BG;
    g_cursor_row--;
}

void fb_putchar(char c, u32 fg, u32 bg) {
    if (!g_pixels || !g_fb_text_enabled) return;

    if (c == '\n') {
        g_cursor_col = 0;
        g_cursor_row++;
    } else if (c == '\r') {
        g_cursor_col = 0;
    } else if (c == '\t') {
        g_cursor_col = (g_cursor_col + 4) & ~3u;
    } else if (c == '\b') {
        if (g_cursor_col > 0) g_cursor_col--;
    } else {
        fb_draw_char(g_cursor_col, g_cursor_row, c, fg, bg);
        g_cursor_col++;
    }

    if (g_cursor_col >= g_max_cols) {
        g_cursor_col = 0;
        g_cursor_row++;
    }
    if (g_cursor_row >= g_max_rows) fb_scroll();
}

void fb_write(const char *str, u32 fg) {
    while (*str) fb_putchar(*str++, fg, FB_COLOR_DARK_BG);
}

void fb_puts_user(const char *s, usize len) {
    for (usize i = 0; i < len; i++) fb_putchar(s[i], FB_COLOR_WHITE, FB_COLOR_DARK_BG);
}

void fb_set_cursor(u32 col, u32 row) {
    g_cursor_col = col;
    g_cursor_row = row;
}

void fb_enable_text(bool enable) {
    g_fb_text_enabled = enable;
}

u32 fb_get_cols(void) { return g_max_cols; }
u32 fb_get_rows(void) { return g_max_rows; }

/* Boot UI */
static int g_logo_cx, g_logo_bottom;
static int g_progress_x, g_progress_y, g_progress_w, g_progress_h;

static u32 fb_mix(u32 dst, u32 src, u8 alpha) {
    u32 inv = 255u - alpha;
    u32 dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    u32 sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
    u32 r = (sr * alpha + dr * inv) / 255u;
    u32 g = (sg * alpha + dg * inv) / 255u;
    u32 b = (sb * alpha + db * inv) / 255u;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static u32 fb_dim_pixel(u32 c, u32 percent) {
    u32 r = ((c >> 16) & 0xFF) * percent / 100u;
    u32 g = ((c >> 8) & 0xFF) * percent / 100u;
    u32 b = (c & 0xFF) * percent / 100u;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static void fb_fill_rect_i(int x, int y, int w, int h, u32 color) {
    if (!g_pixels || w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > (int)g_fb->width ? (int)g_fb->width : x + w;
    int y1 = y + h > (int)g_fb->height ? (int)g_fb->height : y + h;
    for (int yy = y0; yy < y1; yy++) {
        u32 *row = g_pixels + yy * g_pitch4;
        for (int xx = x0; xx < x1; xx++) row[xx] = color;
    }
}

static void fb_fill_round_rect_i(int x, int y, int w, int h, int radius, u32 color) {
    if (!g_pixels || w <= 0 || h <= 0) return;
    if (radius < 1) {
        fb_fill_rect_i(x, y, w, h, color);
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > (int)g_fb->width ? (int)g_fb->width : x + w;
    int y1 = y + h > (int)g_fb->height ? (int)g_fb->height : y + h;
    int rr = radius * radius;
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            int dx = 0, dy = 0;
            if (xx < x + radius) dx = x + radius - xx;
            else if (xx >= x + w - radius) dx = xx - (x + w - radius - 1);
            if (yy < y + radius) dy = y + radius - yy;
            else if (yy >= y + h - radius) dy = yy - (y + h - radius - 1);
            if (dx && dy && dx * dx + dy * dy > rr) continue;
            g_pixels[yy * g_pitch4 + xx] = color;
        }
    }
}

static void fb_draw_text_px(int x, int y, const char *s, u32 color, u32 bg) {
    if (!s) return;
    while (*s) {
        fb_draw_char_at(g_pixels, g_pitch4, x, y, *s++, color, bg);
        x += FONT_W;
    }
}

static void fb_draw_text_center(int y, const char *s, u32 color) {
    int len = (int)strlen(s);
    int x = ((int)g_fb->width - len * FONT_W) / 2;
    fb_draw_text_px(x, y, s, color, 0x00000000);
}

static void fb_draw_boot_background(void *wallpaper_data) {
    if (wallpaper_data) {
        u32 *wp = (u32 *)wallpaper_data;
        u32 w_width = wp[0];
        u32 w_height = wp[1];
        u32 *w_pixels = &wp[2];

        for (u32 y = 0; y < g_fb->height; y++) {
            u32 src_y = (y * w_height) / g_fb->height;
            u32 *src_row = &w_pixels[src_y * w_width];
            u32 *dst_row = &g_pixels[y * g_pitch4];
            for (u32 x = 0; x < g_fb->width; x++) {
                u32 src_x = (x * w_width) / g_fb->width;
                u32 color = fb_dim_pixel(src_row[src_x], 38);
                int dx = (int)x - (int)g_fb->width / 2;
                int dy = (int)y - (int)g_fb->height / 2;
                u32 dist = (u32)((dx * dx + dy * dy) / 9000);
                u32 shade = dist > 42 ? 42 : dist;
                dst_row[x] = fb_mix(color, 0xFF030508, (u8)(85 + shade));
            }
        }
    } else {
        for (u32 y = 0; y < g_fb->height; y++) {
            u32 tone = 9 + (y * 10 / g_fb->height);
            u32 color = 0xFF000000 | (tone << 16) | ((tone + 1) << 8) | (tone + 4);
            for (u32 x = 0; x < g_fb->width; x++) g_pixels[y * g_pitch4 + x] = color;
        }
    }
}

void fb_draw_splash(void *wallpaper_data, void *logo_data) {
    if (!g_pixels) return;
    fb_draw_boot_background(wallpaper_data);

    g_logo_cx = (int)g_fb->width / 2;
    g_logo_bottom = (int)g_fb->height / 2;

    if (logo_data) {
        u32 *lp = (u32 *)logo_data;
        u32 l_width = lp[0];
        u32 l_height = lp[1];
        u32 *l_pixels = &lp[2];
        u32 target_h = g_fb->height * 14 / 100;
        if (target_h < 72) target_h = 72;
        if (target_h > 150) target_h = 150;
        u32 target_w = (l_width * target_h) / l_height;
        int start_x = g_logo_cx - (int)(target_w / 2);
        int start_y = (int)(g_fb->height * 40 / 100) - (int)(target_h / 2);

        g_logo_bottom = start_y + (int)target_h;
        for (u32 y = 0; y < target_h; y++) {
            u32 src_y = (y * l_height) / target_h;
            for (u32 x = 0; x < target_w; x++) {
                u32 src_x = (x * l_width) / target_w;
                int dst_x = start_x + (int)x;
                int dst_y = start_y + (int)y;
                if (dst_x < 0 || dst_x >= (int)g_fb->width || dst_y < 0 || dst_y >= (int)g_fb->height) continue;
                u32 src_pixel = l_pixels[src_y * l_width + src_x];
                u8 alpha = (src_pixel >> 24) & 0xFF;
                if (alpha == 255) g_pixels[dst_y * g_pitch4 + dst_x] = src_pixel;
                else if (alpha > 0) g_pixels[dst_y * g_pitch4 + dst_x] = fb_mix(g_pixels[dst_y * g_pitch4 + dst_x], src_pixel, alpha);
            }
        }
    } else {
        int mark_w = 118;
        int mark_h = 96;
        int x = g_logo_cx - mark_w / 2;
        int y = (int)(g_fb->height * 40 / 100) - mark_h / 2;
        fb_fill_round_rect_i(x + 26, y + 10, 66, 74, 22, 0xFFE9EEF7);
        fb_fill_round_rect_i(x + 6, y + 30, 44, 56, 20, 0xFFE9EEF7);
        fb_fill_round_rect_i(x + 68, y + 28, 44, 58, 20, 0xFFE9EEF7);
        fb_fill_rect_i(x + 52, y + 30, 14, 58, 0xFF07090D);
        fb_fill_round_rect_i(x + 66, y, 38, 20, 10, 0xFFE9EEF7);
        g_logo_bottom = y + mark_h;
    }

    fb_draw_text_center(g_logo_bottom + 34, "YamOS", 0xFFE8ECF4);
    fb_draw_text_center(g_logo_bottom + 56, "Graph-Based Adaptive OS", 0xFF8E96A3);

    g_progress_w = (int)g_fb->width / 5;
    if (g_progress_w < 180) g_progress_w = 180;
    if (g_progress_w > 320) g_progress_w = 320;
    g_progress_h = 6;
    g_progress_x = ((int)g_fb->width - g_progress_w) / 2;
    g_progress_y = g_logo_bottom + 92;

    fb_fill_round_rect_i(g_progress_x, g_progress_y, g_progress_w, g_progress_h, 3, 0xFF2B3038);
    fb_fill_round_rect_i(g_progress_x, g_progress_y, 2, g_progress_h, 3, 0xFFE8ECF4);
}

void fb_draw_spinner(int frame) {
    if (!g_pixels) return;
    if (frame < 0) frame = 0;
    if (frame > 29) frame = 29;
    int fill = (g_progress_w * (frame + 1)) / 30;
    fb_fill_round_rect_i(g_progress_x, g_progress_y, g_progress_w, g_progress_h, 3, 0xFF2B3038);
    fb_fill_round_rect_i(g_progress_x, g_progress_y, fill, g_progress_h, 3, 0xFFE8ECF4);
}

u32 *fb_get_pixels(void) { return g_pixels; }
u32  fb_get_width(void)  { return g_fb ? (u32)g_fb->width  : 0; }
u32  fb_get_height(void) { return g_fb ? (u32)g_fb->height : 0; }
u32  fb_get_pitch(void)  { return g_pitch4 * 4; }
