/* ============================================================================
 * YamKernel — Wayland Compositor Implementation
 * Handles window composition, input routing, and rendering.
 * ============================================================================ */
#include "compositor.h"
#include "drivers/input/evdev.h"
#include "drivers/video/framebuffer.h"
#include "sched/sched.h"
#include "sched/wait.h"
#include "compositor.h"
#include "../../../sched/sched.h"
#include "../../../sched/wait.h"
#include "../../../mem/heap.h"
#include "../../../lib/kprintf.h"
#include "../../../lib/string.h"
#include "../../../lib/kdebug.h"
#include "../../../cpu/percpu.h"
#include "../../../cpu/apic.h"
#include "../../../cpu/msr.h"
#include "../../../drivers/timer/rtc.h"
#include "../../../nexus/channel.h"
#include "../../../nexus/graph.h"
#include "wl_draw.h"
#include <nexus/panic.h>
#include "fs/elf.h"

extern void *g_wallpaper_module;
extern void *g_calc_module;
extern usize g_calc_module_size;
extern void *g_term_module;
extern usize g_term_module_size;
extern void *g_browser_module;
extern usize g_browser_module_size;
extern void wl_term_task(void *);
extern void wl_browser_task(void *);
extern void wl_calc_task(void *);

static wl_compositor_t g_compositor;
static u32 g_next_surface_id = 1;

static void composite_debug_overlay(void);
static void composite_heartbeat(void);

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
    g_compositor.dragging = false;
    
    /* Login State Init */
    g_compositor.state = COMPOSITOR_STATE_LOGIN;
    strcpy(g_compositor.login_user, "root");
    g_compositor.login_pass[0] = '\0';
    g_compositor.login_focus_pass = true;
    g_compositor.login_failed = false;

    kprintf_color(0xFF00DDFF, "[WAYLAND] Compositor initialized at %ux%u (Mode: %s)\n",
                  mode.width, mode.height, (g_compositor.state == COMPOSITOR_STATE_LOGIN) ? "LOGIN" : "DESKTOP");
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
    
    /* Init animation state */
    s->anim_scale   = 50;  /* 50% */
    s->anim_alpha   = 0;   /* 0 alpha */
    s->anim_closing = false;

    s->buffer = drm_create_dumb_buffer(w, h);
    if (!s->buffer) {
        s->state = WL_SURFACE_FREE;
        return NULL;
    }
    /* Zero the new buffer to prevent ghosting from previous apps */
    memset(s->buffer->pixels, 0, w * h * 4);
    
    /* Clear event queue and init lock */
    spin_init(&s->lock);
    s->event_queue.head = 0;
    s->event_queue.tail = 0;
    memset(s->event_queue.events, 0, sizeof(s->event_queue.events));

    /* Auto-focus the new surface */
    wl_surface_focus(s);

    g_compositor.surface_count++;
    
    kprintf_color(0xFF00DDFF, "[WAYLAND] Created surface %u ('%s') %ux%u for task %lu\n",
                  s->id, s->title, w, h, owner);
    return s;
}

void wl_surface_destroy(wl_surface_t *surface) {
    if (!surface || surface->state == WL_SURFACE_FREE) return;
    
    /* If we haven't started closing animation, trigger it and return */
    if (!surface->anim_closing) {
        surface->anim_closing = true;
        return;
    }
    
    /* 1. Mark as FREE immediately so app tasks stop drawing and compositor stops rendering */
    surface->state = WL_SURFACE_FREE;
    u32 old_id = surface->id;
    surface->id = 0;
    
    /* 2. Reset dragging if this was the dragged surface */
    if (g_compositor.dragging && g_compositor.surfaces[g_compositor.drag_surface_id].id == old_id) {
        g_compositor.dragging = false;
    }

    /* 3. Handle focus shift */
    if (g_compositor.focused_id == old_id) {
        g_compositor.focused_id = 0;
        for (int i = WL_MAX_SURFACES - 1; i >= 0; i--) {
            if (g_compositor.surfaces[i].state == WL_SURFACE_ACTIVE) {
                wl_surface_focus(&g_compositor.surfaces[i]);
                break;
            }
        }
    }

    /* 4. Kill the owner task IMMEDIATELY so it doesn't keep drawing */
    if (surface->owner_task_id) {
        sched_kill_task(surface->owner_task_id);
    }

    /* 5. Wait 20ms (two timer ticks) to guarantee that if the task was running on another CPU, 
       it has been preempted and will never be scheduled again (since it's TASK_DEAD). */
    task_sleep_ms(20);

    /* 6. Now it's 100% safe to free the physical memory pages */
    if (surface->buffer) {
        drm_destroy_dumb_buffer(surface->buffer);
        surface->buffer = NULL;
    }
    
    surface->event_queue.head = 0;
    surface->event_queue.tail = 0;
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
    if (!surface || surface->state != WL_SURFACE_ACTIVE) return;
    u64 f = spin_lock_irqsave(&surface->lock);
    u32 next_head = (surface->event_queue.head + 1) % 32;
    if (next_head != surface->event_queue.tail) {
        surface->event_queue.events[surface->event_queue.head] = ev;
        surface->event_queue.head = next_head;
    }
    spin_unlock_irqrestore(&surface->lock, f);
}

