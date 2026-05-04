#ifndef _WL_DRAW_H
#define _WL_DRAW_H

#include <nexus/types.h>
#include "compositor.h"

void wl_draw_rect(wl_surface_t *s, i32 x, i32 y, u32 w, u32 h, u32 color);
void wl_draw_rect_outline(wl_surface_t *s, i32 x, i32 y, u32 w, u32 h, u32 color);
void wl_draw_rounded_rect(wl_surface_t *s, i32 x, i32 y, u32 w, u32 h, u32 radius, u32 color);
void wl_draw_rounded_outline(wl_surface_t *s, i32 x, i32 y, u32 w, u32 h, u32 radius, u32 color);
void wl_draw_vgradient(wl_surface_t *s, i32 x, i32 y, u32 w, u32 h, u32 top, u32 bottom);
void wl_draw_char(wl_surface_t *s, i32 x, i32 y, char c, u32 fg, u32 bg);
void wl_draw_text(wl_surface_t *s, i32 x, i32 y, const char *str, u32 fg, u32 bg);
void wl_draw_text_shadow(wl_surface_t *s, i32 x, i32 y, const char *str, u32 fg, u32 shadow);
void wl_draw_filled_circle(wl_surface_t *s, i32 cx, i32 cy, u32 radius, u32 color);

#endif
