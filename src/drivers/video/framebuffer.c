/* ============================================================================
 * YamKernel — Framebuffer Driver (Optimized)
 * Software text rendering using an embedded 8x16 bitmap font
 * ============================================================================ */

#include "framebuffer.h"
#include "../../lib/string.h"
#include "../../lib/font.h"

/* ---- Embedded 8x16 bitmap font (CP437 subset: ASCII 32-126) ---- */

#define FONT_W 8
#define FONT_H 16



/* ---- Framebuffer state ---- */

static struct limine_framebuffer *g_fb = NULL;
static u32 *g_pixels = NULL;     /* Cached pixel pointer — avoid dereferencing every call */
static u32 g_pitch4 = 0;         /* Cached pitch/4 — used in every draw */
static u32 g_cursor_col = 0;
static u32 g_cursor_row = 0;
static u32 g_max_cols = 0;
static u32 g_max_rows = 0;
static void *g_wallpaper = NULL;
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
    /* Use 32-bit fill — much faster than per-pixel loop for solid colors */
    u64 total = (u64)g_pitch4 * g_fb->height;
    u32 *p = g_pixels;
    u32 *end = p + total;
    /* Unrolled 8x fill loop for speed */
    while (p + 8 <= end) {
        p[0] = color; p[1] = color; p[2] = color; p[3] = color;
        p[4] = color; p[5] = color; p[6] = color; p[7] = color;
        p += 8;
    }
    while (p < end) {
        *p++ = color;
    }
    g_cursor_col = 0;
    g_cursor_row = 0;
}

/* OPTIMIZED: Writes directly to the framebuffer scanline pointer
 * instead of calling fb_put_pixel 128 times per character */
static void fb_draw_char(u32 col, u32 row, char c, u32 fg, u32 bg) {
    if (!g_pixels) return;
    int idx = (int)c - 32;
    if (idx < 0 || idx >= 95) idx = 0; /* Fallback to space */

    u32 x0 = col * FONT_W;
    u32 y0 = row * FONT_H;

    for (int y = 0; y < FONT_H; y++) {
        u8 row_bits = yam_font_data[idx][y];
        u32 *scanline = g_pixels + (y0 + y) * g_pitch4 + x0;
        /* Unrolled 8-pixel row write */
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

    /* Move everything up by one character row — memmove handles overlap */
    u64 copy_size = (u64)g_pitch4 * (g_fb->height - line_height) * sizeof(u32);
    memmove(g_pixels, g_pixels + g_pitch4 * line_height, copy_size);

    /* Clear the last row */
    u32 *clear_ptr = g_pixels + g_pitch4 * (g_fb->height - line_height);
    u64 clear_count = (u64)g_pitch4 * line_height;
    for (u64 i = 0; i < clear_count; i++) {
        clear_ptr[i] = FB_COLOR_DARK_BG;
    }

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

    /* Wrap */
    if (g_cursor_col >= g_max_cols) {
        g_cursor_col = 0;
        g_cursor_row++;
    }

    /* Scroll if needed */
    if (g_cursor_row >= g_max_rows) {
        fb_scroll();
    }
}

void fb_write(const char *str, u32 fg) {
    while (*str) {
        fb_putchar(*str++, fg, FB_COLOR_DARK_BG);
    }
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
/* ============================================================================
 * Splash Screen / Boot UI
 * ============================================================================ */

/* Stored logo position so spinner can be drawn relative to it */
static int g_logo_cx, g_logo_bottom;

void fb_draw_splash(void *wallpaper_data, void *logo_data) {
    if (!g_pixels) return;
    g_wallpaper = wallpaper_data;

    /* 1. Draw Wallpaper (Nearest-Neighbor Scaling — row-optimized) */
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
                dst_row[x] = src_row[src_x];
            }
        }
    } else {
        fb_clear(FB_COLOR_BLACK);
    }

    /* 2. Draw Logo — Center-Left position, scaled to ~12% of screen height */
    g_logo_cx = (int)(g_fb->width * 15 / 100);  /* 15% from left edge */
    g_logo_bottom = g_fb->height / 2;            /* default */

    if (logo_data) {
        u32 *lp = (u32 *)logo_data;
        u32 l_width = lp[0];
        u32 l_height = lp[1];
        u32 *l_pixels = &lp[2];

        /* Target: ~12% of screen height, keep aspect ratio */
        u32 target_h = g_fb->height * 12 / 100;
        if (target_h < 40) target_h = 40;
        u32 target_w = (l_width * target_h) / l_height;

        /* Center-left: x at 15% of screen, y vertically centered */
        int start_x = g_logo_cx - (int)(target_w / 2);
        int start_y = (int)(g_fb->height / 2) - (int)(target_h / 2);

        g_logo_bottom = start_y + (int)target_h;

        for (u32 y = 0; y < target_h; y++) {
            u32 src_y = (y * l_height) / target_h;
            for (u32 x = 0; x < target_w; x++) {
                u32 src_x = (x * l_width) / target_w;
                int dst_x = start_x + (int)x;
                int dst_y = start_y + (int)y;
                
                if (dst_x >= 0 && dst_x < (int)g_fb->width && dst_y >= 0 && dst_y < (int)g_fb->height) {
                    u32 src_pixel = l_pixels[src_y * l_width + src_x];
                    u8 alpha = (src_pixel >> 24) & 0xFF;
                    
                    if (alpha == 255) {
                        g_pixels[dst_y * g_pitch4 + dst_x] = src_pixel;
                    } else if (alpha > 0) {
                        u32 dst_pixel = g_pixels[dst_y * g_pitch4 + dst_x];
                        u8 dr = (dst_pixel >> 16) & 0xFF;
                        u8 dg = (dst_pixel >> 8) & 0xFF;
                        u8 db = dst_pixel & 0xFF;
                        u8 sr = (src_pixel >> 16) & 0xFF;
                        u8 sg = (src_pixel >> 8) & 0xFF;
                        u8 sb = src_pixel & 0xFF;
                        u8 r = (sr * alpha + dr * (255 - alpha)) / 255;
                        u8 g = (sg * alpha + dg * (255 - alpha)) / 255;
                        u8 b = (sb * alpha + db * (255 - alpha)) / 255;
                        g_pixels[dst_y * g_pitch4 + dst_x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
                    }
                }
            }
        }
    }
}