bool wl_surface_pop_event(wl_surface_t *surface, input_event_t *ev) {
    if (!surface || surface->state != WL_SURFACE_ACTIVE) return false;
    u64 f = spin_lock_irqsave(&surface->lock);
    if (surface->event_queue.head == surface->event_queue.tail) {
        spin_unlock_irqrestore(&surface->lock, f);
        return false;
    }
    *ev = surface->event_queue.events[surface->event_queue.tail];
    surface->event_queue.tail = (surface->event_queue.tail + 1) % 32;
    spin_unlock_irqrestore(&surface->lock, f);
    return true;
}

static void composite_surface(wl_surface_t *s) {
    if (s->state != WL_SURFACE_ACTIVE || !s->buffer) return;

    /* Animation Update */
    if (s->anim_closing) {
        s->anim_scale = (s->anim_scale > 10) ? s->anim_scale - 10 : 0;
        s->anim_alpha = (s->anim_alpha > 25) ? s->anim_alpha - 25 : 0;
        if (s->anim_scale <= 50 || s->anim_alpha == 0) {
            wl_surface_destroy(s); /* Finish destruction */
            return;
        }
    } else {
        if (s->anim_scale < 100) {
            s->anim_scale += 10;
            if (s->anim_scale > 100) s->anim_scale = 100;
        }
        if (s->anim_alpha < 255) {
            int new_alpha = (int)s->anim_alpha + 25;
            s->anim_alpha = (new_alpha > 255) ? 255 : (u8)new_alpha;
        }
    }

    u32 *dst = g_compositor.scanout->pixels;
    u32 *src = s->buffer->pixels;
    u32 dw = g_compositor.scanout->width;
    u32 dh = g_compositor.scanout->height;
    
    /* Animated Size */
    u32 sw = (s->width * s->anim_scale) / 100;
    u32 sh = (s->height * s->anim_scale) / 100;
    
    /* Keep centered during scale */
    i32 ax = s->x + (s->width - sw) / 2;
    i32 ay = s->y + (s->height - sh) / 2;
    
    /* Bounding box */
    i32 start_y = ay < 0 ? 0 : ay;
    i32 start_x = ax < 0 ? 0 : ax;
    i32 end_y = ay + sh > dh ? dh : ay + sh;
    i32 end_x = ax + sw > dw ? dw : ax + sw;

    /* Window decorations (border) */
    u32 border = s->focused ? s->border_color : 0xFF555555;
    u32 title_h = 24;
    u32 border_w = 2;
    
    for (i32 y = start_y; y < end_y; y++) {
        u32 sy = y - ay;
        i32 x = start_x;
        
        if (sy < title_h || sy >= sh - border_w) {
            /* Entire row is border or titlebar */
            for (; x < end_x; x++) {
                u32 sx = x - ax;
                u32 color = border;
                if (sy >= border_w && sy < title_h && sx >= border_w && sx < sw - border_w) {
                    /* Glassmorphic titlebar */
                    u32 bg_color = dst[y * dw + x];
                    u32 r1 = (bg_color >> 16) & 0xFF;
                    u32 g1 = (bg_color >> 8) & 0xFF;
                    u32 b1 = bg_color & 0xFF;
                    
                    u32 r2 = (s->title_bg >> 16) & 0xFF;
                    u32 g2 = (s->title_bg >> 8) & 0xFF;
                    u32 b2 = s->title_bg & 0xFF;
                    
                    /* Fast 50/50 blend using shifts */
                    u32 nr = ((r1 & 0xFE) >> 1) + ((r2 & 0xFE) >> 1);
                    u32 ng = ((g1 & 0xFE) >> 1) + ((g2 & 0xFE) >> 1);
                    u32 nb = ((b1 & 0xFE) >> 1) + ((b2 & 0xFE) >> 1);
                    color = 0xFF000000 | (nr << 16) | (ng << 8) | nb;
                    
                    if (sx > sw - 20 && sx < sw - 5 && sy > 5 && sy < 20) {
                        if (sx - (sw - 20) == sy - 5 || sx - (sw - 20) == 15 - (sy - 5)) color = 0xFFFFFFFF;
                        else color = 0xFFFF3333;
                    }
                }
                
                /* Apply alpha fade */
                if (s->anim_alpha < 255) {
                    u32 dc = dst[y * dw + x];
                    u32 sr = (color >> 16) & 0xFF, sg = (color >> 8) & 0xFF, sb = color & 0xFF;
                    u32 dr = (dc >> 16) & 0xFF, dg = (dc >> 8) & 0xFF, db = dc & 0xFF;
                    u32 alpha = s->anim_alpha;
                    u32 nr = (sr * alpha + dr * (255 - alpha)) / 255;
                    u32 ng = (sg * alpha + dg * (255 - alpha)) / 255;
                    u32 nb = (sb * alpha + db * (255 - alpha)) / 255;
                    color = 0xFF000000 | (nr << 16) | (ng << 8) | nb;
                }
                
                dst[y * dw + x] = color;
            }
        } else {
            /* Content row with side borders */
            /* Left border */
            for (; x < ax + (i32)border_w && x < end_x; x++) {
                dst[y * dw + x] = border;
            }
            
            /* Content */
            i32 content_end_x = ax + (i32)sw - (i32)border_w;
            if (content_end_x > end_x) content_end_x = end_x;
            
            for (; x < content_end_x; x++) {
                u32 sx = x - ax;
                
                /* Map scaled coords to original buffer coords */
                u32 orig_x = (sx * s->width) / sw;
                u32 orig_y = ((sy - title_h) * s->height) / sh;
                if (orig_x >= s->width) orig_x = s->width - 1;
                if (orig_y >= s->height) orig_y = s->height - 1;
                
                u32 sc = src[orig_y * s->width + orig_x];
                
                if (s->anim_alpha < 255) {
                    u32 dc = dst[y * dw + x];
                    u32 sr = (sc >> 16) & 0xFF, sg = (sc >> 8) & 0xFF, sb = sc & 0xFF;
                    u32 dr = (dc >> 16) & 0xFF, dg = (dc >> 8) & 0xFF, db = dc & 0xFF;
                    u32 alpha = s->anim_alpha;
                    u32 nr = (sr * alpha + dr * (255 - alpha)) / 255;
                    u32 ng = (sg * alpha + dg * (255 - alpha)) / 255;
                    u32 nb = (sb * alpha + db * (255 - alpha)) / 255;
                    dst[y * dw + x] = 0xFF000000 | (nr << 16) | (ng << 8) | nb;
                } else {
                    dst[y * dw + x] = sc;
                }
            }
            
            /* Right border */
            for (; x < end_x; x++) {
                dst[y * dw + x] = border;
            }
        }
    }
}

