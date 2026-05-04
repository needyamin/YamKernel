/* ============================================================================
 * YamKernel — Wayland Client Graphics Primitives
 * ============================================================================ */
#include "wl_draw.h"
#include "../lib/font.h"

void wl_draw_rect(wl_surface_t *s, i32 x, i32 y, u32 w, u32 h, u32 color) {
    if (!s || !s->buffer || !s->buffer->pixels) return;
    u32 *pixels = s->buffer->pixels;
    
    i32 end_x = x + (i32)w;
    i32 end_y = y + (i32)h;
    
    /* Clamp */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (end_x > (i32)s->width) end_x = (i32)s->width;
    if (end_y > (i32)s->height) end_y = (i32)s->height;
    if (x >= end_x || y >= end_y) return;
    
    for (i32 cy = y; cy < end_y; cy++) {
        for (i32 cx = x; cx < end_x; cx++) {
            pixels[cy * s->width + cx] = color;
        }
    }
}

void wl_draw_rect_outline(wl_surface_t *s, i32 x, i32 y, u32 w, u32 h, u32 color) {
    if (!s || w == 0 || h == 0) return;
    wl_draw_rect(s, x, y, w, 1, color);
    wl_draw_rect(s, x, y + (i32)h - 1, w, 1, color);
    wl_draw_rect(s, x, y, 1, h, color);
    wl_draw_rect(s, x + (i32)w - 1, y, 1, h, color);
}

static bool round_mask(i32 px, i32 py, u32 w, u32 h, u32 r) {
    if (r == 0) return true;
    if (px >= (i32)r && px < (i32)w - (i32)r) return true;
    if (py >= (i32)r && py < (i32)h - (i32)r) return true;

    i32 cx = px < (i32)r ? (i32)r : (i32)w - (i32)r - 1;
    i32 cy = py < (i32)r ? (i32)r : (i32)h - (i32)r - 1;
    i32 dx = px - cx;
    i32 dy = py - cy;
    return dx * dx + dy * dy <= (i32)(r * r);
}

void wl_draw_rounded_rect(wl_surface_t *s, i32 x, i32 y, u32 w, u32 h, u32 r, u32 color) {
    if (!s || !s->buffer || !s->buffer->pixels || w == 0 || h == 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    for (u32 py = 0; py < h; py++) {
        i32 sy = y + (i32)py;
        if (sy < 0 || sy >= (i32)s->height) continue;
        for (u32 px = 0; px < w; px++) {
            i32 sx = x + (i32)px;
            if (sx < 0 || sx >= (i32)s->width) continue;
            if (round_mask((i32)px, (i32)py, w, h, r)) {
                s->buffer->pixels[sy * s->width + sx] = color;
            }
        }
    }
}

void wl_draw_rounded_outline(wl_surface_t *s, i32 x, i32 y, u32 w, u32 h, u32 r, u32 color) {
    if (!s || !s->buffer || !s->buffer->pixels || w < 2 || h < 2) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (u32 py = 0; py < h; py++) {
        i32 sy = y + (i32)py;
        if (sy < 0 || sy >= (i32)s->height) continue;
        for (u32 px = 0; px < w; px++) {
            i32 sx = x + (i32)px;
            if (sx < 0 || sx >= (i32)s->width) continue;
            bool outer = round_mask((i32)px, (i32)py, w, h, r);
            bool inner = false;
            if (px > 0 && py > 0 && px + 1 < w && py + 1 < h) {
                inner = round_mask((i32)px - 1, (i32)py - 1, w - 2, h - 2, r > 1 ? r - 1 : 0);
            }
            if (outer && !inner) s->buffer->pixels[sy * s->width + sx] = color;
        }
    }
}

static u32 mix_color(u32 a, u32 b, u32 t, u32 max) {
    u32 ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    u32 br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    u32 r = (ar * (max - t) + br * t) / max;
    u32 g = (ag * (max - t) + bg * t) / max;
    u32 bl = (ab * (max - t) + bb * t) / max;
    return 0xFF000000 | (r << 16) | (g << 8) | bl;
}

void wl_draw_vgradient(wl_surface_t *s, i32 x, i32 y, u32 w, u32 h, u32 top, u32 bottom) {
    if (!s || h == 0) return;
    for (u32 row = 0; row < h; row++) {
        u32 c = mix_color(top, bottom, row, h - 1 ? h - 1 : 1);
        wl_draw_rect(s, x, y + (i32)row, w, 1, c);
    }
}

void wl_draw_char(wl_surface_t *s, i32 x, i32 y, char c, u32 fg, u32 bg) {
    if (!s || !s->buffer || !s->buffer->pixels) return;
    
    if (c < 32 || c > 126) c = '?';
    u32 idx = c - 32;
    u32 *pixels = s->buffer->pixels;
    
    for (int row = 0; row < FONT_H; row++) {
        u8 row_bits = yam_font_data[idx][row];
        i32 py = y + row;
        if (py < 0 || py >= (i32)s->height) continue;
        
        for (int col = 0; col < FONT_W; col++) {
            i32 px = x + col;
            if (px < 0 || px >= (i32)s->width) continue;
            
            if (row_bits & (0x80 >> col)) {
                pixels[py * s->width + px] = fg;
            } else if (bg != 0) { /* 0 = transparent background */
                pixels[py * s->width + px] = bg;
            }
        }
    }
}

void wl_draw_text(wl_surface_t *s, i32 x, i32 y, const char *str, u32 fg, u32 bg) {
    if (!str) return;
    i32 start_x = x;
    while (*str) {
        if (*str == '\n') {
            y += FONT_H;
            x = start_x;
        } else {
            wl_draw_char(s, x, y, *str, fg, bg);
            x += FONT_W;
        }
        str++;
    }
}

void wl_draw_text_shadow(wl_surface_t *s, i32 x, i32 y, const char *str, u32 fg, u32 shadow) {
    if (!str) return;
    /* Draw shadow offset */
    wl_draw_text(s, x + 1, y + 1, str, shadow, 0);
    /* Draw foreground text */
    wl_draw_text(s, x, y, str, fg, 0);
}

void wl_draw_filled_circle(wl_surface_t *s, i32 cx, i32 cy, u32 r, u32 color) {
    if (!s || !s->buffer || !s->buffer->pixels) return;
    u32 *pixels = s->buffer->pixels;
    u32 sw = s->width;
    u32 sh = s->height;

    i32 r2 = (i32)(r * r);
    for (i32 y = -(i32)r; y <= (i32)r; y++) {
        for (i32 x = -(i32)r; x <= (i32)r; x++) {
            if (x*x + y*y <= r2) {
                i32 px = cx + x;
                i32 py = cy + y;
                if (px >= 0 && px < (i32)sw && py >= 0 && py < (i32)sh) {
                    pixels[py * sw + px] = color;
                }
            }
        }
    }
}
