/* ============================================================================
 * YamKernel — Wayland Compositor Implementation
 * Handles window composition, input routing, and rendering.
 * ============================================================================ */
#include "compositor.h"
#include "compositor_internal.h"
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
#include "fs/vfs.h"
#include "../../dev/vtty.h"

extern void *g_wallpaper_module;
extern void wl_term_task(void *);
extern void wl_browser_task(void *);
extern void wl_calc_task(void *);

wl_compositor_t g_compositor;
static u32 g_next_surface_id = 1;
static char g_wl_clipboard[1024];
static usize g_wl_clipboard_len = 0;

#define WL_PROFILE_PATH "/var/lib/yamos/system.pro"

static void copy_text(char *dst, usize cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) src = "";
    usize i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void wl_password_hash(const char *password, char out[32]) {
    u64 h = 1469598103934665603ULL;
    const unsigned char *p = (const unsigned char *)(password ? password : "");
    while (*p) {
        h ^= (u64)*p++;
        h *= 1099511628211ULL;
    }
    ksnprintf(out, 32, "fnv1a:%lx", h);
}

void wl_compositor_add_account_hash(const char *username, const char *password_hash,
                                    const char *display_name, bool admin) {
    if (!username || !*username || !password_hash || !*password_hash) return;
    for (u32 i = 0; i < g_compositor.user_count; i++) {
        if (g_compositor.users[i].active &&
            strcmp(g_compositor.users[i].username, username) == 0) {
            copy_text(g_compositor.users[i].password, sizeof(g_compositor.users[i].password), password_hash);
            copy_text(g_compositor.users[i].display_name, sizeof(g_compositor.users[i].display_name), display_name);
            g_compositor.users[i].admin = admin;
            return;
        }
    }
    if (g_compositor.user_count >= YAM_MAX_USERS) return;
    yam_user_account_t *u = &g_compositor.users[g_compositor.user_count++];
    memset(u, 0, sizeof(*u));
    copy_text(u->username, sizeof(u->username), username);
    copy_text(u->password, sizeof(u->password), password_hash);
    copy_text(u->display_name, sizeof(u->display_name), display_name);
    u->admin = admin;
    u->active = true;
}

void wl_compositor_add_account(const char *username, const char *password,
                               const char *display_name, bool admin) {
    char hash[32];
    wl_password_hash(password, hash);
    wl_compositor_add_account_hash(username, hash, display_name, admin);
}

static const char *line_after(const char *line, const char *prefix) {
    usize n = strlen(prefix);
    return strncmp(line, prefix, n) == 0 ? line + n : NULL;
}

static void read_field(char **p, char *out, usize cap) {
    if (!p || !*p) return;
    char *s = *p;
    usize i = 0;
    while (*s && *s != '|' && *s != '\n' && *s != '\r') {
        if (i + 1 < cap) out[i++] = *s;
        s++;
    }
    out[i] = 0;
    if (*s == '|') s++;
    *p = s;
}

static void load_profile_line(char *line) {
    const char *value = line_after(line, "computer=");
    if (value) {
        copy_text(g_compositor.computer_name, sizeof(g_compositor.computer_name), value);
        copy_text(g_compositor.setup_computer, sizeof(g_compositor.setup_computer), value);
        return;
    }

    value = line_after(line, "login=");
    if (value) {
        copy_text(g_compositor.login_user, sizeof(g_compositor.login_user), value);
        return;
    }

    char *user = (char *)line_after(line, "user=");
    if (user) {
        char name[32], hash[32], display[32], admin_text[8];
        read_field(&user, name, sizeof(name));
        read_field(&user, hash, sizeof(hash));
        read_field(&user, display, sizeof(display));
        read_field(&user, admin_text, sizeof(admin_text));
        wl_compositor_add_account_hash(name, hash, display[0] ? display : name,
                                       strcmp(admin_text, "1") == 0);
    }
}

bool wl_compositor_load_profile(void) {
    int fd = sys_open(WL_PROFILE_PATH, 0);
    if (fd < 0) return false;

    char buf[2048];
    isize n = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);
    if (n <= 0) return false;
    buf[n] = 0;

    g_compositor.user_count = 0;
    char line[256];
    usize pos = 0;
    for (isize i = 0; i <= n; i++) {
        char c = buf[i];
        if (c == '\n' || c == 0) {
            line[pos] = 0;
            if (pos > 0) load_profile_line(line);
            pos = 0;
        } else if (c != '\r' && pos + 1 < sizeof(line)) {
            line[pos++] = c;
        }
    }

    if (g_compositor.user_count == 0) return false;
    g_compositor.setup_complete = true;
    g_compositor.setup_persisted = true;
    g_compositor.state = COMPOSITOR_STATE_LOGIN;
    g_compositor.login_pass[0] = 0;
    g_compositor.login_focus_pass = false;
    g_compositor.login_failed = false;
    g_compositor.current_user[0] = 0;
    if (g_compositor.login_user[0] == 0) {
        copy_text(g_compositor.login_user, sizeof(g_compositor.login_user),
                  g_compositor.users[0].username);
    }
    kprintf("[SETUP] Loaded persisted profile: computer='%s' users=%u login='%s'\n",
            g_compositor.computer_name, g_compositor.user_count, g_compositor.login_user);
    return true;
}