/* (Unused composite_desktop removed) */

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

static void composite_login_screen(void) {
    u32 *dst = g_compositor.scanout->pixels;
    u32 dw = g_compositor.scanout->width;
    u32 dh = g_compositor.scanout->height;
    
    wl_surface_t ds;
    ds.buffer = g_compositor.scanout;
    ds.width = dw;
    ds.height = dh;
    
    i32 box_w = 400;
    i32 box_h = 300;
    i32 bx = (dw - box_w) / 2;
    i32 by = (dh - box_h) / 2;
    
    /* Full screen dim (simulated blur) */
    for (u32 y = 0; y < dh; y++) {
        for (u32 x = 0; x < dw; x++) {
            u32 c = dst[y * dw + x];
            u32 r = ((c >> 16) & 0xFF) / 2;
            u32 g = ((c >> 8) & 0xFF) / 2;
            u32 b = (c & 0xFF) / 2;
            dst[y * dw + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }
    
    /* Draw Glass Login Box */
    for (i32 y = by; y < by + box_h; y++) {
        for (i32 x = bx; x < bx + box_w; x++) {
            u32 c = dst[y * dw + x];
            u32 r1 = (c >> 16) & 0xFF;
            u32 g1 = (c >> 8) & 0xFF;
            u32 b1 = c & 0xFF;
            /* Blend with a slightly lighter tint for the glass panel */
            u32 r2 = 0x28; u32 g2 = 0x2A; u32 b2 = 0x36;
            
            u32 nr = (r1 * 40 + r2 * 60) / 100;
            u32 ng = (g1 * 40 + g2 * 60) / 100;
            u32 nb = (b1 * 40 + b2 * 60) / 100;
            
            /* Add subtle border glow */
            if (x == bx || x == bx + box_w - 1 || y == by || y == by + box_h - 1) {
                nr += 40; ng += 40; nb += 40;
                if (nr > 255) nr = 255;
                if (ng > 255) ng = 255;
                if (nb > 255) nb = 255;
            }
            dst[y * dw + x] = 0xFF000000 | (nr << 16) | (ng << 8) | nb;
        }
    }
    
    /* Draw Header */
    wl_draw_text(&ds, bx + 120, by + 40, "YamKernel OS", 0xFF50FA7B, 0);
    wl_draw_text(&ds, bx + 150, by + 60, "v0.2.0", 0xFF6272A4, 0);
    
    /* Username Field */
    wl_draw_text(&ds, bx + 50, by + 100, "Username", 0xFFF8F8F2, 0);
    u32 user_bg = g_compositor.login_focus_pass ? 0xFF44475A : 0xFF6272A4;
    wl_draw_rect(&ds, bx + 50, by + 120, 300, 40, user_bg);
    wl_draw_text(&ds, bx + 60, by + 132, g_compositor.login_user, 0xFFFFFFFF, 0);
    if (!g_compositor.login_focus_pass) {
        wl_draw_rect(&ds, bx + 60 + strlen(g_compositor.login_user) * 8, by + 132, 8, 16, 0xFFFFFFFF);
    }
    
    /* Password Field */
    wl_draw_text(&ds, bx + 50, by + 170, "Password", 0xFFF8F8F2, 0);
    u32 pass_bg = g_compositor.login_focus_pass ? 0xFF6272A4 : 0xFF44475A;
    wl_draw_rect(&ds, bx + 50, by + 190, 300, 40, pass_bg);
    
    char hidden_pass[32] = {0};
    int plen = strlen(g_compositor.login_pass);
    for (int i = 0; i < plen; i++) hidden_pass[i] = '*';
    wl_draw_text(&ds, bx + 60, by + 202, hidden_pass, 0xFFFFFFFF, 0);
    if (g_compositor.login_focus_pass) {
        wl_draw_rect(&ds, bx + 60 + plen * 8, by + 202, 8, 16, 0xFFFFFFFF);
    }
    
    if (g_compositor.login_failed) {
        wl_draw_text(&ds, bx + 50, by + 250, "Authentication Failed", 0xFFFF5555, 0);
    } else {
        wl_draw_text(&ds, bx + 50, by + 250, "Press ENTER to login", 0xFF6272A4, 0);
    }
}

static void composite_taskbar(void) {
    u32 *dst = g_compositor.scanout->pixels;
    u32 dw = g_compositor.scanout->width;
    u32 dh = g_compositor.scanout->height;
    
    wl_surface_t ds;
    ds.buffer = g_compositor.scanout;
    ds.width = dw;
    ds.height = dh;
    
    /* Calculate floating dock width based on active apps */
    i32 active_apps = 0;
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (g_compositor.surfaces[i].state == WL_SURFACE_ACTIVE) active_apps++;
    }
    
    i32 dock_w = 120 + (active_apps * 160); /* Start button + app icons + padding */
    if (dock_w > (i32)dw - 40) dock_w = dw - 40;
    i32 dock_h = 44;
    i32 dock_x = (dw - dock_w) / 2;
    i32 dock_y = dh - dock_h - 10; /* Floating 10px from bottom */
    
    /* Draw Glass Dock Background */
    for (i32 y = dock_y; y < dock_y + dock_h; y++) {
        for (i32 x = dock_x; x < dock_x + dock_w; x++) {
            u32 c = dst[y * dw + x];
            u32 r1 = (c >> 16) & 0xFF;
            u32 g1 = (c >> 8) & 0xFF;
            u32 b1 = c & 0xFF;
            
            u32 nr = (r1 * 40 + 0x1E * 60) / 100;
            u32 ng = (g1 * 40 + 0x1E * 60) / 100;
            u32 nb = (b1 * 40 + 0x2E * 60) / 100;
            
            /* Light top border for glass highlight */
            if (y == dock_y || y == dock_y + dock_h - 1 || x == dock_x || x == dock_x + dock_w - 1) {
                nr += 40; ng += 40; nb += 40;
                if (nr > 255) nr = 255;
                if (ng > 255) ng = 255;
                if (nb > 255) nb = 255;
            }
            dst[y * dw + x] = 0xFF000000 | (nr << 16) | (ng << 8) | nb;
        }
    }
    
    /* Draw Start Pill */
    i32 bx = dock_x + 10;
    wl_draw_rect(&ds, bx, dock_y + 6, 80, 32, 0xFF8BE9FD);
    wl_draw_text(&ds, bx + 24, dock_y + 14, "YAM", 0xFF282A36, 0);
    bx += 100;
    
    /* Draw Power Menu if active */
    if (g_compositor.show_power_menu) {
        i32 mw = 160;
        i32 mh = 200;
        i32 mx = dock_x;
        i32 my = dock_y - mh - 10;
        
        /* Glass Menu Background */
        for (i32 y = my; y < my + mh; y++) {
            for (i32 x = mx; x < mx + mw; x++) {
                u32 c = dst[y * dw + x];
                u32 r1 = (c >> 16) & 0xFF; u32 g1 = (c >> 8) & 0xFF; u32 b1 = c & 0xFF;
                /* Fast 50/50 blend */
                u32 nr = ((r1 & 0xFE) >> 1) + (0x28 >> 1);
                u32 ng = ((g1 & 0xFE) >> 1) + (0x2A >> 1);
                u32 nb = ((b1 & 0xFE) >> 1) + (0x36 >> 1);
                if (y == my || y == my + mh - 1 || x == mx || x == mx + mw - 1) {
                    nr += 30; ng += 30; nb += 30;
                    if (nr > 255) nr = 255;
                    if (ng > 255) ng = 255;
                    if (nb > 255) nb = 255;
                }
                dst[y * dw + x] = 0xFF000000 | (nr << 16) | (ng << 8) | nb;
            }
        }
        
        /* App Launcher Section */
        wl_draw_text(&ds, mx + 20, my + 20, "Terminal", 0xFFBD93F9, 0);
        wl_draw_text(&ds, mx + 20, my + 45, "Browser", 0xFF8BE9FD, 0);
        wl_draw_text(&ds, mx + 20, my + 70, "Calculator", 0xFF50FA7B, 0);
        
        wl_draw_rect(&ds, mx + 15, my + 105, mw - 30, 1, 0xFF6272A4);
        
        /* Power Section */
        wl_draw_text(&ds, mx + 20, my + 125, "Restart System", 0xFFF8F8F2, 0);
        wl_draw_text(&ds, mx + 20, my + 155, "Shutdown PC", 0xFFFF5555, 0);
    }
    
    /* Draw running apps */
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        wl_surface_t *s = &g_compositor.surfaces[i];
        if (s->state == WL_SURFACE_ACTIVE) {
            u32 bg = s->focused ? 0xFF6272A4 : 0xFF44475A;
            wl_draw_rect(&ds, bx, dock_y + 8, 140, 28, bg);
            
            char short_title[16] = {0};
            strncpy(short_title, s->title, 14);
            wl_draw_text(&ds, bx + 10, dock_y + 14, short_title, 0xFFF8F8F2, 0);
            
            bx += 150;
        }
    }
    
    /* Floating Clock Pill Top Right */
    i32 clock_w = 130;
    i32 clock_h = 48;
    i32 clock_x = dw - clock_w - 20;
    i32 clock_y = 20;
    
    for (i32 y = clock_y; y < clock_y + clock_h; y++) {
        for (i32 x = clock_x; x < clock_x + clock_w; x++) {
            u32 c = dst[y * dw + x];
            u32 r1 = (c >> 16) & 0xFF; u32 g1 = (c >> 8) & 0xFF; u32 b1 = c & 0xFF;
            u32 nr = (r1 * 40 + 0x28 * 60) / 100;
            u32 ng = (g1 * 40 + 0x2A * 60) / 100;
            u32 nb = (b1 * 40 + 0x36 * 60) / 100;
            if (y == clock_y || y == clock_y + clock_h - 1 || x == clock_x || x == clock_x + clock_w - 1) {
                nr += 40; ng += 40; nb += 40;
            }
            dst[y * dw + x] = 0xFF000000 | (nr << 16) | (ng << 8) | nb;
        }
    }
    
    rtc_time_t t;
    rtc_read(&t);
    
    /* Time format: H:MM AM/PM */
    char clock_str[24];
    int h = t.hour;
    const char *ampm = "AM";
    if (h >= 12) {
        ampm = "PM";
        if (h > 12) h -= 12;
    }
    if (h == 0) h = 12;
    
    char *p = clock_str;
    if (h >= 10) *p++ = '0' + (h / 10);
    *p++ = '0' + (h % 10);
    *p++ = ':';
    int m = t.minute; *p++ = '0' + (m / 10); *p++ = '0' + (m % 10);
    *p++ = ' ';
    *p++ = ampm[0]; *p++ = ampm[1];
    *p++ = '\0';
    
    /* Date format: DD Mon YYYY */
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char date_str[32];
    ksnprintf(date_str, sizeof(date_str), "%d %s %d", t.day, (t.month > 0 && t.month <= 12) ? months[t.month-1] : "???", t.year);
    
    wl_draw_text(&ds, clock_x + 15, clock_y + 8, clock_str, 0xFF8BE9FD, 0);
    wl_draw_text(&ds, clock_x + 15, clock_y + 28, date_str, 0xFF6272A4, 0);
}

