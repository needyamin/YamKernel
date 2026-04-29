#include "libgui.h"
#include "../libc/stdlib.h"
#include "../libc/string.h"
#include "../libyam/syscall.h"

void gui_init(void) {
    /* Any global initialization */
}

gui_window_t *gui_create_window(u32 width, u32 height, u32 bg_color) {
    gui_window_t *win = malloc(sizeof(gui_window_t));
    win->width = width;
    win->height = height;
    win->bg_color = bg_color;
    
    /* Syscall to ask compositor for a surface ID */
    win->id = syscall2(SYS_WL_CREATE_SURFACE, width, height);
    
    /* Syscall to map the compositor's shared memory buffer into our address space */
    win->pixels = (u32 *)syscall1(SYS_WL_MAP_BUFFER, win->id);
    
    if (bg_color != 0) {
        gui_fill_rect(win, 0, 0, width, height, bg_color);
    }
    
    return win;
}

void gui_commit(gui_window_t *win) {
    /* Tell compositor the buffer is ready to draw */
    syscall1(SYS_WL_COMMIT, win->id);
}

bool gui_poll_event(gui_window_t *win, gui_event_t *ev, bool block) {
    u64 ret = syscall3(SYS_WL_POLL_EVENT, win->id, (u64)ev, block ? 1 : 0);
    return ret == 1;
}

void gui_draw_rect(gui_window_t *win, int x, int y, int w, int h, u32 color) {
    if (x < 0 || y < 0 || x + w > (int)win->width || y + h > (int)win->height) return;
    for (int i = 0; i < w; i++) {
        win->pixels[y * win->width + (x + i)] = color;
        win->pixels[(y + h - 1) * win->width + (x + i)] = color;
    }
    for (int i = 0; i < h; i++) {
        win->pixels[(y + i) * win->width + x] = color;
        win->pixels[(y + i) * win->width + (x + w - 1)] = color;
    }
}

void gui_fill_rect(gui_window_t *win, int x, int y, int w, int h, u32 color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)win->width) w = win->width - x;
    if (y + h > (int)win->height) h = win->height - y;
    
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            win->pixels[(y + i) * win->width + (x + j)] = color;
        }
    }
}

/* Very simple blur for glassmorphism */
void gui_blur_rect(gui_window_t *win, int x, int y, int w, int h, int radius) {
    /* Fast box blur algorithm over the pixels */
    /* ... omitted full implementation for brevity, this is a scaffold ... */
    (void)win; (void)x; (void)y; (void)w; (void)h; (void)radius;
}

/* Simple text rendering (will be hooked to a font array) */
extern const u8 font8x8_basic[128][8]; /* Defined in font_data.c */

void gui_draw_text(gui_window_t *win, int x, int y, const char *text, u32 color) {
    int cur_x = x;
    while (*text) {
        char c = *text++;
        if ((u8)c < 128) {
            const u8 *glyph = font8x8_basic[(int)(u8)c];
            for (int row = 0; row < 8; row++) {
                for (int col = 0; col < 8; col++) {
                    if (glyph[row] & (1 << col)) {
                        if (cur_x + col < (int)win->width && y + row < (int)win->height)
                            win->pixels[(y + row) * win->width + (cur_x + col)] = color;
                    }
                }
            }
        }
        cur_x += 8;
    }
}