bool wl_compositor_save_profile(void) {
    sys_mkdir("/var/lib", 0755);
    sys_mkdir("/var/lib/yamos", 0755);

    int fd = sys_open(WL_PROFILE_PATH, 0x40 | 0x0200 | 0x0001);
    if (fd < 0) {
        kprintf("[SETUP] profile save failed: open %s\n", WL_PROFILE_PATH);
        return false;
    }

    char buf[2048];
    usize pos = 0;
    pos += ksnprintf(buf + pos, sizeof(buf) - pos,
                     "version=1\ncomputer=%s\nlogin=%s\n",
                     g_compositor.computer_name,
                     g_compositor.login_user[0] ? g_compositor.login_user : "root");
    for (u32 i = 0; i < g_compositor.user_count && pos + 96 < sizeof(buf); i++) {
        yam_user_account_t *u = &g_compositor.users[i];
        if (!u->active) continue;
        pos += ksnprintf(buf + pos, sizeof(buf) - pos, "user=%s|%s|%s|%u\n",
                         u->username, u->password, u->display_name, u->admin ? 1 : 0);
    }

    isize written = sys_write(fd, buf, pos);
    sys_close(fd);
    if (written != (isize)pos) {
        kprintf("[SETUP] profile save failed: wrote=%ld expected=%lu\n", written, pos);
        return false;
    }
    g_compositor.setup_persisted = true;
    kprintf("[SETUP] profile saved: %s users=%u bytes=%lu\n",
            WL_PROFILE_PATH, g_compositor.user_count, pos);
    return true;
}

static u32 wl_debug_checksum(const u32 *pixels, u32 total_pixels) {
    if (!pixels || total_pixels == 0) return 0;
    u32 step = total_pixels / 64;
    if (step == 0) step = 1;

    u32 sum = 0x811C9DC5u;
    for (u32 i = 0; i < total_pixels; i += step) {
        sum ^= pixels[i];
        sum *= 16777619u;
    }
    sum ^= pixels[total_pixels - 1];
    return sum;
}

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
    
    /* First-boot setup and login state. Load persistent profile if /var is
     * backed by the system volume; otherwise fall back to setup mode. */
    g_compositor.state = COMPOSITOR_STATE_SETUP;
    strcpy(g_compositor.computer_name, "yamos-pc");
    strcpy(g_compositor.setup_computer, "yamos-pc");
    g_compositor.setup_user[0] = '\0';
    g_compositor.setup_pass[0] = '\0';
    g_compositor.setup_focus = 0;
    g_compositor.setup_failed = false;
    g_compositor.setup_complete = false;
    g_compositor.setup_persisted = false;
    g_compositor.user_count = 0;
    strcpy(g_compositor.login_user, "root");
    g_compositor.login_pass[0] = '\0';
    g_compositor.login_focus_pass = false;
    g_compositor.login_failed = false;
    g_compositor.current_user[0] = '\0';
    strcpy(g_compositor.current_user, "setup");

    if (wl_compositor_load_profile()) {
        kprintf_color(0xFF00DDFF, "[WAYLAND] Compositor initialized at %ux%u (Mode: LOGIN, persisted setup)\n",
                      mode.width, mode.height);
    } else {
        kprintf_color(0xFF00DDFF, "[WAYLAND] Compositor initialized at %ux%u (Mode: FIRST-BOOT SETUP)\n",
                      mode.width, mode.height);
    }
}

wl_compositor_t *wl_get_compositor(void) {
    return &g_compositor;
}

isize wl_clipboard_set_text(const char *text, usize len) {
    if (!text) return -1;
    if (len >= sizeof(g_wl_clipboard)) len = sizeof(g_wl_clipboard) - 1;
    memcpy(g_wl_clipboard, text, len);
    g_wl_clipboard[len] = 0;
    g_wl_clipboard_len = len;
    kprintf("[CLIPBOARD] compositor text set len=%lu\n", len);
    return (isize)len;
}

