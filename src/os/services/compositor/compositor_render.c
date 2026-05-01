/* YamKernel - compositor rendering paths */
#include "compositor_internal.h"

static void composite_surface(wl_surface_t *s) {
    if (s->state != WL_SURFACE_ACTIVE || !s->buffer) return;
    s->composite_count++;
    if (s->composite_count <= 3 || (s->composite_count % 180) == 0) {
        kprintf("[WL_DBG] composite id=%u title='%s' frame=%u commits=%u checksum=0x%x pos=%d,%d size=%ux%u focus=%d\n",
                s->id, s->title, s->composite_count, s->commit_count, s->last_checksum,
                s->x, s->y, s->width, s->height, s->focused ? 1 : 0);
    }

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
    (void)dh;
    
    /* Window decorations */
    u32 title_h = 30;
    u32 corner_radius = 10;

    /* Animated Size */
    u32 sw = (s->width * s->anim_scale) / 100;
    u32 sh = ((s->height + title_h) * s->anim_scale) / 100;
    
    /* Center on (s->x, s->y) during scale animation */
    i32 ax = s->x + (i32)(s->width - sw) / 2;
    i32 ay = s->y + (i32)(s->height + title_h - sh) / 2;
    
    /* Bounding box */
    i32 start_y = ay < 0 ? 0 : ay;
    i32 start_x = ax < 0 ? 0 : ax;
    i32 end_y = ay + (i32)sh > (i32)dh ? (i32)dh : ay + (i32)sh;
    i32 end_x = ax + (i32)sw > (i32)dw ? (i32)dw : ax + (i32)sw;
    
    /* Avoid division by zero if window is too small during animation */
    if (sw < 1 || sh < title_h) return;
    
    for (i32 y = start_y; y < end_y; y++) {
        u32 sy = (u32)(y - ay);
        for (i32 x = start_x; x < end_x; x++) {
            u32 sx = (u32)(x - ax);
            
            /* Corner masking */
            bool mask = false;
            if (sy < corner_radius && sx < corner_radius) {
                i32 dx = corner_radius - sx, dy = corner_radius - sy;
                if (dx*dx + dy*dy > (i32)(corner_radius*corner_radius)) mask = true;
            } else if (sy < corner_radius && sx >= sw - corner_radius) {
                i32 dx = sx - (sw - corner_radius), dy = corner_radius - sy;
                if (dx*dx + dy*dy > (i32)(corner_radius*corner_radius)) mask = true;
            } else if (sy >= sh - corner_radius && sx < corner_radius) {
                i32 dx = corner_radius - sx, dy = sy - (sh - corner_radius);
                if (dx*dx + dy*dy > (i32)(corner_radius*corner_radius)) mask = true;
            } else if (sy >= sh - corner_radius && sx >= sw - corner_radius) {
                i32 dx = sx - (sw - corner_radius), dy = sy - (sh - corner_radius);
                if (dx*dx + dy*dy > (i32)(corner_radius*corner_radius)) mask = true;
            }
            if (mask) continue;

            u32 color = 0;
            if (sy < title_h) {
                /* Glassmorphic titlebar (60/40 blend) */
                u32 bg_color = dst[y * dw + x];
                u32 r1 = (bg_color >> 16) & 0xFF, g1 = (bg_color >> 8) & 0xFF, b1 = bg_color & 0xFF;
                u32 r2 = (s->title_bg >> 16) & 0xFF, g2 = (s->title_bg >> 8) & 0xFF, b2 = s->title_bg & 0xFF;
                
                u32 nr = (r1 * 40 + r2 * 60) / 100;
                u32 ng = (g1 * 40 + g2 * 60) / 100;
                u32 nb = (b1 * 40 + b2 * 60) / 100;
                color = 0xFF000000 | (nr << 16) | (ng << 8) | nb;
                
                /* Traffic light buttons */
                i32 bx = 16, by = 14, br = 6, sp = 20;
                if (sy >= (u32)(by - br) && sy <= (u32)(by + br)) {
                    i32 dy = (i32)sy - by;
                    i32 dx0 = (i32)sx - bx;
                    i32 dx1 = (i32)sx - (bx + sp);
                    i32 dx2 = (i32)sx - (bx + sp * 2);
                    if (dx0 * dx0 + dy * dy <= br * br) color = 0xFFFF5F57;
                    else if (dx1 * dx1 + dy * dy <= br * br) color = 0xFFFEBC2E;
                    else if (dx2 * dx2 + dy * dy <= br * br) color = 0xFF28C840;
                }
            } else {
                /* Content area */
                u32 orig_x = (sx * s->width) / sw;
                u32 orig_y = ((sy - title_h) * s->height) / (sh - title_h);
                if (orig_x >= s->width) orig_x = s->width - 1;
                if (orig_y >= s->height) orig_y = s->height - 1;
                color = src[orig_y * s->width + orig_x];
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
    }
    
    /* Draw Title Text (centered) */
    if (sw > 100) {
        wl_surface_t ds = {0};
        ds.buffer = g_compositor.scanout;
        ds.width = dw;
        ds.height = dh;
        
        u32 tw = strlen(s->title) * 8;
        i32 tx = ax + (i32)(sw - tw) / 2;
        i32 ty = ay + (i32)(title_h - 16) / 2;
        
        /* Ensure we don't overlap traffic lights too much */
        if (tx < ax + 80) tx = ax + 80;
        
        wl_draw_text(&ds, tx, ty, s->title, 0xFFF8F8F2, 0);
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

static void draw_setup_field(wl_surface_t *ds, i32 x, i32 y, const char *label,
                             const char *value, bool password, bool focused) {
    wl_draw_text(ds, x, y, label, 0xFFE8EEF7, 0);
    wl_draw_rect(ds, x, y + 20, 360, 40, focused ? 0xFF2563EB : 0xFF293447);

    char hidden[32] = {0};
    const char *shown = value;
    int len = strlen(value);
    if (password) {
        for (int i = 0; i < len && i < 31; i++) hidden[i] = '*';
        shown = hidden;
    }
    wl_draw_text(ds, x + 12, y + 32, shown, 0xFFFFFFFF, 0);
    if (focused) {
        int cx = x + 12 + len * 8;
        if (cx > x + 348) cx = x + 348;
        wl_draw_rect(ds, cx, y + 32, 8, 16, 0xFFFFFFFF);
    }
}

static void composite_setup_screen(void) {
    u32 *dst = g_compositor.scanout->pixels;
    u32 dw = g_compositor.scanout->width;
    u32 dh = g_compositor.scanout->height;
    wl_surface_t ds = { .buffer = g_compositor.scanout, .width = dw, .height = dh };

    for (u32 y = 0; y < dh; y++) {
        for (u32 x = 0; x < dw; x++) {
            u32 c = dst[y * dw + x];
            u32 r = (((c >> 16) & 0xFF) * 2) / 3;
            u32 g = (((c >> 8) & 0xFF) * 2) / 3;
            u32 b = ((c & 0xFF) * 2) / 3;
            dst[y * dw + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }

    i32 box_w = 480;
    i32 box_h = 360;
    i32 bx = (dw - box_w) / 2;
    i32 by = (dh - box_h) / 2;

    wl_draw_rect(&ds, bx, by, box_w, box_h, 0xFF111827);
    wl_draw_rect(&ds, bx, by, box_w, 3, 0xFF36D399);
    wl_draw_rect(&ds, bx, by + box_h - 1, box_w, 1, 0xFF374151);

    wl_draw_text(&ds, bx + 60, by + 34, "Set up YamOS", 0xFF36D399, 0);
    wl_draw_text(&ds, bx + 60, by + 58, "Create this computer and first administrator", 0xFF9CA3AF, 0);

    draw_setup_field(&ds, bx + 60, by + 86, "Computer name",
                     g_compositor.setup_computer, false, g_compositor.setup_focus == 0);
    draw_setup_field(&ds, bx + 60, by + 156, "Username",
                     g_compositor.setup_user, false, g_compositor.setup_focus == 1);
    draw_setup_field(&ds, bx + 60, by + 226, "Password",
                     g_compositor.setup_pass, true, g_compositor.setup_focus == 2);

    if (g_compositor.setup_failed) {
        wl_draw_text(&ds, bx + 60, by + 306, "Fill all fields. Password needs 4+ characters.", 0xFFFF5C70, 0);
    } else {
        wl_draw_text(&ds, bx + 60, by + 306, "TAB moves fields. ENTER creates the account.", 0xFF9CA3AF, 0);
        wl_draw_text(&ds, bx + 60, by + 328, "Ctrl+Shift+Y auto-creates root and opens desktop.", 0xFF9CA3AF, 0);
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
    wl_draw_text(&ds, bx + 112, by + 40, "YamOS Desktop", 0xFF50FA7B, 0);
    wl_draw_text(&ds, bx + 104, by + 60, g_compositor.computer_name, 0xFF8BE9FD, 0);
    
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

static void composite_menubar(void) {
    u32 *dst = g_compositor.scanout->pixels;
    u32 dw = g_compositor.scanout->width;
    u32 dh = g_compositor.scanout->height;
    (void)dh;
    
    wl_surface_t ds = { .buffer = g_compositor.scanout, .width = dw, .height = dh };
    u32 bar_h = 30;
    
    /* Draw Glass Menubar */
    for (u32 y = 0; y < bar_h; y++) {
        for (u32 x = 0; x < dw; x++) {
            u32 c = dst[y * dw + x];
            u32 r1 = (c >> 16) & 0xFF, g1 = (c >> 8) & 0xFF, b1 = c & 0xFF;
            u32 nr = (r1 * 45 + 0x16 * 55) / 100;
            u32 ng = (g1 * 45 + 0x1C * 55) / 100;
            u32 nb = (b1 * 45 + 0x28 * 55) / 100;
            dst[y * dw + x] = 0xFF000000 | (nr << 16) | (ng << 8) | nb;
        }
    }
    wl_draw_rect(&ds, 0, bar_h - 1, dw, 1, 0x553B4658);
    
    /* Desktop launcher mark */
    wl_draw_filled_circle(&ds, 22, 15, 7, 0xFFE8EEF7);
    wl_draw_text(&ds, 18, 8, "Y", 0xFF111827, 0);
    
    /* Active App Title */
    const char *active_title = "YamOS Desktop";
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (g_compositor.surfaces[i].focused) {
            active_title = g_compositor.surfaces[i].title;
            break;
        }
    }
    wl_draw_text(&ds, 44, 8, active_title, 0xFFFFFFFF, 0);

    if (g_compositor.current_user[0]) {
        char session[80];
        ksnprintf(session, sizeof(session), "%s@%s", g_compositor.current_user, g_compositor.computer_name);
        wl_draw_text(&ds, 372, 8, session, 0xFF9AA8BA, 0);
    }

    u32 file_bg = (g_compositor.desktop_menu_open == 1) ? 0xFF44475A : 0x00000000;
    u32 view_bg = (g_compositor.desktop_menu_open == 2) ? 0xFF44475A : 0x00000000;
    u32 win_bg  = (g_compositor.desktop_menu_open == 3) ? 0xFF44475A : 0x00000000;
    if (file_bg) wl_draw_rect(&ds, 182, 5, 48, 20, file_bg);
    if (view_bg) wl_draw_rect(&ds, 234, 5, 50, 20, view_bg);
    if (win_bg)  wl_draw_rect(&ds, 288, 5, 72, 20, win_bg);
    wl_draw_text(&ds, 192, 8, "File", 0xFFE8EEF7, 0);
    wl_draw_text(&ds, 244, 8, "View", 0xFFE8EEF7, 0);
    wl_draw_text(&ds, 298, 8, "Window", 0xFFE8EEF7, 0);
    
    /* Clock and Stats on Right */
    rtc_time_t t;
    rtc_read(&t);
    char time_str[16];
    int bdt_hour = (t.hour + 6) % 24;
    int hour12 = bdt_hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char *ampm = (bdt_hour >= 12) ? "PM" : "AM";
    ksnprintf(time_str, sizeof(time_str), "%d:%02d:%02d %s", hour12, t.minute, t.second, ampm);
    wl_draw_text(&ds, dw - 112, 8, time_str, 0xFFE8EEF7, 0);
    
    wl_draw_rect(&ds, dw - 148, 18, 4, 4, 0xFF34D399);
    wl_draw_rect(&ds, dw - 142, 14, 4, 8, 0xFF34D399);
    wl_draw_rect(&ds, dw - 136, 10, 4, 12, 0xFF34D399);
    wl_draw_text(&ds, dw - 204, 8, "YamNet", 0xFF9AA8BA, 0);

    if (g_compositor.desktop_menu_open) {
        i32 mx = 182;
        i32 mw = 204;
        if (g_compositor.desktop_menu_open == 2) mx = 234;
        if (g_compositor.desktop_menu_open == 3) mx = 288;
        wl_draw_rect(&ds, mx, 34, mw, 158, 0xF0141C2B);
        wl_draw_rect(&ds, mx, 34, mw, 1, 0xFF60A5FA);
        wl_draw_rect(&ds, mx, 191, mw, 1, 0x553B4658);
        if (g_compositor.desktop_menu_open == 1) {
            wl_draw_text(&ds, mx + 14, 50, "New Terminal", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 80, "New Browser", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 110, "New Calculator", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 140, "Python", 0xFFE8EEF7, 0);
        } else if (g_compositor.desktop_menu_open == 2) {
            wl_draw_text(&ds, mx + 14, 50, "Toggle Debug", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 80, "Refresh Screen", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 110, "Hide Menu", 0xFFE8EEF7, 0);
        } else {
            wl_draw_text(&ds, mx + 14, 50, "Close Focused", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 80, "Minimize Focused", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 110, "Restore All", 0xFFE8EEF7, 0);
        }
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
    
    /* Calculate floating dock width based on running/minimized apps */
    i32 shown_apps = 0;
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (g_compositor.surfaces[i].state == WL_SURFACE_ACTIVE ||
            g_compositor.surfaces[i].state == WL_SURFACE_MINIMIZED) shown_apps++;
    }
    
    i32 dock_w = 116 + (shown_apps * 72);
    if (dock_w > (i32)dw - 40) dock_w = dw - 40;
    i32 dock_h = 64;
    i32 dock_x = (dw - dock_w) / 2;
    i32 dock_y = dh - dock_h - 14;
    
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
    
    /* Draw launcher tile */
    i32 bx = dock_x + 10;
    u32 launcher_bg = g_compositor.show_power_menu ? 0xFF60A5FA : 0xFFE8EEF7;
    wl_draw_rect(&ds, bx, dock_y + 10, 48, 44, launcher_bg);
    wl_draw_text(&ds, bx + 12, dock_y + 24, "YAM", 0xFF111827, 0);
    bx += 66;
    
    /* Draw Power Menu if active */
    if (g_compositor.show_power_menu) {
        i32 mw = 300;
        i32 mh = 344;
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
        
        wl_draw_text(&ds, mx + 20, my + 18, "YamOS Launcher", 0xFFE8EEF7, 0);
        wl_draw_text(&ds, mx + 20, my + 42, "Applications", 0xFF9AA8BA, 0);

        wl_draw_rect(&ds, mx + 18, my + 68, mw - 36, 42, 0xFF1F2937);
        wl_draw_text(&ds, mx + 34, my + 81, "Terminal", 0xFFBD93F9, 0);
        wl_draw_text(&ds, mx + 176, my + 81, "Shell", 0xFF9AA8BA, 0);

        wl_draw_rect(&ds, mx + 18, my + 116, mw - 36, 42, 0xFF1F2937);
        wl_draw_text(&ds, mx + 34, my + 129, "Browser", 0xFF8BE9FD, 0);
        wl_draw_text(&ds, mx + 176, my + 129, "Web", 0xFF9AA8BA, 0);

        wl_draw_rect(&ds, mx + 18, my + 164, mw - 36, 42, 0xFF1F2937);
        wl_draw_text(&ds, mx + 34, my + 177, "Calculator", 0xFF50FA7B, 0);
        wl_draw_text(&ds, mx + 176, my + 177, "Tools", 0xFF9AA8BA, 0);

        wl_draw_rect(&ds, mx + 18, my + 212, mw - 36, 42, 0xFF1F2937);
        wl_draw_text(&ds, mx + 34, my + 225, "Python", 0xFFFFD166, 0);
        wl_draw_text(&ds, mx + 176, my + 225, "Runtime", 0xFF9AA8BA, 0);

        wl_draw_rect(&ds, mx + 18, my + 260, mw - 36, 34, 0xFF293447);
        wl_draw_text(&ds, mx + 34, my + 269, "Lock / Switch User", 0xFFE8EEF7, 0);

        wl_draw_rect(&ds, mx + 18, my + 304, 120, 28, 0xFF374151);
        wl_draw_text(&ds, mx + 42, my + 310, "Restart", 0xFFE8EEF7, 0);
        wl_draw_rect(&ds, mx + 156, my + 304, 120, 28, 0xFF7F1D1D);
        wl_draw_text(&ds, mx + 176, my + 310, "Shutdown", 0xFFFFCACA, 0);
    }
    
    /* Draw running apps */
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        wl_surface_t *s = &g_compositor.surfaces[i];
        if (s->state == WL_SURFACE_ACTIVE || s->state == WL_SURFACE_MINIMIZED) {
            bool minimized = (s->state == WL_SURFACE_MINIMIZED);
            u32 bg = s->focused ? 0xFF60A5FA : minimized ? 0xFF273244 : 0xFF374151;
            u32 accent = 0xFF8BE9FD;
            const char *glyph = "?";
            if (strstr(s->title, "Terminal")) { glyph = ">_"; accent = 0xFFBD93F9; }
            else if (strstr(s->title, "Browser")) { glyph = "W"; accent = 0xFF8BE9FD; }
            else if (strstr(s->title, "Calculator")) { glyph = "+"; accent = 0xFF50FA7B; }

            wl_draw_rect(&ds, bx, dock_y + 10, 54, 44, bg);
            wl_draw_rect(&ds, bx, dock_y + 10, 54, 3, accent);
            wl_draw_text(&ds, bx + 16, dock_y + 24, glyph, 0xFFFFFFFF, 0);
            if (minimized) wl_draw_rect(&ds, bx + 12, dock_y + 48, 30, 2, 0xFF9AA8BA);
            else wl_draw_rect(&ds, bx + 23, dock_y + 57, 8, 2, 0xFFE8EEF7);
            bx += 72;
        }
    }
}

static void composite_context_menu(void) {
    if (!g_compositor.context_menu_open) return;

    u32 dw = g_compositor.scanout->width;
    u32 dh = g_compositor.scanout->height;
    wl_surface_t ds = { .buffer = g_compositor.scanout, .width = dw, .height = dh };

    i32 mw = 168;
    i32 mh = 104;
    i32 mx = g_compositor.context_x;
    i32 my = g_compositor.context_y;
    if (mx + mw >= (i32)dw) mx = (i32)dw - mw - 4;
    if (my + mh >= (i32)dh) my = (i32)dh - mh - 4;
    if (mx < 4) mx = 4;
    if (my < 34) my = 34;

    wl_draw_rect(&ds, mx, my, mw, mh, 0xF0141C2B);
    wl_draw_rect(&ds, mx, my, mw, 1, 0xFF60A5FA);
    wl_draw_rect(&ds, mx, my + mh - 1, mw, 1, 0x553B4658);

    wl_draw_text(&ds, mx + 14, my + 14, "Copy", 0xFFE8EEF7, 0);
    wl_draw_text(&ds, mx + 14, my + 44, "Paste", 0xFFE8EEF7, 0);
    wl_draw_text(&ds, mx + 14, my + 74, "Cancel", 0xFF9AA8BA, 0);
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


void wl_compositor_render_frame(void) {
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
        for (u32 i = 0; i < g_compositor.scanout->size / 4; i++) g_compositor.scanout->pixels[i] = bg_color;
    }

    if (g_compositor.state == COMPOSITOR_STATE_SETUP) {
        for (int i = 0; i < WL_MAX_SURFACES; i++) {
            if (!g_compositor.surfaces[i].focused) composite_surface(&g_compositor.surfaces[i]);
        }
        for (int i = 0; i < WL_MAX_SURFACES; i++) {
            if (g_compositor.surfaces[i].focused) { composite_surface(&g_compositor.surfaces[i]); break; }
        }
        composite_menubar();
        composite_taskbar();
        composite_setup_screen();
    } else if (g_compositor.state == COMPOSITOR_STATE_LOGIN) {
        composite_login_screen();
    } else if (g_compositor.state == COMPOSITOR_STATE_VTTY) {
        vtty_render();
    } else {
        for (int i = 0; i < WL_MAX_SURFACES; i++) {
            if (!g_compositor.surfaces[i].focused) composite_surface(&g_compositor.surfaces[i]);
        }
        for (int i = 0; i < WL_MAX_SURFACES; i++) {
            if (g_compositor.surfaces[i].focused) { composite_surface(&g_compositor.surfaces[i]); break; }
        }
        composite_menubar();
        composite_taskbar();
        composite_context_menu();
    }

    composite_debug_overlay();
    composite_heartbeat();
    composite_cursor();
    drm_page_flip(g_compositor.scanout);
}
