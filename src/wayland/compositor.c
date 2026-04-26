/* ============================================================================
 * YamKernel — Wayland Compositor Implementation
 * Handles window composition, input routing, and rendering.
 * ============================================================================ */
#include "compositor.h"
#include "../drivers/input/evdev.h"
#include "../drivers/video/framebuffer.h"
#include "../sched/sched.h"
#include "../sched/wait.h"
#include "../lib/string.h"
#include "../lib/kprintf.h"
#include "../mem/heap.h"
#include <nexus/panic.h>

extern void *g_wallpaper_module;

static wl_compositor_t g_compositor;
static u32 g_next_surface_id = 1;

void wl_compositor_init(void) {
    memset(&g_compositor, 0, sizeof(wl_compositor_t));
    
    drm_mode_t mode = drm_get_mode();
    g_compositor.display_w = mode.width;
    g_compositor.display_h = mode.height;
    
    g_compositor.scanout = drm_create_dumb_buffer(mode.width, mode.height);
    if (!g_compositor.scanout) {
        kpanic("Wayland Compositor: Failed to allocate scanout buffer");
    }
    
    g_compositor.cursor_x = mode.width / 2;
    g_compositor.cursor_y = mode.height / 2;
    g_compositor.cursor_visible = true;
    g_compositor.running = true;

    kprintf_color(0xFF00DDFF, "[WAYLAND] Compositor initialized at %ux%u\n",
                  mode.width, mode.height);
}

wl_compositor_t *wl_get_compositor(void) {
    return &g_compositor;
}

wl_surface_t *wl_surface_create(const char *title, i32 x, i32 y, u32 w, u32 h, u64 owner) {
    if (g_compositor.surface_count >= WL_MAX_SURFACES) return NULL;
    
    /* Find free slot */
    wl_surface_t *s = NULL;
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (g_compositor.surfaces[i].state == WL_SURFACE_FREE) {
            s = &g_compositor.surfaces[i];
            break;
        }
    }
    if (!s) return NULL;

    s->id = g_next_surface_id++;
    s->state = WL_SURFACE_ACTIVE;
    strncpy(s->title, title, WL_TITLE_MAX - 1);
    s->title[WL_TITLE_MAX - 1] = '\0';
    s->x = x;
    s->y = y;
    s->width = w;
    s->height = h;
    s->owner_task_id = owner;
    
    /* Default styling */
    s->border_color = 0xFF4488FF;  /* Yam Blue */
    s->title_bg     = 0xFF222233;
    s->title_fg     = 0xFFFFFFFF;
    s->focused      = false;

    s->buffer = drm_create_dumb_buffer(w, h);
    if (!s->buffer) {
        s->state = WL_SURFACE_FREE;
        return NULL;
    }

    /* Auto-focus the new surface */
    wl_surface_focus(s);

    g_compositor.surface_count++;
    
    kprintf_color(0xFF00DDFF, "[WAYLAND] Created surface %u ('%s') %ux%u for task %lu\n",
                  s->id, s->title, w, h, owner);
    return s;
}

void wl_surface_destroy(wl_surface_t *surface) {
    if (!surface || surface->state == WL_SURFACE_FREE) return;
    
    if (surface->buffer) {
        drm_destroy_dumb_buffer(surface->buffer);
        surface->buffer = NULL;
    }
    
    if (g_compositor.focused_id == surface->id) {
        g_compositor.focused_id = 0;
        /* Focus the highest active surface (if any) */
        for (int i = WL_MAX_SURFACES - 1; i >= 0; i--) {
            if (g_compositor.surfaces[i].state == WL_SURFACE_ACTIVE && 
                g_compositor.surfaces[i].id != surface->id) {
                wl_surface_focus(&g_compositor.surfaces[i]);
                break;
            }
        }
    }

    surface->state = WL_SURFACE_FREE;
    g_compositor.surface_count--;
}

void wl_surface_focus(wl_surface_t *surface) {
    if (!surface) return;
    
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (g_compositor.surfaces[i].state != WL_SURFACE_FREE) {
            g_compositor.surfaces[i].focused = false;
        }
    }
    
    surface->focused = true;
    g_compositor.focused_id = surface->id;
    
    /* In a real compositor, we'd move it to the end of the list (z-order) 
     * For now, we'll just draw focused windows last during composition */
}

u32 *wl_surface_get_pixels(wl_surface_t *surface) {
    if (!surface || !surface->buffer) return NULL;
    return surface->buffer->pixels;
}

void wl_surface_commit(wl_surface_t *surface) {
    (void)surface;
    /* In our simple kernel-mode compositor, we redraw every frame anyway, 
       so commit is currently a no-op. In a real Wayland setup, this would
       trigger damage tracking and schedule a repaint. */
}

void wl_surface_push_event(wl_surface_t *surface, input_event_t ev) {
    if (!surface) return;
    u32 next_head = (surface->event_queue.head + 1) % 32;
    if (next_head != surface->event_queue.tail) {
        surface->event_queue.events[surface->event_queue.head] = ev;
        surface->event_queue.head = next_head;
    }
}

