/* YamKernel - compositor input routing and app launch */
#include "compositor_internal.h"
#include "cpu/msr.h"
#include "nexus/channel.h"
#include "nexus/graph.h"

extern void wl_term_task(void *);
extern void wl_browser_task(void *);
extern void wl_calc_task(void *);
extern void wl_file_manager_task(void *);
extern void wl_wifi_settings_task(void *);
extern void wl_bluetooth_settings_task(void *);
extern void wl_network_settings_task(void *);
extern void wl_sound_settings_task(void *);
extern void wl_display_settings_task(void *);

static wl_surface_t *focused_surface(void) {
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (g_compositor.surfaces[i].state == WL_SURFACE_ACTIVE &&
            g_compositor.surfaces[i].id == g_compositor.focused_id) {
            return &g_compositor.surfaces[i];
        }
    }
    return NULL;
}

static void surface_frame_rect(wl_surface_t *s, i32 *x, i32 *y, i32 *w, i32 *h) {
    if (!s) return;
    if (s->maximized) {
        if (x) *x = 12;
        if (y) *y = 42;
        if (w) *w = (i32)g_compositor.display_w - 24;
        if (h) *h = (i32)g_compositor.display_h - 126 + 36;
    } else {
        if (x) *x = s->x;
        if (y) *y = s->y;
        if (w) *w = (i32)s->width;
        if (h) *h = (i32)s->height + 36;
    }
}

static bool focus_existing_window(const char *title) {
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        wl_surface_t *s = &g_compositor.surfaces[i];
        if ((s->state == WL_SURFACE_ACTIVE || s->state == WL_SURFACE_MINIMIZED) &&
            strcmp(s->title, title) == 0) {
            if (s->state == WL_SURFACE_MINIMIZED) {
                s->state = WL_SURFACE_ACTIVE;
            }
            wl_surface_focus(s);
            kprintf("[WAYLAND] focused existing '%s' window\n", title);
            return true;
        }
    }
    return false;
}

static void open_or_focus_window(const char *title, const char *task_name,
                                 void (*entry)(void *)) {
    if (focus_existing_window(title)) return;
    sched_spawn(task_name, entry, NULL, 2);
}

typedef struct {
    i32 sound_x, sound_w;
    i32 net_x, net_w;
    i32 wifi_x, wifi_w;
    i32 bt_x, bt_w;
    i32 clock_x;
    bool show_wifi;
    bool show_bt;
} tray_regions_t;

static tray_regions_t desktop_tray_regions(void) {
    tray_regions_t r;
    memset(&r, 0, sizeof(r));

    i32 dw = (i32)g_compositor.display_w;
    i32 tray_right = dw - 12;
    i32 left_limit = 372;
    i32 gap = 8;
    i32 sound_w = 70;
    i32 net_w = 94;
    i32 wifi_w = 78;
    i32 bt_w = 58;
    i32 clock_w = 150;
    bool show_wifi = true;
    bool show_bt = true;

    i32 tray_w = sound_w + net_w + wifi_w + bt_w + clock_w + gap * 4;
    if (tray_w > tray_right - left_limit) {
        sound_w = 54;
        net_w = 68;
        wifi_w = 58;
        bt_w = 42;
        clock_w = 126;
        tray_w = sound_w + net_w + wifi_w + bt_w + clock_w + gap * 4;
    }
    if (tray_w > tray_right - left_limit) {
        show_bt = false;
        sound_w = 46;
        net_w = 58;
        wifi_w = 52;
        clock_w = 116;
        tray_w = sound_w + net_w + wifi_w + clock_w + gap * 3;
    }
    if (tray_w > tray_right - left_limit) {
        show_wifi = false;
        tray_w = sound_w + net_w + clock_w + gap * 2;
    }

    i32 tray_x = tray_right - tray_w;
    if (tray_x < left_limit) tray_x = left_limit;

    r.sound_x = tray_x;
    r.sound_w = sound_w;
    r.net_x = r.sound_x + sound_w + gap;
    r.net_w = net_w;
    r.wifi_x = r.net_x + net_w + gap;
    r.wifi_w = wifi_w;
    r.bt_x = r.wifi_x + wifi_w + gap;
    r.bt_w = bt_w;
    r.clock_x = show_wifi ? (show_bt ? (r.bt_x + bt_w + gap) : (r.wifi_x + wifi_w + gap))
                          : (r.net_x + net_w + gap);
    r.show_wifi = show_wifi;
    r.show_bt = show_bt;
    return r;
}

