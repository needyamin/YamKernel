/* ============================================================================
 * YamKernel — Wayland Display Server (Kernel-Space Compositor)
 * Implements Wayland's core concepts:
 *   - wl_surface: A window buffer that clients render into
 *   - wl_compositor: Composites surfaces onto the DRM scanout buffer
 *   - wl_shm: Shared memory buffer protocol for software rendering
 *   - Cursor rendering with hardware mouse position
 * ============================================================================ */
#ifndef _WAYLAND_COMPOSITOR_H
#define _WAYLAND_COMPOSITOR_H

#include <nexus/types.h>
#include "../drivers/drm/drm.h"
#include "../drivers/input/evdev.h"

/* Maximum surfaces the compositor can track */
#define WL_MAX_SURFACES     32
#define WL_TITLE_MAX        64

/* Surface states */
typedef enum {
    WL_SURFACE_FREE = 0,
    WL_SURFACE_ACTIVE,
    WL_SURFACE_HIDDEN,
    WL_SURFACE_MINIMIZED,
} wl_surface_state_t;

/* A Wayland surface — represents a single client window */
typedef struct wl_surface {
    u32                 id;
    wl_surface_state_t  state;
    char                title[WL_TITLE_MAX];
    
    /* Geometry (position and size on screen) */
    i32                 x, y;
    u32                 width, height;
    
    /* The client's pixel buffer (shared memory) */
    drm_buffer_t       *buffer;
    
    /* Decoration: border and titlebar colors */
    u32                 border_color;
    u32                 title_bg;
    u32                 title_fg;
    bool                focused;
    
    /* Owner task ID */
    u64                 owner_task_id;
    
    /* Event queue for routing input to client */
    struct {
        u32 head;
        u32 tail;
        input_event_t events[32];
    } event_queue;
} wl_surface_t;

/* Compositor system state */
typedef enum {
    COMPOSITOR_STATE_LOGIN = 0,
    COMPOSITOR_STATE_DESKTOP
} compositor_state_t;

/* Compositor state */
typedef struct wl_compositor {
    compositor_state_t state;
    char            login_user[32];
    char            login_pass[32];
    bool            login_focus_pass;
    bool            login_failed;

    wl_surface_t    surfaces[WL_MAX_SURFACES];
    u32             surface_count;
    u32             focused_id;         /* ID of the focused (top) surface */
    drm_buffer_t   *scanout;            /* The composited output buffer */
    
    /* Cursor state */
    i32             cursor_x, cursor_y;
    bool            cursor_visible;
    
    /* Display dimensions */
    u32             display_w, display_h;
    
    bool            running;
    
    /* Window Dragging */
    bool            dragging;
    u32             drag_surface_id;
    i32             drag_off_x, drag_off_y;
    
    /* Power Menu */
    bool            show_power_menu;
} wl_compositor_t;

/* Initialize the Wayland compositor */
void wl_compositor_init(void);

/* Create a new surface for a client */
wl_surface_t *wl_surface_create(const char *title, i32 x, i32 y, u32 w, u32 h, u64 owner);

/* Destroy a surface */
void wl_surface_destroy(wl_surface_t *surface);

/* Mark a surface's buffer as updated (needs recomposite) */
void wl_surface_commit(wl_surface_t *surface);

/* Input event routing */
void wl_surface_push_event(wl_surface_t *surface, input_event_t ev);
bool wl_surface_pop_event(wl_surface_t *surface, input_event_t *ev);

/* Set surface focus */
void wl_surface_focus(wl_surface_t *surface);

/* The compositor render loop (runs as a kernel task) */
void wl_compositor_task(void *arg);

/* Get a surface's pixel buffer for client rendering */
u32 *wl_surface_get_pixels(wl_surface_t *surface);

/* Get the global compositor instance */
wl_compositor_t *wl_get_compositor(void);

#endif /* _WAYLAND_COMPOSITOR_H */