bool wl_surface_pop_event(wl_surface_t *surface, input_event_t *ev) {
    if (!surface || surface->event_queue.head == surface->event_queue.tail) return false;
    *ev = surface->event_queue.events[surface->event_queue.tail];
    surface->event_queue.tail = (surface->event_queue.tail + 1) % 32;
    return true;
}

static void composite_surface(wl_surface_t *s) {
    if (s->state != WL_SURFACE_ACTIVE || !s->buffer) return;

    u32 *dst = g_compositor.scanout->pixels;
    u32 *src = s->buffer->pixels;
    u32 sw = s->width;
    u32 sh = s->height;
    u32 dw = g_compositor.scanout->width;
    u32 dh = g_compositor.scanout->height;
    
    /* Bounding box */
    i32 start_y = s->y < 0 ? 0 : s->y;
    i32 start_x = s->x < 0 ? 0 : s->x;
    i32 end_y = s->y + sh > dh ? dh : s->y + sh;
    i32 end_x = s->x + sw > dw ? dw : s->x + sw;

    /* Window decorations (border) */
    u32 border = s->focused ? s->border_color : 0xFF555555;
    u32 title_h = 24;
    u32 border_w = 2;
    
    for (i32 y = start_y; y < end_y; y++) {
        u32 sy = y - s->y;
        i32 x = start_x;
        
        if (sy < title_h || sy >= sh - border_w) {
            /* Entire row is border or titlebar */
            for (; x < end_x; x++) {
                u32 sx = x - s->x;
                u32 color = border;
                if (sy >= border_w && sy < title_h && sx >= border_w && sx < sw - border_w) {
                    color = s->title_bg;
                    if (sx > sw - 20 && sx < sw - 5 && sy > 5 && sy < 20) {
                        if (sx - (sw - 20) == sy - 5 || sx - (sw - 20) == 15 - (sy - 5)) color = 0xFFFFFFFF;
                        else color = 0xFFFF3333;
                    }
                }
                dst[y * dw + x] = color;
            }
        } else {
            /* Content row with side borders */
            /* Left border */
            for (; x < s->x + (i32)border_w && x < end_x; x++) {
                dst[y * dw + x] = border;
            }
            
            /* Content (use memcpy for speed) */
            i32 content_end_x = s->x + (i32)sw - (i32)border_w;
            if (content_end_x > end_x) content_end_x = end_x;
            
            if (x < content_end_x) {
                u32 copy_w = content_end_x - x;
                u32 sx = x - s->x;
                memcpy(&dst[y * dw + x], &src[sy * sw + sx], copy_w * 4);
                x += copy_w;
            }
            
            /* Right border */
            for (; x < end_x; x++) {
                dst[y * dw + x] = border;
            }
        }
    }
}

static void composite_cursor(void) {
    if (!g_compositor.cursor_visible) return;
    
    u32 *dst = g_compositor.scanout->pixels;
    u32 dw = g_compositor.scanout->width;
    u32 dh = g_compositor.scanout->height;
    
    i32 cx = g_compositor.cursor_x;
    i32 cy = g_compositor.cursor_y;
    
    /* Draw a simple arrow cursor */
    const char cursor_map[15][12] = {
        "X           ",
        "XX          ",
        "X.X         ",
        "X..X        ",
        "X...X       ",
        "X....X      ",
        "X.....X     ",
        "X......X    ",
        "X.......X   ",
        "X........X  ",
        "X.....XXXXX ",
        "X..X..X     ",
        "X.X X..X    ",
        "XX  X..X    ",
        "X    XX     "
    };
    
    for (int y = 0; y < 15; y++) {
        for (int x = 0; x < 12; x++) {
            i32 px = cx + x;
            i32 py = cy + y;
            if (px >= 0 && px < (i32)dw && py >= 0 && py < (i32)dh) {
                if (cursor_map[y][x] == 'X') {
                    dst[py * dw + px] = 0xFF000000; /* Black outline */
                } else if (cursor_map[y][x] == '.') {
                    dst[py * dw + px] = 0xFFFFFFFF; /* White fill */
                }
            }
        }
    }
}