/* Spinner — 12 positions in a circle, drawn below the logo */
static const int spinner_offx[12] = { 0, 10, 17, 20, 17, 10, 0, -10, -17, -20, -17, -10 };
static const int spinner_offy[12] = { -20, -17, -10, 0, 10, 17, 20, 17, 10, 0, -10, -17 };

void fb_draw_spinner(int frame) {
    if (!g_pixels || !g_wallpaper) return;
    
    /* Spinner center: below the logo, horizontally aligned with logo */
    int cx = g_logo_cx;
    int cy = g_logo_bottom + 40;  /* 40px below logo bottom edge */
    
    u32 *wp = (u32 *)g_wallpaper;
    u32 w_width = wp[0];
    u32 w_height = wp[1];
    u32 *w_pixels = &wp[2];

    /* Erase 50x50 area by restoring wallpaper pixels */
    for (int y = -25; y <= 25; y++) {
        u32 dy = (u32)(cy + y);
        if (dy >= g_fb->height) continue;
        u32 sy = (dy * w_height) / g_fb->height;
        for (int x = -25; x <= 25; x++) {
            u32 dx = (u32)(cx + x);
            if (dx >= g_fb->width) continue;
            u32 sx = (dx * w_width) / g_fb->width;
            g_pixels[dy * g_pitch4 + dx] = w_pixels[sy * w_width + sx];
        }
    }

    /* Draw 5-dot tail with fading brightness */
    for (int i = 0; i < 5; i++) {
        int idx = (frame - i + 12) % 12;
        int dx = cx + spinner_offx[idx];
        int dy = cy + spinner_offy[idx];
        
        /* Fade: lead dot is bright white, tail fades to gray */
        u32 brightness = 255 - (i * 40);
        u32 color = (0xFF << 24) | (brightness << 16) | (brightness << 8) | brightness;
        
        /* Draw dot (radius ~2.5px) */
        for (int py = -2; py <= 2; py++) {
            for (int px = -2; px <= 2; px++) {
                if (px*px + py*py <= 5) {
                    u32 fy = (u32)(dy + py);
                    u32 fx = (u32)(dx + px);
                    if (fy < g_fb->height && fx < g_fb->width) {
                        g_pixels[fy * g_pitch4 + fx] = color;
                    }
                }
            }
        }
    }
}

/* ---- Raw access for DRM/Wayland ---- */
u32 *fb_get_pixels(void) { return g_pixels; }
u32  fb_get_width(void)  { return g_fb ? (u32)g_fb->width  : 0; }
u32  fb_get_height(void) { return g_fb ? (u32)g_fb->height : 0; }
u32  fb_get_pitch(void)  { return g_pitch4 * 4; }