static void composite_debug_overlay(void) {
    if (!g_compositor.show_debug_overlay) return;

    u32 dw = g_compositor.scanout->width;
    u32 dh = g_compositor.scanout->height;
    wl_surface_t ds = { .buffer = g_compositor.scanout, .width = dw, .height = dh };

    /* Draw Semi-transparent background */
    wl_draw_rect(&ds, 20, 20, 400, dh - 100, 0xCC11111B);
    
    wl_draw_text(&ds, 40, 40, "--- YamOS DIAGNOSTICS ---", 0xFF89B4FA, 0);
    
    /* Mouse Info Info */
    wl_draw_text(&ds, 40, 70, "Input Status: OK (120Hz)", 0xFFF8F8F2, 0);

    /* Recent Kernel Logs */
    wl_draw_text(&ds, 40, 100, "RECENT KERNEL EVENTS:", 0xFFFAB387, 0);
    char logs[512];
    kdebug_get_recent(logs, 512);
    
    /* Display logs line by line */
    int ly = 120;
    char *lptr = logs;
    for(int i=0; i<15; i++) {
        char line[64] = {0};
        int j = 0;
        while(*lptr && *lptr != '\n' && j < 63) line[j++] = *lptr++;
        if(*lptr == '\n') lptr++;
        wl_draw_text(&ds, 50, ly, line, 0xFFA6ADC8, 0);
        ly += 20;
        if(!*lptr) break;
    }

    wl_draw_text(&ds, 40, ly + 20, "APP STATUS:", 0xFFF9E2AF, 0);
    ly += 40;
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (g_compositor.surfaces[i].state == WL_SURFACE_ACTIVE) {
            wl_draw_text(&ds, 50, ly, g_compositor.surfaces[i].title, 0xFF94E2D5, 0);
            ly += 20;
        }
    }
}