static void process_input(void) {
    input_event_t ev;
    while (evdev_pop_event(&ev)) {
        if (ev.type == EV_REL) {
            if (ev.code == REL_X) g_compositor.cursor_x += ev.value;
            if (ev.code == REL_Y) g_compositor.cursor_y += ev.value;
            
            /* Clamp to screen */
            if (g_compositor.cursor_x < 0) g_compositor.cursor_x = 0;
            if (g_compositor.cursor_x >= (i32)g_compositor.display_w) g_compositor.cursor_x = g_compositor.display_w - 1;
            if (g_compositor.cursor_y < 0) g_compositor.cursor_y = 0;
            if (g_compositor.cursor_y >= (i32)g_compositor.display_h) g_compositor.cursor_y = g_compositor.display_h - 1;
        } 
        else if (ev.type == EV_KEY) {
            /* Mouse button scancodes from PS/2: code=0x01 is Escape but we
               receive mouse clicks as special codes. The PS/2 mouse driver
               pushes scancode 0x110 (BTN_LEFT) for mouse clicks.
               Regular keyboard scancodes are 0x01-0x58.
               We distinguish: if cursor is over a surface AND it's a low scancode
               we check if it's a mouse event (from mouse driver) vs keyboard. 
               Simple heuristic: mouse driver uses code >= 0x110. */
            
            bool is_mouse_btn = (ev.code >= 0x110);
            
            if (is_mouse_btn && (ev.value == KEY_PRESSED || ev.value == KEY_RELEASED)) {
                /* Mouse click — route to surface under cursor */
                for (int i = WL_MAX_SURFACES - 1; i >= 0; i--) {
                    wl_surface_t *s = &g_compositor.surfaces[i];
                    if (s->state == WL_SURFACE_ACTIVE) {
                        if (g_compositor.cursor_x >= s->x && g_compositor.cursor_x < s->x + (i32)s->width &&
                            g_compositor.cursor_y >= s->y && g_compositor.cursor_y < s->y + (i32)s->height) {
                            
                            if (ev.value == KEY_PRESSED && g_compositor.cursor_y < s->y + 24 && g_compositor.cursor_x > s->x + (i32)s->width - 24) {
                                wl_surface_destroy(s);
                            } else {
                                if (ev.value == KEY_PRESSED) wl_surface_focus(s);
                                input_event_t ex = { .type = EV_ABS, .code = 0, .value = g_compositor.cursor_x - s->x };
                                input_event_t ey = { .type = EV_ABS, .code = 1, .value = g_compositor.cursor_y - (s->y + 24) };
                                wl_surface_push_event(s, ex);
                                wl_surface_push_event(s, ey);
                                wl_surface_push_event(s, ev);
                            }
                            break;
                        }
                    }
                }
            } else if (!is_mouse_btn) {
                /* Keyboard key — route to focused surface */
                for (int i = 0; i < WL_MAX_SURFACES; i++) {
                    wl_surface_t *s = &g_compositor.surfaces[i];
                    if (s->state == WL_SURFACE_ACTIVE && s->id == g_compositor.focused_id) {
                        wl_surface_push_event(s, ev);
                        break;
                    }
                }
            }
        }
    }
}

void wl_compositor_task(void *arg) {
    (void)arg;
    kprintf_color(0xFF00DDFF, "[WAYLAND] Compositor task started.\n");

    /* Disable kernel shell text rendering */
    fb_enable_text(false);

    while (g_compositor.running) {
        process_input();

        /* Background fill */
        if (g_wallpaper_module) {
            u32 *wp = (u32 *)g_wallpaper_module;
            u32 wp_w = wp[0];
            u32 wp_h = wp[1];
            u32 *wp_pixels = &wp[2];
            u32 *dst = g_compositor.scanout->pixels;
            u32 dw = g_compositor.scanout->width;
            u32 dh = g_compositor.scanout->height;
            
            /* Center the wallpaper (assuming it's larger or same size as screen) */
            i32 off_x = (wp_w > dw) ? (wp_w - dw) / 2 : 0;
            i32 off_y = (wp_h > dh) ? (wp_h - dh) / 2 : 0;
            
            for (u32 y = 0; y < dh; y++) {
                u32 src_y = off_y + y;
                u32 *dst_row = dst + (y * dw);
                if (src_y < wp_h) {
                    u32 *src_row = wp_pixels + (src_y * wp_w);
                    u32 copy_w = (dw < wp_w - off_x) ? dw : (wp_w - off_x);
                    memcpy(dst_row, src_row + off_x, copy_w * 4);
                    
                    /* Fill any remaining width with background color */
                    if (copy_w < dw) {
                        for (u32 x = copy_w; x < dw; x++) dst_row[x] = 0xFF1E1E2E;
                    }
                } else {
                    for (u32 x = 0; x < dw; x++) dst_row[x] = 0xFF1E1E2E;
                }
            }
        } else {
            u32 bg_color = 0xFF1E1E2E;
            for(u32 i = 0; i < g_compositor.scanout->size/4; i++){
                g_compositor.scanout->pixels[i] = bg_color;
            }
        }

        /* 1. Draw unfocused surfaces */
        for (int i = 0; i < WL_MAX_SURFACES; i++) {
            if (!g_compositor.surfaces[i].focused) {
                composite_surface(&g_compositor.surfaces[i]);
            }
        }
        
        /* 2. Draw focused surface on top */
        for (int i = 0; i < WL_MAX_SURFACES; i++) {
            if (g_compositor.surfaces[i].focused) {
                composite_surface(&g_compositor.surfaces[i]);
                break;
            }
        }

        /* 3. Draw cursor */
        composite_cursor();

        /* 4. Page flip */
        drm_page_flip(g_compositor.scanout);

        /* Sleep approx 16ms (60 FPS) */
        task_sleep_ms(16);
    }
}
