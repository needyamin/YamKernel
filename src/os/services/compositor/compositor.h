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
#include "drivers/drm/drm.h"
#include "drivers/input/evdev.h"

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

#include "../../../lib/spinlock.h"

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
    bool                maximized;
    i32                 restore_x, restore_y;
    
    /* Owner task ID */
    u64                 owner_task_id;
    
    /* Animations */
    u8                  anim_scale; /* 0 to 100 */
    u8                  anim_alpha; /* 0 to 255 */
    bool                anim_closing;

    /* Debug counters for client/compositor handoff */
    u32                 commit_count;
    u32                 composite_count;
    u32                 event_push_count;
    u32                 event_pop_count;
    u32                 last_checksum;
    bool                content_seen;
    
    spinlock_t          lock;
    /* Event queue for routing input to client */
    struct {
        u32 head;
        u32 tail;
        input_event_t events[32];
    } event_queue;
} wl_surface_t;

/* Compositor system state */
typedef enum {
    COMPOSITOR_STATE_SETUP = 0,
    COMPOSITOR_STATE_LOGIN,
    COMPOSITOR_STATE_DESKTOP,
    COMPOSITOR_STATE_VTTY
} compositor_state_t;

#define YAM_MAX_USERS 8

typedef struct {
    char username[32];
    char password[32];
    char display_name[32];
    bool admin;
    bool active;
} yam_user_account_t;

/* Compositor state */
typedef struct wl_compositor {
    compositor_state_t state;
    char            login_user[32];
    char            login_pass[32];
    bool            login_focus_pass;
    bool            login_failed;
    char            setup_user[32];
    char            setup_pass[32];
    char            setup_computer[32];
    u8              setup_focus;       /* 0 computer, 1 user, 2 password */
    bool            setup_failed;
    bool            setup_complete;
    bool            setup_persisted;
    char            current_user[32];
    char            computer_name[32];
    yam_user_account_t users[YAM_MAX_USERS];
    u32             user_count;

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
    bool            show_debug_overlay;
    u8              desktop_menu_open;  /* 0 none, 1 File, 2 View, 3 Window */
    bool            calendar_open;
    bool            context_menu_open;
    i32             context_x, context_y;
    u32             context_surface_id;
} wl_compositor_t;

/* Initialize the Wayland compositor */
void wl_compositor_init(void);
void wl_compositor_add_account(const char *username, const char *password,
                               const char *display_name, bool admin);
void wl_compositor_add_account_hash(const char *username, const char *password_hash,
                                    const char *display_name, bool admin);
bool wl_compositor_save_profile(void);
bool wl_compositor_load_profile(void);
void wl_password_hash(const char *password, char out[32]);

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
isize wl_clipboard_set_text(const char *text, usize len);
isize wl_clipboard_get_text(char *out, usize cap);

/* Get a surface's pixel buffer for client rendering */
u32 *wl_surface_get_pixels(wl_surface_t *surface);

/* Get the global compositor instance */
wl_compositor_t *wl_get_compositor(void);

#endif /* _WAYLAND_COMPOSITOR_H */
