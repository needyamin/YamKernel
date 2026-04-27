#ifndef _USER_WL_DRAW_H
#define _USER_WL_DRAW_H

#include "yam.h"

/* Simple font data (8x16) - typically you'd link this or include it */
extern const uint8_t font_basic_8x16[128][16];

typedef struct {
    u32 *pixels;
    u32  width;
    u32  height;
} wl_user_buffer_t;

static inline void wl_user_draw_rect(wl_user_buffer_t *buf, i32 x, i32 y, u32 w, u32 h, u32 color) {
    for (u32 i = 0; i < h; i++) {
        i32 py = y + i;
        if (py < 0 || (u32)py >= buf->height) continue;
        for (u32 j = 0; j < w; j++) {
            i32 px = x + j;
            if (px < 0 || (u32)px >= buf->width) continue;
            buf->pixels[py * buf->width + px] = color;
        }
    }
}

static inline void wl_user_draw_char(wl_user_buffer_t *buf, i32 x, i32 y, char c, u32 color) {
    if ((uint8_t)c > 127) return;
    const uint8_t *glyph = font_basic_8x16[(int)c];
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 8; j++) {
            if (glyph[i] & (1 << (7 - j))) {
                i32 px = x + j;
                i32 py = y + i;
                if (px >= 0 && (u32)px < buf->width && py >= 0 && (u32)py < buf->height) {
                    buf->pixels[py * buf->width + px] = color;
                }
            }
        }
    }
}

static inline void wl_user_draw_text(wl_user_buffer_t *buf, i32 x, i32 y, const char *s, u32 color) {
    int cur_x = x;
    while (*s) {
        wl_user_draw_char(buf, cur_x, y, *s++, color);
        cur_x += 8;
    }
}

/* Minimal ksnprintf for userspace */
static inline void utoa(char *buf, int n) {
    if (n == 0) { buf[0] = '0'; buf[1] = 0; return; }
    int i = 0, sign = n;
    if (n < 0) n = -n;
    while (n > 0) { buf[i++] = (n % 10) + '0'; n /= 10; }
    if (sign < 0) buf[i++] = '-';
    buf[i] = 0;
    /* Reverse */
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i - j - 1]; buf[i - j - 1] = t;
    }
}

#endif
