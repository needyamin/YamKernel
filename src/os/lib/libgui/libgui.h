#ifndef _LIBGUI_H
#define _LIBGUI_H

#include <nexus/types.h>

#define WL_EVENT_KEYPRESS   1
#define WL_EVENT_MOUSE_MOVE 2
#define WL_EVENT_MOUSE_BTN  3
#define WL_EVENT_EXPOSE     4

typedef struct {
    u32 type;
    u32 x, y;
    u32 keycode;
    u32 state;
} gui_event_t;

typedef struct {
    int id;
    u32 width;
    u32 height;
    u32 *pixels;
    u32 bg_color;
} gui_window_t;

/* Initialize the GUI library (connects to compositor) */
void gui_init(void);

/* Create a new window */
gui_window_t *gui_create_window(u32 width, u32 height, u32 bg_color);

/* Draw pixels to screen */
void gui_commit(gui_window_t *win);

/* Poll for an event (blocking or non-blocking) */
bool gui_poll_event(gui_window_t *win, gui_event_t *ev, bool block);

/* Drawing primitives */
void gui_draw_rect(gui_window_t *win, int x, int y, int w, int h, u32 color);
void gui_fill_rect(gui_window_t *win, int x, int y, int w, int h, u32 color);
void gui_draw_text(gui_window_t *win, int x, int y, const char *text, u32 color);

/* Glassmorphism / Themes */
void gui_blur_rect(gui_window_t *win, int x, int y, int w, int h, int radius);

#endif