isize wl_clipboard_get_text(char *out, usize cap) {
    if (!out || cap == 0) return -1;
    usize n = g_wl_clipboard_len;
    if (n + 1 > cap) n = cap - 1;
    memcpy(out, g_wl_clipboard, n);
    out[n] = 0;
    return (isize)n;
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

    memset(s, 0, sizeof(*s));
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
    s->maximized    = false;
    s->restore_x    = x;
    s->restore_y    = y;
    
    /* Init animation state */
    s->anim_scale   = 50;  /* 50% */
    s->anim_alpha   = 0;   /* 0 alpha */
    s->anim_closing = false;
    s->last_checksum = 0;
    s->content_seen = false;

    s->buffer = drm_create_dumb_buffer(w, h);
    if (!s->buffer) {
        s->state = WL_SURFACE_FREE;
        return NULL;
    }
    /* Zero the new buffer to prevent ghosting from previous apps */
    memset(s->buffer->pixels, 0, s->buffer->size);
    
    /* Clear event queue and init lock */
    spin_init(&s->lock);
    s->event_queue.head = 0;
    s->event_queue.tail = 0;
    memset(s->event_queue.events, 0, sizeof(s->event_queue.events));

    /* Auto-focus the new surface */
    wl_surface_focus(s);

    g_compositor.surface_count++;
    
    kprintf_color(0xFF00DDFF, "[WL_DBG] create id=%u title='%s' pos=%d,%d size=%ux%u owner=%lu buffer=%p bytes=%lu\n",
                  s->id, s->title, x, y, w, h, owner,
                  s->buffer ? s->buffer->pixels : NULL,
                  s->buffer ? (u64)s->buffer->size : 0);
    return s;
}

void wl_surface_destroy(wl_surface_t *surface) {
    if (!surface || surface->state == WL_SURFACE_FREE) return;
    
    /* If we haven't started closing animation, trigger it and return */
    if (!surface->anim_closing) {
        surface->anim_closing = true;
        input_event_t close_ev = { .type = EV_CLOSE, .code = 0, .value = 1 };
        wl_surface_push_event(surface, close_ev);
        kprintf("[WL_DBG] close-request id=%u title='%s' owner=%lu\n",
                surface->id, surface->title, surface->owner_task_id);
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
    if (!surface || surface->state != WL_SURFACE_ACTIVE || !surface->buffer) return;

    surface->commit_count++;
    surface->last_checksum = wl_debug_checksum(surface->buffer->pixels, surface->width * surface->height);

    if (!surface->content_seen && surface->last_checksum != 0) {
        surface->content_seen = true;
        kprintf("[WL_DBG] first-content id=%u title='%s' commit=%u checksum=0x%x\n",
                surface->id, surface->title, surface->commit_count, surface->last_checksum);
    } else if (surface->commit_count <= 3 || (surface->commit_count % 120) == 0) {
        kprintf("[WL_DBG] commit id=%u title='%s' count=%u checksum=0x%x\n",
                surface->id, surface->title, surface->commit_count, surface->last_checksum);
    }
}

void wl_surface_push_event(wl_surface_t *surface, input_event_t ev) {
    if (!surface || surface->state != WL_SURFACE_ACTIVE) return;
    u64 f = spin_lock_irqsave(&surface->lock);
    u32 next_head = (surface->event_queue.head + 1) % 32;
    if (next_head != surface->event_queue.tail) {
        surface->event_queue.events[surface->event_queue.head] = ev;
        surface->event_queue.head = next_head;
        surface->event_push_count++;
        if (surface->event_push_count <= 8 || (surface->event_push_count % 64) == 0) {
            kprintf("[WL_DBG] event-push id=%u title='%s' type=%u code=%u value=%d total=%u\n",
                    surface->id, surface->title, ev.type, ev.code, ev.value, surface->event_push_count);
        }
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
    surface->event_pop_count++;
    if (surface->event_pop_count <= 8 || (surface->event_pop_count % 64) == 0) {
        kprintf("[WL_DBG] event-pop id=%u title='%s' type=%u code=%u value=%d total=%u\n",
                surface->id, surface->title, ev->type, ev->code, ev->value, surface->event_pop_count);
    }
    spin_unlock_irqrestore(&surface->lock, f);
    return true;
}


void wl_compositor_task(void *arg) {
    (void)arg;
    kprintf_color(0xFF00DDFF, "[WAYLAND] Compositor task started.\n");
    fb_enable_text(false);
    while (g_compositor.running) {
        wl_compositor_process_input();
        wl_compositor_render_frame();
        task_sleep_ms(16);
    }
}