static void toggle_surface_maximize(wl_surface_t *s) {
    if (!s) return;
    if (!s->maximized) {
        s->restore_x = s->x;
        s->restore_y = s->y;
        s->maximized = true;
        g_compositor.dragging = false;
        kprintf("[WAYLAND] Titlebar maximize surface %u ('%s')\n", s->id, s->title);
    } else {
        s->maximized = false;
        s->x = s->restore_x;
        s->y = s->restore_y;
        kprintf("[WAYLAND] Titlebar restore surface %u ('%s')\n", s->id, s->title);
    }
}

static void clipboard_copy_focused(void) {
    wl_surface_t *s = focused_surface();
    if (!s) return;
    input_event_t ev = { .type = EV_CLIPBOARD, .code = CLIPBOARD_COPY, .value = 1 };
    wl_surface_push_event(s, ev);
    kprintf("[CLIPBOARD] copy requested for surface %u ('%s')\n", s->id, s->title);
}

static void clipboard_paste_focused(void) {
    wl_surface_t *s = focused_surface();
    if (!s) return;
    input_event_t ev = { .type = EV_CLIPBOARD, .code = CLIPBOARD_PASTE, .value = 1 };
    wl_surface_push_event(s, ev);
    kprintf("[CLIPBOARD] paste requested for surface %u ('%s')\n", s->id, s->title);
}

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

