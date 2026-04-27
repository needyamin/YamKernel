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