/* ---- Asynchronous App Loader ---- */
typedef struct {
    void *data;
    usize size;
    char name[32];
} app_load_request_t;

static void app_loader_task(void *arg) {
    app_load_request_t *req = (app_load_request_t *)arg;
    elf_load(req->data, req->size, req->name);
    kfree(req);
}

static void spawn_app_async(void *data, usize size, const char *name) {
    app_load_request_t *req = (app_load_request_t *)kmalloc(sizeof(app_load_request_t));
    if (!req) return;
    req->data = data;
    req->size = size;
    for(int i=0; i<31 && name[i]; i++) req->name[i] = name[i];
    req->name[31] = '\0';
    sched_spawn("app-loader", app_loader_task, req, 1);
}

static void composite_heartbeat(void) {
    /* Blinking dot in bottom-right to show compositor is alive */
    rtc_time_t t;
    rtc_read(&t);
    if (t.second % 2 == 0) {
        u32 *dst = g_compositor.scanout->pixels;
        u32 dw = g_compositor.scanout->width;
        u32 dh = g_compositor.scanout->height;
        dst[(dh - 5) * dw + (dw - 5)] = 0xFF50FA7B; /* Green blink */
    }
}

static const char sc_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',  0,
    '\\','z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
};
static const char sc_ascii_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',  0,
    '|','Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ',
};
static bool shift_held = false;