void wl_spawn_app_async(void *data, usize size, const char *name) {
    app_load_request_t *req = (app_load_request_t *)kmalloc(sizeof(app_load_request_t));
    if (!req) return;
    req->data = data;
    req->size = size;
    for(int i=0; i<31 && name[i]; i++) req->name[i] = name[i];
    req->name[31] = '\0';
    sched_spawn("app-loader", app_loader_task, req, 1);
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
static bool alt_held   = false;
static bool ctrl_held  = false;

static void field_backspace(char *buf) {
    int len = strlen(buf);
    if (len > 0) buf[len - 1] = '\0';
}

static void field_append(char *buf, char c) {
    int len = strlen(buf);
    if (len < 31) {
        buf[len] = c;
        buf[len + 1] = '\0';
    }
}

static bool validate_account(const char *username, const char *password) {
    char hash[32];
    wl_password_hash(password, hash);
    for (u32 i = 0; i < g_compositor.user_count; i++) {
        yam_user_account_t *u = &g_compositor.users[i];
        if (u->active && strcmp(u->username, username) == 0 &&
            (strcmp(u->password, hash) == 0 || strcmp(u->password, password) == 0)) {
            strcpy(g_compositor.current_user, u->username);
            return true;
        }
    }
    return false;
}

static void finish_first_boot_setup(void) {
    if (strlen(g_compositor.setup_computer) == 0 ||
        strlen(g_compositor.setup_user) == 0 ||
        strlen(g_compositor.setup_pass) < 4) {
        g_compositor.setup_failed = true;
        return;
    }

    strcpy(g_compositor.computer_name, g_compositor.setup_computer);
    wl_compositor_add_account(g_compositor.setup_user, g_compositor.setup_pass, g_compositor.setup_user, true);
    wl_compositor_add_account("root", "password", "root", true);
    wl_compositor_add_account("guest", "guest", "guest", false);

    strcpy(g_compositor.current_user, g_compositor.setup_user);
    strcpy(g_compositor.login_user, g_compositor.setup_user);
    g_compositor.login_pass[0] = '\0';
    g_compositor.setup_complete = true;
    if (wl_compositor_save_profile()) {
        g_compositor.state = COMPOSITOR_STATE_DESKTOP;
    } else {
        g_compositor.setup_failed = true;
        return;
    }
    KINFO("SETUP", "Computer '%s' initialized; first user '%s' created",
          g_compositor.computer_name, g_compositor.current_user);
    /* Ensure home directory exists for each account */
    char home[64];
    for (u32 i = 0; i < g_compositor.user_count; i++) {
        ksnprintf(home, sizeof(home), "/home/%s", g_compositor.users[i].username);
        sys_mkdir(home, 0755);
    }
}

static void skip_first_boot_setup(void) {
    if (g_compositor.user_count == 0) {
        wl_compositor_add_account("root", "password", "root", true);
        wl_compositor_add_account("guest", "guest", "guest", false);
    }
    strcpy(g_compositor.computer_name, "yamos-pc");
    strcpy(g_compositor.current_user, "root");
    strcpy(g_compositor.login_user, "root");
    g_compositor.login_pass[0] = '\0';
    g_compositor.setup_complete = true;
    if (wl_compositor_save_profile()) {
        g_compositor.state = COMPOSITOR_STATE_DESKTOP;
    } else {
        g_compositor.setup_failed = true;
        return;
    }
    KINFO("SETUP", "First-boot setup skipped and persisted; using root/password and guest/guest");
    /* Ensure home dirs exist for default accounts */
    sys_mkdir("/home/root", 0755);
    sys_mkdir("/home/guest", 0755);
}

static char *setup_focused_field(void) {
    if (g_compositor.setup_focus == 0) return g_compositor.setup_computer;
    if (g_compositor.setup_focus == 1) return g_compositor.setup_user;
    return g_compositor.setup_pass;
}

void wl_compositor_process_input(void) {
    input_event_t ev;
    int processed = 0;
    while (evdev_pop_event(&ev) && processed < 64) {
        processed++;
        if (ev.type == EV_REL) {
            /* Apply 2x sensitivity multiplier */
            i32 move = ev.value * 2;
            if (ev.code == REL_X) g_compositor.cursor_x += move;
            if (ev.code == REL_Y) g_compositor.cursor_y += move;
            
            if (g_compositor.dragging) {
                wl_surface_t *s = &g_compositor.surfaces[g_compositor.drag_surface_id];
                if (s->state == WL_SURFACE_ACTIVE) {
                    if (!s->maximized) {
                        s->x = g_compositor.cursor_x - g_compositor.drag_off_x;
                        s->y = g_compositor.cursor_y - g_compositor.drag_off_y;
                    }
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
            
            bool is_mouse_btn = (ev.code >= 0x110);
            bool is_left_btn = (ev.code == BTN_LEFT);
            bool is_right_btn = (ev.code == BTN_RIGHT);

            if (!is_mouse_btn) {
                u16 sc = ev.code;
                if (sc == 0x2A || sc == 0x36) {
                    shift_held = (ev.value == KEY_PRESSED);
                }
                if (sc == 0x1D) {
                    ctrl_held = (ev.value == KEY_PRESSED);
                }
                if (sc == 0x38 || sc == 0x38 + 0x80) { /* Left Alt */
                    alt_held = (ev.value == KEY_PRESSED);
                }
            }

            if (ev.value == KEY_PRESSED) {
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
                if (ev.code == 0x15 && shift_held && ctrl_held) { /* Ctrl+Shift+Y: first-boot/login bypass */
                    skip_first_boot_setup();
                    continue;
                }
            }

            if (!is_mouse_btn) {
                u16 sc = ev.code;

                /* Alt + F1..F6: Switch to VTTY */
                if (alt_held && ev.value == KEY_PRESSED && sc >= 0x3B && sc <= 0x40) {
                    int vtty_id = sc - 0x3B;
                    g_compositor.state = COMPOSITOR_STATE_VTTY;
                    vtty_switch(vtty_id);
                    continue;
                }
                /* Alt + F7: Switch back to Desktop */
                if (alt_held && ev.value == KEY_PRESSED && sc == 0x41) {
                    g_compositor.state = COMPOSITOR_STATE_DESKTOP;
                    fb_enable_text(false);
                    continue;
                }
            }
            
            if (g_compositor.state == COMPOSITOR_STATE_VTTY) {
                if (!is_mouse_btn && ev.value == KEY_PRESSED) {
                    u16 sc = ev.code;
                    char c = 0;
                    if (sc < 128) c = shift_held ? sc_ascii_shift[sc] : sc_ascii[sc];
                    if (c) vtty_input_char(vtty_active(), (u8)c);
                }
                continue;
            }

            if (g_compositor.state == COMPOSITOR_STATE_SETUP) {
                if (!is_mouse_btn && ev.value == KEY_PRESSED) {
                    u16 sc = ev.code;
                    char c = 0;
                    if (sc < 128) c = shift_held ? sc_ascii_shift[sc] : sc_ascii[sc];

                    if (c == '\n') {
                        finish_first_boot_setup();
                    } else if (c == 27) {
                        skip_first_boot_setup();
                    } else if (c == '\b') {
                        field_backspace(setup_focused_field());
                    } else if (c == '\t') {
                        g_compositor.setup_focus = (g_compositor.setup_focus + 1) % 3;
                    } else if (c >= 32 && c <= 126) {
                        g_compositor.setup_failed = false;
                        field_append(setup_focused_field(), c);
                    }
                } else if (is_mouse_btn && ev.value == KEY_PRESSED) {
                    i32 dw = g_compositor.scanout->width;
                    i32 dh = g_compositor.scanout->height;
                    i32 bx = (dw - 760) / 2;
                    i32 by = (dh - 430) / 2;
                    i32 cx = bx + 270;
                    if (g_compositor.cursor_x >= cx && g_compositor.cursor_x <= cx + 390) {
                        if (g_compositor.cursor_y >= by + 122 && g_compositor.cursor_y <= by + 164) {
                            g_compositor.setup_focus = 0;
                        } else if (g_compositor.cursor_y >= by + 200 && g_compositor.cursor_y <= by + 242) {
                            g_compositor.setup_focus = 1;
                        } else if (g_compositor.cursor_y >= by + 278 && g_compositor.cursor_y <= by + 320) {
                            g_compositor.setup_focus = 2;
                        }
                    }
                    if (g_compositor.cursor_y >= by + 356 && g_compositor.cursor_y <= by + 396) {
                        if (g_compositor.cursor_x >= cx && g_compositor.cursor_x <= cx + 176) {
                            finish_first_boot_setup();
                        } else if (g_compositor.cursor_x >= cx + 192 && g_compositor.cursor_x <= cx + 330) {
                            skip_first_boot_setup();
                        }
                    }
                }
                continue;
            }

            if (g_compositor.state == COMPOSITOR_STATE_LOGIN) {
                if (!is_mouse_btn && ev.value == KEY_PRESSED) {
                    u16 sc = ev.code;
                    char c = 0;
                    if (sc < 128) c = shift_held ? sc_ascii_shift[sc] : sc_ascii[sc];
                    
                    if (c == '\n') {
                        static yam_channel_t *auth_chan = NULL;
                        const char *attempt = g_compositor.login_pass;
                        int len = (int)strlen(attempt);
                        if (!auth_chan) {
                            yam_node_id_t authd_node = yamgraph_find_node_by_name("authd");
                            if (authd_node != (yam_node_id_t)-1) {
                                yam_node_id_t comp_node = sched_current()->graph_node;
                                auth_chan = channel_create("auth_channel", comp_node, authd_node);
                            }
                        }
                        
                        bool success = validate_account(g_compositor.login_user, g_compositor.login_pass);
                        if (!success && g_compositor.user_count == 0 && auth_chan) {
                            KINFO("AUTH", "Sending password attempt: '%s' (len=%d)", attempt, len);
                            channel_send(auth_chan, sched_current()->graph_node, 1, attempt, (u32)len);
                            
                            yam_message_t reply;
                            int retries = 50;
                            success = false;
                            while (retries-- > 0) {
                                if (channel_recv(auth_chan, sched_current()->graph_node, &reply)) {
                                    KINFO("AUTH", "Received reply: type=%d", reply.msg_type);
                                    if (reply.msg_type == 2) success = true;
                                    break;
                                }
                                task_sleep_ms(10);
                            }
                        }
                        
                        if (success) {
                            KINFO("AUTH", "Access GRANTED for '%s'. Transitioning to DESKTOP...", g_compositor.current_user);
                            /* Ensure home directory exists for this user */
                            {
                                char home[64];
                                ksnprintf(home, sizeof(home), "/home/%s", g_compositor.current_user);
                                sys_mkdir(home, 0755);
                            }
                            g_compositor.state = COMPOSITOR_STATE_DESKTOP;
                            KINFO("AUTH", "Desktop ready. Apps are launched from the dock or File menu.");
                        } else {
                                g_compositor.login_failed = true;
                                g_compositor.login_pass[0] = '\0';
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
                    i32 bx = (dw - 520) / 2;
                    i32 by = (dh - 360) / 2;
                    
                    if (g_compositor.cursor_x >= bx + 50 && g_compositor.cursor_x <= bx + 470) {
                        if (g_compositor.cursor_y >= by + 148 && g_compositor.cursor_y <= by + 190) {
                            g_compositor.login_focus_pass = false;
                        } else if (g_compositor.cursor_y >= by + 236 && g_compositor.cursor_y <= by + 278) {
                            g_compositor.login_focus_pass = true;
                        }
                    }
                }
            } else {
                /* DESKTOP STATE ROUTING */
                if (is_mouse_btn && (ev.value == KEY_PRESSED || ev.value == KEY_RELEASED)) {
                    /* Calculate Dock position (must match composite_taskbar) */
                    i32 app_count = 0;
                    for (int i = 0; i < WL_MAX_SURFACES; i++) {
                        if (g_compositor.surfaces[i].state == WL_SURFACE_ACTIVE ||
                            g_compositor.surfaces[i].state == WL_SURFACE_MINIMIZED) app_count++;
                    }
                    i32 dock_w = 116 + (app_count * 72);
                    if (dock_w > (i32)g_compositor.display_w - 40) dock_w = g_compositor.display_w - 40;
                    i32 dock_x = (g_compositor.display_w - dock_w) / 2;
                    i32 dock_y = g_compositor.display_h - 64 - 14;

                    if (is_right_btn && ev.value == KEY_PRESSED) {
                        g_compositor.context_menu_open = true;
                        g_compositor.context_x = g_compositor.cursor_x;
                        g_compositor.context_y = g_compositor.cursor_y;
                        g_compositor.context_surface_id = g_compositor.focused_id;
                        g_compositor.desktop_menu_open = 0;
                        g_compositor.show_power_menu = false;
                        g_compositor.calendar_open = false;
                        kprintf("[CLIPBOARD] context menu open at %d,%d\n",
                                g_compositor.context_x, g_compositor.context_y);
                        continue;
                    }

                    if (is_left_btn && ev.value == KEY_PRESSED && g_compositor.context_menu_open) {
                        i32 mx = g_compositor.context_x;
                        i32 my = g_compositor.context_y;
                        i32 mw = 168;
                        i32 mh = 104;
                        if (mx + mw >= (i32)g_compositor.display_w) mx = (i32)g_compositor.display_w - mw - 4;
                        if (my + mh >= (i32)g_compositor.display_h) my = (i32)g_compositor.display_h - mh - 4;
                        if (mx < 4) mx = 4;
                        if (my < 34) my = 34;
                        if (g_compositor.cursor_x >= mx && g_compositor.cursor_x < mx + mw &&
                            g_compositor.cursor_y >= my && g_compositor.cursor_y < my + mh) {
                            i32 row = (g_compositor.cursor_y - my) / 30;
                            if (row == 0) clipboard_copy_focused();
                            else if (row == 1) clipboard_paste_focused();
                            else kprintf("[CLIPBOARD] context menu canceled\n");
                        }
                        g_compositor.context_menu_open = false;
                        continue;
                    } else if (is_left_btn && ev.value == KEY_PRESSED) {
                        g_compositor.context_menu_open = false;
                    }

                    if (is_left_btn && ev.value == KEY_PRESSED && g_compositor.cursor_y >= 0 && g_compositor.cursor_y < 34) {
                        tray_regions_t tray = desktop_tray_regions();
                        if (g_compositor.cursor_x >= tray.clock_x) {
                            g_compositor.calendar_open = !g_compositor.calendar_open;
                            g_compositor.desktop_menu_open = 0;
                            g_compositor.show_power_menu = false;
                            kprintf("[DESKTOP] calendar %s\n", g_compositor.calendar_open ? "open" : "closed");
                            continue;
                        }
                        if (g_compositor.cursor_x >= tray.sound_x &&
                            g_compositor.cursor_x < tray.sound_x + tray.sound_w) {
                            open_or_focus_window("Sound Settings", "wl-sound-settings", wl_sound_settings_task);
                            g_compositor.calendar_open = false;
                            g_compositor.desktop_menu_open = 0;
                            g_compositor.show_power_menu = false;
                            kprintf("[DESKTOP] opening Sound Settings from tray\n");
                            continue;
                        }
                        if (g_compositor.cursor_x >= tray.net_x &&
                            g_compositor.cursor_x < tray.net_x + tray.net_w) {
                            open_or_focus_window("Ethernet Settings", "wl-net-settings", wl_network_settings_task);
                            g_compositor.calendar_open = false;
                            g_compositor.desktop_menu_open = 0;
                            g_compositor.show_power_menu = false;
                            kprintf("[DESKTOP] opening Ethernet Settings from tray\n");
                            continue;
                        }
                        if (tray.show_wifi && g_compositor.cursor_x >= tray.wifi_x &&
                            g_compositor.cursor_x < tray.wifi_x + tray.wifi_w) {
                            open_or_focus_window("Wi-Fi Settings", "wl-wifi-settings", wl_wifi_settings_task);
                            g_compositor.calendar_open = false;
                            g_compositor.desktop_menu_open = 0;
                            g_compositor.show_power_menu = false;
                            kprintf("[DESKTOP] opening Wi-Fi Settings from tray\n");
                            continue;
                        }
                        if (tray.show_bt && g_compositor.cursor_x >= tray.bt_x &&
                            g_compositor.cursor_x < tray.bt_x + tray.bt_w) {
                            open_or_focus_window("Bluetooth Settings", "wl-bt-settings", wl_bluetooth_settings_task);
                            g_compositor.calendar_open = false;
                            g_compositor.desktop_menu_open = 0;
                            g_compositor.show_power_menu = false;
                            kprintf("[DESKTOP] opening Bluetooth Settings from tray\n");
                            continue;
                        }
                        if (g_compositor.cursor_x >= 182 && g_compositor.cursor_x < 230) {
                            g_compositor.desktop_menu_open = (g_compositor.desktop_menu_open == 1) ? 0 : 1;
                            g_compositor.calendar_open = false;
                            kprintf("[WL_DBG] desktop menu File %s\n", g_compositor.desktop_menu_open ? "open" : "closed");
                            continue;
                        }
                        if (g_compositor.cursor_x >= 234 && g_compositor.cursor_x < 284) {
                            g_compositor.desktop_menu_open = (g_compositor.desktop_menu_open == 2) ? 0 : 2;
                            g_compositor.calendar_open = false;
                            kprintf("[WL_DBG] desktop menu View %s\n", g_compositor.desktop_menu_open ? "open" : "closed");
                            continue;
                        }
                        if (g_compositor.cursor_x >= 288 && g_compositor.cursor_x < 360) {
                            g_compositor.desktop_menu_open = (g_compositor.desktop_menu_open == 3) ? 0 : 3;
                            g_compositor.calendar_open = false;
                            kprintf("[WL_DBG] desktop menu Window %s\n", g_compositor.desktop_menu_open ? "open" : "closed");
                            continue;
                        }
                        g_compositor.desktop_menu_open = 0;
                    }

                    if (is_left_btn && ev.value == KEY_PRESSED && g_compositor.desktop_menu_open) {
                        i32 mx = 182;
                        if (g_compositor.desktop_menu_open == 2) mx = 234;
                        if (g_compositor.desktop_menu_open == 3) mx = 288;
                        i32 menu_h = g_compositor.desktop_menu_open == 1 ? 342 : 222;
                        if (g_compositor.cursor_x >= mx && g_compositor.cursor_x < mx + 204 &&
                            g_compositor.cursor_y >= 34 && g_compositor.cursor_y < menu_h) {
                            i32 row = (g_compositor.cursor_y - 42) / 30;
                            if (row < 0) row = 0;
                            if (g_compositor.desktop_menu_open == 1) {
                                if (row == 0) {
                                    kprintf("[WAYLAND] Menu launching Terminal (kernel compositor app)...\n");
                                    sched_spawn("wl-term-k", wl_term_task, NULL, 2);
                                } else if (row == 1) {
                                    kprintf("[WAYLAND] Menu launching Browser (kernel compositor app)...\n");
                                    sched_spawn("wl-browser-k", wl_browser_task, NULL, 2);
                                } else if (row == 2) {
                                    kprintf("[WAYLAND] Menu launching Calculator (kernel compositor app)...\n");
                                    sched_spawn("wl-calc-k", wl_calc_task, NULL, 2);
                                } else if (row == 3) {
                                    kprintf("[WAYLAND] Menu launching File Manager...\n");
                                    sched_spawn("wl-files-k", wl_file_manager_task, NULL, 2);
                                } else if (row == 4) {
                                    kprintf("[WAYLAND] Menu opening Ethernet Settings...\n");
                                    open_or_focus_window("Ethernet Settings", "wl-net-settings", wl_network_settings_task);
                                } else if (row == 5) {
                                    kprintf("[WAYLAND] Menu opening Wi-Fi Settings...\n");
                                    open_or_focus_window("Wi-Fi Settings", "wl-wifi-settings", wl_wifi_settings_task);
                                } else if (row == 6) {
                                    kprintf("[WAYLAND] Menu opening Bluetooth Settings...\n");
                                    open_or_focus_window("Bluetooth Settings", "wl-bt-settings", wl_bluetooth_settings_task);
                                } else if (row == 7) {
                                    kprintf("[WAYLAND] Menu opening Sound Settings...\n");
                                    open_or_focus_window("Sound Settings", "wl-sound-settings", wl_sound_settings_task);
                                } else {
                                    kprintf("[WAYLAND] Menu launching driver probe terminal...\n");
                                    sched_spawn("wl-term-k", wl_term_task, NULL, 2);
                                }
                            } else if (g_compositor.desktop_menu_open == 2) {
                                if (row == 0) {
                                    g_compositor.show_debug_overlay = !g_compositor.show_debug_overlay;
                                    kprintf("[WL_DBG] debug overlay from menu: %s\n", g_compositor.show_debug_overlay ? "on" : "off");
                                } else if (row == 1) {
                                    fb_clear(0xFF111827);
                                    kprintf("[WL_DBG] refresh from View menu\n");
                                } else if (row == 2) {
                                    kprintf("[WAYLAND] Menu opening Display Settings...\n");
                                    open_or_focus_window("Display Settings", "wl-display-settings", wl_display_settings_task);
                                }
                            } else {
                                wl_surface_t *focused = NULL;
                                for (int i = 0; i < WL_MAX_SURFACES; i++) {
                                    if (g_compositor.surfaces[i].id == g_compositor.focused_id) {
                                        focused = &g_compositor.surfaces[i];
                                        break;
                                    }
                                }
                                if (row == 0 && focused && focused->state == WL_SURFACE_ACTIVE) {
                                    kprintf("[WAYLAND] Menu close focused surface %u\n", focused->id);
                                    wl_surface_destroy(focused);
                                } else if (row == 1 && focused && focused->state == WL_SURFACE_ACTIVE) {
                                    kprintf("[WAYLAND] Menu minimize focused surface %u\n", focused->id);
                                    focused->state = WL_SURFACE_MINIMIZED;
                                    focused->focused = false;
                                    g_compositor.focused_id = 0;
                                } else if (row == 2 && focused && focused->state == WL_SURFACE_ACTIVE) {
                                    toggle_surface_maximize(focused);
                                } else {
                                    kprintf("[WAYLAND] Menu restore all surfaces\n");
                                    for (int i = 0; i < WL_MAX_SURFACES; i++) {
                                        if (g_compositor.surfaces[i].state == WL_SURFACE_MINIMIZED) {
                                            g_compositor.surfaces[i].state = WL_SURFACE_ACTIVE;
                                        }
                                    }
                                }
                            }
                            g_compositor.desktop_menu_open = 0;
                            continue;
                        }
                        g_compositor.desktop_menu_open = 0;
                    }

                    /* Check Power/App Menu Clicks */
                    if (is_left_btn && g_compositor.show_power_menu && ev.value == KEY_PRESSED) {
                        i32 mw = 520, mh = 440;
                        i32 mx = dock_x;
                        if (mx + mw > (i32)g_compositor.display_w - 12) mx = (i32)g_compositor.display_w - mw - 12;
                        if (mx < 12) mx = 12;
                        if (g_compositor.cursor_x >= mx && g_compositor.cursor_x <= mx + mw) {
                            if (g_compositor.cursor_y >= dock_y - mh - 10 && g_compositor.cursor_y < dock_y - 10) {
                                i32 rel_y = g_compositor.cursor_y - (dock_y - mh - 10);
                                i32 rel_x = g_compositor.cursor_x - mx;
                                if (rel_y >= 70 && rel_y <= 134) {
                                    int col = (rel_x - 35) / 92;
                                    if (col >= 0 && col < 5) {
                                        if (col == 0) sched_spawn("wl-term-k", wl_term_task, NULL, 2);
                                        else if (col == 1) sched_spawn("wl-browser-k", wl_browser_task, NULL, 2);
                                        else if (col == 2) sched_spawn("wl-calc-k", wl_calc_task, NULL, 2);
                                        else if (col == 3) sched_spawn("wl-files-k", wl_file_manager_task, NULL, 2);
                                        else if (col == 4) sched_spawn("wl-term-k", wl_term_task, NULL, 2);
                                    }
                                } else if (rel_y >= 180 && rel_y <= 244) {
                                    int col = (rel_x - 35) / 92;
                                    if (col >= 0 && col < 5) {
                                        if (col == 0) open_or_focus_window("Ethernet Settings", "wl-net-settings", wl_network_settings_task);
                                        else if (col == 1) open_or_focus_window("Wi-Fi Settings", "wl-wifi-settings", wl_wifi_settings_task);
                                        else if (col == 2) open_or_focus_window("Bluetooth Settings", "wl-bt-settings", wl_bluetooth_settings_task);
                                        else if (col == 3) open_or_focus_window("Sound Settings", "wl-sound-settings", wl_sound_settings_task);
                                        else if (col == 4) open_or_focus_window("Display Settings", "wl-display-settings", wl_display_settings_task);
                                    }
                                } else if (rel_y >= 370 && rel_y <= 406) {
                                    if (rel_x >= 35 && rel_x < 175) {
                                        kprintf("[AUTH] Locking desktop for switch-user login\n");
                                        g_compositor.state = COMPOSITOR_STATE_LOGIN;
                                        g_compositor.login_pass[0] = '\0';
                                        g_compositor.login_failed = false;
                                        g_compositor.login_focus_pass = false;
                                        g_compositor.current_user[0] = '\0';
                                    } else if (rel_x >= 190 && rel_x < 330) {
                                        kprintf("[POWER] Restarting system...\n");
                                        outb(0x64, 0xFE);
                                    } else if (rel_x >= 345 && rel_x < 485) {
                                        kprintf("[POWER] Shutting down...\n");
                                        outw(0x604, 0x2000);
                                    }
                                }
                                g_compositor.show_power_menu = false;
                                continue;
                            }
                        }
                    }

                    /* Check Taskbar/Dock clicks */
                    if (is_left_btn && ev.value == KEY_PRESSED && g_compositor.cursor_y >= dock_y && g_compositor.cursor_y <= dock_y + 64) {
                        /* Launcher tile */
                        if (g_compositor.cursor_x >= dock_x + 10 && g_compositor.cursor_x <= dock_x + 58) {
                            g_compositor.show_power_menu = !g_compositor.show_power_menu;
                            g_compositor.calendar_open = false;
                        } else {
                            /* Check Dock App Icons */
                            i32 abx = dock_x + 76;
                            for (int i = 0; i < WL_MAX_SURFACES; i++) {
                                wl_surface_t *s = &g_compositor.surfaces[i];
                                if (s->state == WL_SURFACE_ACTIVE || s->state == WL_SURFACE_MINIMIZED) {
                                    if (g_compositor.cursor_x >= abx && g_compositor.cursor_x <= abx + 54) {
                                        if (s->state == WL_SURFACE_MINIMIZED) s->state = WL_SURFACE_ACTIVE;
                                        wl_surface_focus(s);
                                        break;
                                    }
                                    abx += 72;
                                }
                            }
                        }
                        continue;
                    } else if (is_left_btn) {
                        if (ev.value == KEY_PRESSED &&
                            g_compositor.calendar_open &&
                            g_compositor.cursor_y >= 34) {
                            g_compositor.calendar_open = false;
                        }
                        /* Mouse button */
                        if (ev.value == KEY_RELEASED) {
                            g_compositor.dragging = false;
                        }
                        
                        /* Route to surface under cursor */
                        for (int i = WL_MAX_SURFACES - 1; i >= 0; i--) {
                            wl_surface_t *s = &g_compositor.surfaces[i];
                            if (s->state == WL_SURFACE_ACTIVE) {
                                i32 fx, fy, fw, fh;
                                surface_frame_rect(s, &fx, &fy, &fw, &fh);
                                if (g_compositor.cursor_x >= fx && g_compositor.cursor_x < fx + fw &&
                                    g_compositor.cursor_y >= fy && g_compositor.cursor_y < fy + fh) {
                                    
                                    if (ev.value == KEY_PRESSED && g_compositor.cursor_y < fy + 36) {
                                        /* Clicked Titlebar area */
                                        i32 close_x = fx + fw - 42;
                                        i32 max_x = close_x - 40;
                                        i32 min_x = max_x - 40;
                                        if (g_compositor.cursor_x >= min_x && g_compositor.cursor_x < min_x + 34) {
                                            kprintf("[WAYLAND] Titlebar minimize surface %u ('%s')\n", s->id, s->title);
                                            s->state = WL_SURFACE_MINIMIZED;
                                            s->focused = false;
                                            if (g_compositor.focused_id == s->id) g_compositor.focused_id = 0;
                                        } else if (g_compositor.cursor_x >= max_x && g_compositor.cursor_x < max_x + 34) {
                                            toggle_surface_maximize(s);
                                        } else if (g_compositor.cursor_x >= close_x && g_compositor.cursor_x < close_x + 34) {
                                            kprintf("[WAYLAND] Titlebar close surface %u ('%s')\n", s->id, s->title);
                                            wl_surface_destroy(s);
                                        } else {
                                            /* Start Dragging */
                                            wl_surface_focus(s);
                                            if (!s->maximized) {
                                                g_compositor.dragging = true;
                                                g_compositor.drag_surface_id = i;
                                                g_compositor.drag_off_x = g_compositor.cursor_x - s->x;
                                                g_compositor.drag_off_y = g_compositor.cursor_y - s->y;
                                            }
                                        }
                                    } else {
                                        if (ev.value == KEY_PRESSED) wl_surface_focus(s);
                                        /* Push mouse coordinates relative to content area (below titlebar) */
                                        i32 content_h = fh - 36;
                                        i32 rel_x = g_compositor.cursor_x - fx;
                                        i32 rel_y = g_compositor.cursor_y - (fy + 36);
                                        if (content_h > 0) {
                                            rel_x = (rel_x * (i32)s->width) / fw;
                                            rel_y = (rel_y * (i32)s->height) / content_h;
                                        }
                                        input_event_t ex = { .type = EV_ABS, .code = 0, .value = rel_x };
                                        input_event_t ey = { .type = EV_ABS, .code = 1, .value = rel_y };
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
