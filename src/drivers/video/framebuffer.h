/* ============================================================================
 * YamKernel — Framebuffer Driver
 * Renders text to Limine-provided framebuffer
 * ============================================================================ */

#ifndef _DRIVERS_FRAMEBUFFER_H
#define _DRIVERS_FRAMEBUFFER_H

#include <nexus/types.h>
#include <limine.h>

/* Colors (32-bit ARGB) */
#define FB_COLOR_BLACK      0xFF000000
#define FB_COLOR_WHITE      0xFFFFFFFF
#define FB_COLOR_GREEN      0xFF00FF88
#define FB_COLOR_CYAN       0xFF00DDFF
#define FB_COLOR_YELLOW     0xFFFFDD00
#define FB_COLOR_RED        0xFFFF3333
#define FB_COLOR_ORANGE     0xFFFF8833
#define FB_COLOR_BLUE       0xFF4488FF
#define FB_COLOR_DARK_BG    0xFF0A0A14
#define FB_COLOR_GRAY       0xFF888888

void fb_init(struct limine_framebuffer *fb);
void fb_clear(u32 color);
void fb_putchar(char c, u32 fg, u32 bg);
void fb_write(const char *str, u32 fg);
void fb_set_cursor(u32 col, u32 row);
void fb_scroll(void);
void fb_put_pixel(u32 x, u32 y, u32 color);
void fb_draw_splash(void *wallpaper_data, void *logo_data);
void fb_draw_spinner(int frame);
void fb_enable_text(bool enable);

/* Current framebuffer dimensions in characters */
u32  fb_get_cols(void);
u32  fb_get_rows(void);

#endif /* _DRIVERS_FRAMEBUFFER_H */
