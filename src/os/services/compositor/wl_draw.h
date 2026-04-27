/* ============================================================================
 * YamKernel — Wayland Client Graphics Primitives
 * ============================================================================ */
#ifndef _WL_DRAW_H
#define _WL_DRAW_H

#include <nexus/types.h>
#include "compositor.h"

/* Fill a rectangle with a solid ARGB color */
void wl_draw_rect(wl_surface_t *s, i32 x, i32 y, u32 w, u32 h, u32 color);

/* Draw a single character using the embedded font */
void wl_draw_char(wl_surface_t *s, i32 x, i32 y, char c, u32 fg, u32 bg);

/* Draw a null-terminated string */
void wl_draw_text(wl_surface_t *s, i32 x, i32 y, const char *str, u32 fg, u32 bg);

#endif /* _WL_DRAW_H */