static void process_input(void) {
    input_event_t ev;
    int processed = 0;
    while (evdev_pop_event(&ev) && processed < 32) {
        processed++;
        if (ev.type == EV_REL) {
            /* Apply 2x sensitivity multiplier */
            i32 move = ev.value * 2;
            if (ev.code == REL_X) g_compositor.cursor_x += move;
            if (ev.code == REL_Y) g_compositor.cursor_y += move;
            
            if (g_compositor.dragging) {
                wl_surface_t *s = &g_compositor.surfaces[g_compositor.drag_surface_id];
                if (s->state == WL_SURFACE_ACTIVE) {
                    s->x = g_compositor.cursor_x - g_compositor.drag_off_x;
                    s->y = g_compositor.cursor_y - g_compositor.drag_off_y;
                }
            }
            
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
            
            if (ev.type == EV_KEY && ev.value == KEY_PRESSED) {
                /* Global Hotkeys */
                if (ev.code == 0x3F) { /* F5: Refresh (Restart compositor loop) */
                    fb_clear(0xFF1E1E2E);
                    continue;
                }
                if (ev.code == 0x58 || ev.code == 0x3B) { /* F12 or F1: Debug Overlay */
                    g_compositor.show_debug_overlay = !g_compositor.show_debug_overlay;
                    kprintf("[DEBUG] Overlay toggled: %s\n", g_compositor.show_debug_overlay ? "ON" : "OFF");
                    continue;
                }
                /* Emergency Bypass: CTRL(0x1D) + SHIFT(0x2A) + B(0x30) */
                if (ev.code == 0x30 && shift_held) {
                    kprintf("[WAYLAND] Emergency login bypass triggered\n");
                    g_compositor.state = COMPOSITOR_STATE_DESKTOP;
                    continue;
                }
            }
            
            bool is_mouse_btn = (ev.code >= 0x110);
            
            if (!is_mouse_btn) {
                u16 sc = ev.code;
                if (sc == 0x2A || sc == 0x36) {
                    shift_held = (ev.value == KEY_PRESSED);
                }
            }
            
            if (g_compositor.state == COMPOSITOR_STATE_LOGIN) {
                if (!is_mouse_btn && ev.value == KEY_PRESSED) {
                    u16 sc = ev.code;
                    char c = 0;
                    if (sc < 128) c = shift_held ? sc_ascii_shift[sc] : sc_ascii[sc];
                    
                    if (c == '\n') {
                        static yam_channel_t *auth_chan = NULL;
                        if (!auth_chan) {
                            yam_node_id_t authd_node = yamgraph_find_node_by_name("authd");
                            if (authd_node != (yam_node_id_t)-1) {
                                yam_node_id_t comp_node = sched_current()->graph_node;
                                auth_chan = channel_create("auth_channel", comp_node, authd_node);
                            }
                        }
                        
                        if (auth_chan) {
                            int len = strlen(g_compositor.login_pass);
                            channel_send(auth_chan, sched_current()->graph_node, 1, g_compositor.login_pass, len);
                            
                            yam_message_t reply;
                            int retries = 50;
                            bool success = false;
                            while (retries-- > 0) {
                                if (channel_recv(auth_chan, &reply)) {
                                    if (reply.msg_type == 2) success = true;
                                    break;
                                }
                                task_sleep_ms(10);
                            }
                            
                            if (success) {
                                g_compositor.state = COMPOSITOR_STATE_DESKTOP;
                            } else {
                                g_compositor.login_failed = true;
                                g_compositor.login_pass[0] = '\0';
                            }
                        } else {
                            /* Fallback if authd is dead */
                            if (strcmp(g_compositor.login_pass, "password") == 0) {
                                g_compositor.state = COMPOSITOR_STATE_DESKTOP;
                            } else {
                                g_compositor.login_failed = true;
                                g_compositor.login_pass[0] = '\0';
                            }
                        }
                    } else if (c == '\b') {
                        if (g_compositor.login_focus_pass) {
                            int len = strlen(g_compositor.login_pass);
                            if (len > 0) g_compositor.login_pass[len-1] = '\0';
                        } else {
                            int len = strlen(g_compositor.login_user);
                            if (len > 0) g_compositor.login_user[len-1] = '\0';
                        }
                    } else if (c == '\t') {
                        g_compositor.login_focus_pass = !g_compositor.login_focus_pass;
                    } else if (c >= 32 && c <= 126) {
                        g_compositor.login_failed = false;
                        if (g_compositor.login_focus_pass) {
                            int len = strlen(g_compositor.login_pass);
                            if (len < 31) { g_compositor.login_pass[len] = c; g_compositor.login_pass[len+1] = '\0'; }
                        } else {
                            int len = strlen(g_compositor.login_user);
                            if (len < 31) { g_compositor.login_user[len] = c; g_compositor.login_user[len+1] = '\0'; }
                        }
                    }
                } else if (is_mouse_btn && ev.value == KEY_PRESSED) {
                    /* Click to switch focus */
                    i32 dw = g_compositor.scanout->width;
                    i32 dh = g_compositor.scanout->height;
                    i32 bx = (dw - 400) / 2;
                    i32 by = (dh - 300) / 2;
                    
                    if (g_compositor.cursor_x >= bx + 50 && g_compositor.cursor_x <= bx + 350) {
                        if (g_compositor.cursor_y >= by + 120 && g_compositor.cursor_y <= by + 160) {
                            g_compositor.login_focus_pass = false;
                        } else if (g_compositor.cursor_y >= by + 190 && g_compositor.cursor_y <= by + 230) {
                            g_compositor.login_focus_pass = true;
                        }
                    }
                }
            } else {
                /* DESKTOP STATE ROUTING */
                if (is_mouse_btn && (ev.value == KEY_PRESSED || ev.value == KEY_RELEASED)) {
                    /* Calculate Dock position (must match composite_taskbar) */
                    i32 app_count = 0;
                    for (int i = 0; i < WL_MAX_SURFACES; i++) if (g_compositor.surfaces[i].state == WL_SURFACE_ACTIVE) app_count++;
                    i32 dock_w = 120 + (app_count * 160);
                    i32 dock_x = (g_compositor.display_w - dock_w) / 2;
                    i32 dock_y = g_compositor.display_h - 44 - 10;

                    /* Check Power/App Menu Clicks */
                    if (g_compositor.show_power_menu && ev.value == KEY_PRESSED) {
                        i32 mw = 160, mh = 200;
                        i32 mx = dock_x;
                        if (g_compositor.cursor_x >= mx && g_compositor.cursor_x <= mx + mw) {
                            if (g_compositor.cursor_y >= dock_y - mh - 10 && g_compositor.cursor_y < dock_y - 10) {
                                i32 rel_y = g_compositor.cursor_y - (dock_y - mh - 10);
                                if (rel_y >= 10 && rel_y < 40) {
                                    kprintf("[WAYLAND] Launching Terminal (async)...\n");
                                    if (g_term_module && g_term_module_size > 0) {
                                        spawn_app_async(g_term_module, g_term_module_size, "wl-term");
                                    } else {
                                        kprintf("[WAYLAND] Fallback to Kernel Terminal\n");
                                        sched_spawn("wl-term-k", wl_term_task, NULL, 2);
                                    }
                                } else if (rel_y >= 40 && rel_y < 65) {
                                    kprintf("[WAYLAND] Launching Browser (async)...\n");
                                    if (g_browser_module && g_browser_module_size > 0) {
                                        spawn_app_async(g_browser_module, g_browser_module_size, "wl-browser");
                                    } else {
                                        kprintf("[WAYLAND] Fallback to Kernel Browser\n");
                                        sched_spawn("wl-browser-k", wl_browser_task, NULL, 2);
                                    }
                                } else if (rel_y >= 65 && rel_y < 90) {
                                    kprintf("[WAYLAND] Launching Calculator (async)...\n");
                                    if (g_calc_module && g_calc_module_size > 0) {
                                        spawn_app_async(g_calc_module, g_calc_module_size, "wl-calc");
                                    } else {
                                        kprintf("[WAYLAND] Fallback to Kernel Calculator\n");
                                        sched_spawn("wl-calc-k", wl_calc_task, NULL, 2);
                                    }
                                } else if (rel_y >= 120 && rel_y < 150) {
                                    /* RESTART */
                                    kprintf("[POWER] Restarting system...\n");
                                    outb(0x64, 0xFE);
                                } else if (rel_y >= 150 && rel_y < 190) {
                                    /* SHUTDOWN */
                                    kprintf("[POWER] Shutting down...\n");
                                    outw(0x604, 0x2000);
                                }
                                g_compositor.show_power_menu = false;
                                continue;
                            }
                        }
                    }

                    /* Check Taskbar/Dock clicks */
                    if (ev.value == KEY_PRESSED && g_compositor.cursor_y >= dock_y && g_compositor.cursor_y <= dock_y + 44) {
                        /* Start button check (first 100px of dock) */
                        if (g_compositor.cursor_x >= dock_x && g_compositor.cursor_x <= dock_x + 100) {
                            g_compositor.show_power_menu = !g_compositor.show_power_menu;
                        } else {
                            /* Check Dock App Icons */
                            i32 abx = dock_x + 110;
                            for (int i = 0; i < WL_MAX_SURFACES; i++) {
                                wl_surface_t *s = &g_compositor.surfaces[i];
                                if (s->state == WL_SURFACE_ACTIVE) {
                                    if (g_compositor.cursor_x >= abx && g_compositor.cursor_x <= abx + 140) {
                                        wl_surface_focus(s);
                                        break;
                                    }
                                    abx += 150;
                                }
                            }
                        }
                        continue;
                    } else if (ev.code == 0x110) {
                        /* Mouse button */
                        if (ev.value == KEY_RELEASED) {
                            g_compositor.dragging = false;
                        }
                        
                        /* Route to surface under cursor */
                        for (int i = WL_MAX_SURFACES - 1; i >= 0; i--) {
                            wl_surface_t *s = &g_compositor.surfaces[i];
                            if (s->state == WL_SURFACE_ACTIVE) {
                                if (g_compositor.cursor_x >= s->x && g_compositor.cursor_x < s->x + (i32)s->width &&
                                    g_compositor.cursor_y >= s->y && g_compositor.cursor_y < s->y + (i32)s->height) {
                                    
                                    if (ev.value == KEY_PRESSED && g_compositor.cursor_y < s->y + 28) {
                                        /* Clicked Titlebar area */
                                        if (g_compositor.cursor_x > s->x + (i32)s->width - 32) {
                                            /* Close button (32px hitbox) */
                                            wl_surface_destroy(s);
                                        } else {
                                            /* Start Dragging */
                                            wl_surface_focus(s);
                                            g_compositor.dragging = true;
                                            g_compositor.drag_surface_id = i;
                                            g_compositor.drag_off_x = g_compositor.cursor_x - s->x;
                                            g_compositor.drag_off_y = g_compositor.cursor_y - s->y;
                                        }
                                    } else {
                                        if (ev.value == KEY_PRESSED) wl_surface_focus(s);
                                        /* Push mouse coordinates relative to content area (below titlebar) */
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
}

void wl_compositor_task(void *arg) {
    (void)arg;
    kprintf_color(0xFF00DDFF, "[WAYLAND] Compositor task started.\n");

    /* Disable kernel shell text rendering */
    fb_enable_text(false);

    while (g_compositor.running) {
        process_input();

        /* Background fill: Centered wallpaper module with fast centering */
        if (g_wallpaper_module) {
            u32 *wp = (u32 *)g_wallpaper_module;
            u32 wp_w = wp[0], wp_h = wp[1];
            u32 *wp_pixels = &wp[2];
            u32 *dst = g_compositor.scanout->pixels;
            u32 dw = g_compositor.scanout->width, dh = g_compositor.scanout->height;
            
            i32 off_x = (wp_w > dw) ? (wp_w - dw) / 2 : 0;
            i32 off_y = (wp_h > dh) ? (wp_h - dh) / 2 : 0;
            
            for (u32 y = 0; y < dh; y++) {
                u32 *dst_row = dst + (y * dw);
                u32 src_y = off_y + y;
                if (src_y < wp_h) {
                    u32 *src_row = wp_pixels + (src_y * wp_w);
                    u32 copy_w = (dw < wp_w - off_x) ? dw : (wp_w - off_x);
                    memcpy(dst_row, src_row + off_x, copy_w * 4);
                } else {
                    for (u32 x = 0; x < dw; x++) dst_row[x] = 0xFF11111B;
                }
            }
        } else {
            u32 bg_color = 0xFF11111B;
            for(u32 i = 0; i < g_compositor.scanout->size/4; i++){
                g_compositor.scanout->pixels[i] = bg_color;
            }
        }

        if (g_compositor.state == COMPOSITOR_STATE_LOGIN) {
            composite_login_screen();
        } else {
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
            
            /* 3. Draw Taskbar */
            composite_taskbar();
        }

        /* 4. Draw Debug Overlay (Always accessible) */
        composite_debug_overlay();
        
        /* 5. Draw Heartbeat */
        composite_heartbeat();

        /* 6. Draw cursor */
        composite_cursor();

        /* 5. Page flip */
        drm_page_flip(g_compositor.scanout);

        /* Target ~120 FPS for ultra-smooth input, or as fast as rendering allows */
        task_sleep_ms(8);
    }
}
