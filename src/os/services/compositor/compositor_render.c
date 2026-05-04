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
    u32 title_h = 36;
    u32 corner_radius = s->maximized ? 0 : 8;
    u32 frame_w = s->maximized ? (dw - 24) : s->width;
    u32 frame_content_h = s->maximized ? (dh - 126) : s->height;
    i32 frame_x = s->maximized ? 12 : s->x;
    i32 frame_y = s->maximized ? 42 : s->y;

    /* Animated Size */
    u32 sw = (frame_w * s->anim_scale) / 100;
    u32 sh = ((frame_content_h + title_h) * s->anim_scale) / 100;
    
    /* Center on (s->x, s->y) during scale animation */
    i32 ax = frame_x + (i32)(frame_w - sw) / 2;
    i32 ay = frame_y + (i32)(frame_content_h + title_h - sh) / 2;
    
    /* Bounding box */
    i32 start_y = ay < 0 ? 0 : ay;
    i32 start_x = ax < 0 ? 0 : ax;
    i32 end_y = ay + (i32)sh > (i32)dh ? (i32)dh : ay + (i32)sh;
    i32 end_x = ax + (i32)sw > (i32)dw ? (i32)dw : ax + (i32)sw;
    
    /* Avoid division by zero if window is too small during animation */
    if (sw < 1 || sh < title_h) return;

    if (!s->maximized && s->anim_alpha > 80) {
        wl_surface_t ds_shadow = { .buffer = g_compositor.scanout, .width = dw, .height = dh };
        wl_draw_rounded_rect(&ds_shadow, ax + 10, ay + 12, sw, sh, 10, 0xFF05070A);
        wl_draw_rounded_rect(&ds_shadow, ax + 4, ay + 6, sw, sh, 9, 0xFF0A0F1A);
    }
    
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
                /* Focus-aware titlebar */
                u32 bg_color = dst[y * dw + x];
                u32 r1 = (bg_color >> 16) & 0xFF, g1 = (bg_color >> 8) & 0xFF, b1 = bg_color & 0xFF;
                u32 title_color = s->focused ? 0xFF172033 : 0xFF242B38;
                u32 r2 = (title_color >> 16) & 0xFF, g2 = (title_color >> 8) & 0xFF, b2 = title_color & 0xFF;
                
                u32 nr = (r1 * 28 + r2 * 72) / 100;
                u32 ng = (g1 * 28 + g2 * 72) / 100;
                u32 nb = (b1 * 28 + b2 * 72) / 100;
                color = 0xFF000000 | (nr << 16) | (ng << 8) | nb;

                if (sy == title_h - 1) color = s->focused ? 0xFF3B82F6 : 0xFF343B49;

                /* Right-side window controls: minimize, maximize/restore, close */
                i32 btn_y = 6;
                i32 btn_w = 34;
                i32 btn_h = 24;
                i32 close_x = (i32)sw - 42;
                i32 max_x = close_x - 40;
                i32 min_x = max_x - 40;
                if ((i32)sy >= btn_y && (i32)sy < btn_y + btn_h) {
                    bool in_min = (i32)sx >= min_x && (i32)sx < min_x + btn_w;
                    bool in_max = (i32)sx >= max_x && (i32)sx < max_x + btn_w;
                    bool in_close = (i32)sx >= close_x && (i32)sx < close_x + btn_w;
                    if (in_min) color = 0xFF334155;
                    if (in_max) color = 0xFF334155;
                    if (in_close) color = 0xFF7F1D1D;
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
    
    /* Draw window title and control glyphs */
    if (sw > 100) {
        wl_surface_t ds = {0};
        ds.buffer = g_compositor.scanout;
        ds.width = dw;
        ds.height = dh;
        
        i32 ty = ay + 10;
        wl_draw_filled_circle(&ds, ax + 18, ay + 18, 7, s->focused ? 0xFF38BDF8 : 0xFF64748B);
        wl_draw_text(&ds, ax + 34, ty, s->title, s->focused ? 0xFFFFFFFF : 0xFFCBD5E1, 0);

        i32 close_x = ax + (i32)sw - 42;
        i32 max_x = close_x - 40;
        i32 min_x = max_x - 40;
        wl_draw_text(&ds, min_x + 12, ty, "-", 0xFFE2E8F0, 0);
        wl_draw_text(&ds, max_x + 11, ty, s->maximized ? "[]" : "^", 0xFFE2E8F0, 0);
        wl_draw_text(&ds, close_x + 12, ty, "X", 0xFFFFE4E6, 0);
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
    wl_draw_text(ds, x, y, label, focused ? 0xFF93C5FD : 0xFFE8EEF7, 0);
    wl_draw_rounded_rect(ds, x, y + 22, 390, 42, 7, focused ? 0xFF1D4ED8 : 0xFF202A3A);
    wl_draw_rounded_rect(ds, x + 1, y + 23, 388, 40, 6, focused ? 0xFF111C35 : 0xFF111827);

    char hidden[32] = {0};
    const char *shown = value;
    int len = strlen(value);
    if (password) {
        for (int i = 0; i < len && i < 31; i++) hidden[i] = '*';
        shown = hidden;
    }
    wl_draw_text(ds, x + 14, y + 35, shown, 0xFFFFFFFF, 0);
    if (focused) {
        int cx = x + 14 + len * 8;
        if (cx > x + 376) cx = x + 376;
        wl_draw_rect(ds, cx, y + 35, 8, 16, 0xFFFFFFFF);
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

    i32 box_w = 760;
    i32 box_h = 430;
    i32 bx = (dw - box_w) / 2;
    i32 by = (dh - box_h) / 2;

    wl_draw_rounded_rect(&ds, bx + 8, by + 10, box_w, box_h, 12, 0xFF05070A);
    wl_draw_rounded_rect(&ds, bx, by, box_w, box_h, 12, 0xFF0F172A);
    wl_draw_rounded_rect(&ds, bx, by, 220, box_h, 12, 0xFF111827);
    wl_draw_rect(&ds, bx + 220, by, 1, box_h, 0xFF263244);
    wl_draw_rect(&ds, bx, by, box_w, 3, 0xFF38BDF8);
    wl_draw_rect(&ds, bx, by + box_h - 1, box_w, 1, 0xFF374151);

    wl_draw_text(&ds, bx + 32, by + 34, "YamOS", 0xFFFFFFFF, 0);
    wl_draw_text(&ds, bx + 32, by + 62, "First boot setup", 0xFF94A3B8, 0);
    wl_draw_rounded_rect(&ds, bx + 30, by + 112, 156, 34, 6, 0xFF1E293B);
    wl_draw_text(&ds, bx + 46, by + 122, "1  Device", 0xFFBAE6FD, 0);
    wl_draw_rounded_rect(&ds, bx + 30, by + 158, 156, 34, 6, 0xFF1E293B);
    wl_draw_text(&ds, bx + 46, by + 168, "2  Account", 0xFFBAE6FD, 0);
    wl_draw_rounded_rect(&ds, bx + 30, by + 204, 156, 34, 6, 0xFF1E293B);
    wl_draw_text(&ds, bx + 46, by + 214, "3  Ready", 0xFFBAE6FD, 0);
    wl_draw_text(&ds, bx + 32, by + box_h - 62, "Ctrl+Shift+Y", 0xFF38BDF8, 0);
    wl_draw_text(&ds, bx + 32, by + box_h - 40, "persist default admin", 0xFF94A3B8, 0);

    i32 cx = bx + 270;
    wl_draw_text(&ds, cx, by + 36, "Set up this YamOS device", 0xFFFFFFFF, 0);
    wl_draw_text(&ds, cx, by + 62, "Create the computer identity and first administrator.", 0xFF9CA3AF, 0);

    draw_setup_field(&ds, cx, by + 100, "Computer name",
                     g_compositor.setup_computer, false, g_compositor.setup_focus == 0);
    draw_setup_field(&ds, cx, by + 178, "Username",
                     g_compositor.setup_user, false, g_compositor.setup_focus == 1);
    draw_setup_field(&ds, cx, by + 256, "Password",
                     g_compositor.setup_pass, true, g_compositor.setup_focus == 2);

    wl_draw_rounded_rect(&ds, cx, by + 356, 176, 40, 7, 0xFF2563EB);
    wl_draw_text(&ds, cx + 34, by + 368, "Create account", 0xFFFFFFFF, 0);
    wl_draw_rounded_rect(&ds, cx + 192, by + 356, 138, 40, 7, 0xFF1E293B);
    wl_draw_text(&ds, cx + 228, by + 368, "Bypass", 0xFFE2E8F0, 0);

    if (g_compositor.setup_failed) {
        wl_draw_text(&ds, cx, by + 324, "Setup could not be saved. Check writable /var storage.", 0xFFFF5C70, 0);
    } else {
        wl_draw_text(&ds, cx, by + 324, "Saved setup is reused after restart. TAB moves fields.", 0xFF9CA3AF, 0);
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
    
    i32 box_w = 520;
    i32 box_h = 360;
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
    
    /* Draw glass login box */
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
    
    wl_draw_rect(&ds, bx, by, box_w, 4, 0xFF60A5FA);
    wl_draw_text(&ds, bx + 48, by + 36, "Welcome to YamOS", 0xFFFFFFFF, 0);
    wl_draw_text(&ds, bx + 48, by + 62, g_compositor.computer_name, 0xFF93C5FD, 0);
    wl_draw_text(&ds, bx + 48, by + 88, "Sign in to open your desktop session.", 0xFF9AA8BA, 0);
    
    /* Username Field */
    wl_draw_text(&ds, bx + 50, by + 126, "Username", g_compositor.login_focus_pass ? 0xFFE8EEF7 : 0xFF93C5FD, 0);
    u32 user_bg = g_compositor.login_focus_pass ? 0xFF111827 : 0xFF111C35;
    wl_draw_rect(&ds, bx + 50, by + 148, 420, 42, g_compositor.login_focus_pass ? 0xFF334155 : 0xFF2563EB);
    wl_draw_rect(&ds, bx + 51, by + 149, 418, 40, user_bg);
    wl_draw_text(&ds, bx + 64, by + 161, g_compositor.login_user, 0xFFFFFFFF, 0);
    if (!g_compositor.login_focus_pass) {
        wl_draw_rect(&ds, bx + 64 + strlen(g_compositor.login_user) * 8, by + 161, 8, 16, 0xFFFFFFFF);
    }
    
    /* Password Field */
    wl_draw_text(&ds, bx + 50, by + 214, "Password", g_compositor.login_focus_pass ? 0xFF93C5FD : 0xFFE8EEF7, 0);
    u32 pass_bg = g_compositor.login_focus_pass ? 0xFF111C35 : 0xFF111827;
    wl_draw_rect(&ds, bx + 50, by + 236, 420, 42, g_compositor.login_focus_pass ? 0xFF2563EB : 0xFF334155);
    wl_draw_rect(&ds, bx + 51, by + 237, 418, 40, pass_bg);
    
    char hidden_pass[32] = {0};
    int plen = strlen(g_compositor.login_pass);
    for (int i = 0; i < plen; i++) hidden_pass[i] = '*';
    wl_draw_text(&ds, bx + 64, by + 249, hidden_pass, 0xFFFFFFFF, 0);
    if (g_compositor.login_focus_pass) {
        wl_draw_rect(&ds, bx + 64 + plen * 8, by + 249, 8, 16, 0xFFFFFFFF);
    }

    wl_draw_rect(&ds, bx + 50, by + 298, 140, 36, 0xFF2563EB);
    wl_draw_text(&ds, bx + 88, by + 308, "Sign in", 0xFFFFFFFF, 0);
    wl_draw_text(&ds, bx + 210, by + 308, "Ctrl+Shift+Y bypass", 0xFF9AA8BA, 0);
    
    if (g_compositor.login_failed) {
        wl_draw_text(&ds, bx + 50, by + 282, "Authentication failed", 0xFFFF5555, 0);
    } else {
        wl_draw_text(&ds, bx + 50, by + 282, "ENTER submits. TAB switches field.", 0xFF64748B, 0);
    }
}

static bool is_leap_year(int year) {
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

static int days_in_month(int year, int month) {
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2 && is_leap_year(year)) return 29;
    if (month < 1 || month > 12) return 30;
    return days[month - 1];
}

static int day_of_week(int year, int month, int day) {
    static const int offsets[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (month < 3) year--;
    return (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + day) % 7;
}

static void rtc_read_local_bdt(rtc_time_t *out) {
    rtc_read(out);
    out->hour = (u8)(out->hour + 6);
    if (out->hour >= 24) {
        out->hour = (u8)(out->hour - 24);
        out->day++;
        if (out->day > days_in_month(out->year, out->month)) {
            out->day = 1;
            out->month++;
            if (out->month > 12) {
                out->month = 1;
                out->year++;
            }
        }
    }
}

static void draw_signal_bars(wl_surface_t *ds, i32 x, i32 y, bool strong, u32 color) {
    for (int i = 0; i < 4; i++) {
        i32 h = 4 + i * 4;
        u32 c = strong || i < 1 ? color : 0xFF475569;
        wl_draw_rect(ds, x + i * 6, y + (16 - h), 4, h, c);
    }
}

static void draw_status_chip(wl_surface_t *ds, i32 x, i32 y, i32 w, const char *label,
                             u32 bg, u32 fg, u32 accent) {
    wl_draw_rounded_rect(ds, x, y, w, 22, 6, bg);
    wl_draw_rect(ds, x, y, w, 1, accent);
    wl_draw_text(ds, x + 8, y + 5, label, fg, 0);
}

static void draw_text_fit(wl_surface_t *ds, i32 x, i32 y, const char *text,
                          i32 max_w, u32 color);

static void draw_text_fit(wl_surface_t *ds, i32 x, i32 y, const char *text,
                          i32 max_w, u32 color) {
    if (max_w < 8 || !text) return;
    int max_chars = max_w / 8;
    if (max_chars <= 0) return;

    char buf[96];
    int i = 0;
    while (text[i] && i < max_chars && i < 95) {
        buf[i] = text[i];
        i++;
    }
    if (text[i] && i > 1) {
        buf[i - 1] = '>';
        buf[i] = '\0';
    } else {
        buf[i] = '\0';
    }
    wl_draw_text(ds, x, y, buf, color, 0);
}

static void composite_calendar_popover(rtc_time_t t) {
    if (!g_compositor.calendar_open) return;

    u32 dw = g_compositor.scanout->width;
    u32 dh = g_compositor.scanout->height;
    wl_surface_t ds = { .buffer = g_compositor.scanout, .width = dw, .height = dh };
    static const char *months[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };

    i32 w = 312;
    i32 h = 292;
    i32 x = (i32)dw - w - 14;
    i32 y = 40;
    wl_draw_rounded_rect(&ds, x + 6, y + 8, w, h, 10, 0xAA05070A);
    wl_draw_rounded_rect(&ds, x, y, w, h, 10, 0xF0111827);
    wl_draw_rect(&ds, x, y, w, 2, 0xFF38BDF8);

    char header[64];
    ksnprintf(header, sizeof(header), "%s %u", months[t.month - 1], t.year);
    wl_draw_text(&ds, x + 18, y + 18, header, 0xFFFFFFFF, 0);

    char clock[32];
    int hour12 = t.hour % 12;
    if (hour12 == 0) hour12 = 12;
    ksnprintf(clock, sizeof(clock), "%d:%02d:%02d %s BDT", hour12, t.minute, t.second,
              t.hour >= 12 ? "PM" : "AM");
    wl_draw_text(&ds, x + 18, y + 42, clock, 0xFF9AA8BA, 0);

    const char *days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    for (int i = 0; i < 7; i++) {
        wl_draw_text(&ds, x + 20 + i * 40, y + 76, days[i], 0xFF93C5FD, 0);
    }

    int first = day_of_week(t.year, t.month, 1);
    int count = days_in_month(t.year, t.month);
    for (int d = 1; d <= count; d++) {
        int cell = first + d - 1;
        int cx = x + 18 + (cell % 7) * 40;
        int cy = y + 104 + (cell / 7) * 30;
        char num[4];
        ksnprintf(num, sizeof(num), "%d", d);
        if (d == t.day) {
            wl_draw_rounded_rect(&ds, cx - 5, cy - 5, 32, 24, 6, 0xFF2563EB);
            wl_draw_text(&ds, cx + (d < 10 ? 5 : 1), cy, num, 0xFFFFFFFF, 0);
        } else {
            wl_draw_text(&ds, cx + (d < 10 ? 5 : 1), cy, num, 0xFFE8EEF7, 0);
        }
    }
}

static void composite_menubar(void) {
    u32 *dst = g_compositor.scanout->pixels;
    u32 dw = g_compositor.scanout->width;
    u32 dh = g_compositor.scanout->height;
    (void)dh;
    
    wl_surface_t ds = { .buffer = g_compositor.scanout, .width = dw, .height = dh };
    u32 bar_h = 34;
    
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
    
    /* Active app title, bounded so it never collides with the menu strip. */
    const char *active_title = "YamOS Desktop";
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (g_compositor.surfaces[i].focused) {
            active_title = g_compositor.surfaces[i].title;
            break;
        }
    }
    draw_text_fit(&ds, 44, 8, active_title, 126, 0xFFFFFFFF);

    u32 file_bg = (g_compositor.desktop_menu_open == 1) ? 0xFF44475A : 0x00000000;
    u32 view_bg = (g_compositor.desktop_menu_open == 2) ? 0xFF44475A : 0x00000000;
    u32 win_bg  = (g_compositor.desktop_menu_open == 3) ? 0xFF44475A : 0x00000000;
    if (file_bg) wl_draw_rect(&ds, 182, 5, 48, 20, file_bg);
    if (view_bg) wl_draw_rect(&ds, 234, 5, 50, 20, view_bg);
    if (win_bg)  wl_draw_rect(&ds, 288, 5, 72, 20, win_bg);
    wl_draw_text(&ds, 192, 8, "File", 0xFFE8EEF7, 0);
    wl_draw_text(&ds, 244, 8, "View", 0xFFE8EEF7, 0);
    wl_draw_text(&ds, 298, 8, "Window", 0xFFE8EEF7, 0);
    
    rtc_time_t t;
    rtc_read_local_bdt(&t);

    const audio_status_t *audio = audio_get_status();
    const iwlwifi_status_t *wifi = iwlwifi_get_status();
    const hci_status_t *bt = hci_get_status();
    bool net_ready = g_net_iface.is_up && g_net_iface.dhcp_done;

    i32 tray_right = (i32)dw - 12;
    i32 left_limit = 372;
    i32 gap = 8;
    i32 sound_w = 70;
    i32 net_w = 94;
    i32 wifi_w = 78;
    i32 bt_w = 58;
    i32 clock_w = 150;
    bool show_wifi = true;
    bool show_bt = true;
    bool compact_labels = false;

    i32 tray_w = sound_w + net_w + wifi_w + bt_w + clock_w + gap * 4;
    if (tray_w > tray_right - left_limit) {
        compact_labels = true;
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

    if (g_compositor.current_user[0]) {
        char session[80];
        ksnprintf(session, sizeof(session), "%s@%s", g_compositor.current_user, g_compositor.computer_name);
        draw_text_fit(&ds, left_limit, 8, session, tray_x - left_limit - 12, 0xFF9AA8BA);
    }

    i32 sound_x = tray_x;
    i32 net_x = sound_x + sound_w + gap;
    i32 wifi_x = net_x + net_w + gap;
    i32 bt_x = wifi_x + wifi_w + gap;
    i32 clock_x = show_wifi ? (show_bt ? (bt_x + bt_w + gap) : (wifi_x + wifi_w + gap))
                             : (net_x + net_w + gap);

    char sound_str[16];
    if (audio->output_available) {
        ksnprintf(sound_str, sizeof(sound_str), compact_labels ? "%u" : "VOL %u",
                  audio->muted ? 0 : audio->volume_percent);
    } else {
        ksnprintf(sound_str, sizeof(sound_str), compact_labels ? "--" : "VOL --");
    }
    draw_status_chip(&ds, sound_x, 6, sound_w, sound_str,
                     audio->output_available ? 0xFF1E293B : 0xFF273244,
                     audio->output_available ? 0xFFE8EEF7 : 0xFF94A3B8,
                     audio->output_available ? 0xFF34D399 : 0xFF64748B);

    draw_status_chip(&ds, net_x, 6, net_w,
                     net_ready ? (compact_labels ? "ETH" : "ETH ON") : (compact_labels ? "--" : "NET --"),
                     net_ready ? 0xFF123024 : 0xFF2B2531,
                     net_ready ? 0xFFBBF7D0 : 0xFFFFD166,
                     net_ready ? 0xFF34D399 : 0xFFFFD166);
    if (net_w >= 82) draw_signal_bars(&ds, net_x + net_w - 32, 9, net_ready, net_ready ? 0xFF34D399 : 0xFFFFD166);

    if (show_wifi) {
        bool wifi_ready = wifi->radio_enabled && wifi->present && wifi->firmware_loaded;
        draw_status_chip(&ds, wifi_x, 6, wifi_w,
                         wifi_ready ? (compact_labels ? "Wi" : "WiFi ON") :
                                      (wifi->radio_enabled ? (compact_labels ? "Wi!" : "WiFi !") :
                                                             (compact_labels ? "Wi" : "WiFi --")),
                         wifi_ready ? 0xFF123024 : 0xFF273244,
                         wifi_ready ? 0xFFBBF7D0 : (wifi->radio_enabled ? 0xFFFFD166 : 0xFF94A3B8),
                         wifi_ready ? 0xFF34D399 : (wifi->radio_enabled ? 0xFFFFD166 : 0xFF64748B));
        if (wifi_w >= 70) draw_signal_bars(&ds, wifi_x + wifi_w - 30, 9, wifi_ready,
                                           wifi_ready ? 0xFF34D399 : 0xFF64748B);
    }

    if (show_bt) {
        bool bt_ready = bt->radio_enabled && bt->controller_present && bt->usb_backend_ready;
        draw_status_chip(&ds, bt_x, 6, bt_w,
                         bt_ready ? (compact_labels ? "BT" : "BT ON") :
                                    (bt->radio_enabled ? (compact_labels ? "B!" : "BT !") :
                                                         (compact_labels ? "BT" : "BT --")),
                         bt_ready ? 0xFF123024 : 0xFF273244,
                         bt_ready ? 0xFFBBF7D0 : (bt->radio_enabled ? 0xFFFFD166 : 0xFF94A3B8),
                         bt_ready ? 0xFF34D399 : (bt->radio_enabled ? 0xFFFFD166 : 0xFF64748B));
    }

    char time_str[16];
    int hour12 = t.hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char *ampm = (t.hour >= 12) ? "PM" : "AM";
    if (compact_labels) {
        ksnprintf(time_str, sizeof(time_str), "%d:%02d %s", hour12, t.minute, ampm);
    } else {
        ksnprintf(time_str, sizeof(time_str), "%d:%02d:%02d %s", hour12, t.minute, t.second, ampm);
    }
    draw_text_fit(&ds, clock_x, 4, time_str, clock_w, 0xFFE8EEF7);
    char date_str[16];
    ksnprintf(date_str, sizeof(date_str), "%02u/%02u/%04u", t.day, t.month, t.year);
    if (clock_w >= 130) draw_text_fit(&ds, clock_x + 20, 18, date_str, clock_w - 20, 0xFF9AA8BA);

    if (g_compositor.desktop_menu_open) {
        i32 mx = 182;
        i32 mw = 204;
        i32 mh = g_compositor.desktop_menu_open == 1 ? 308 : 188;
        if (g_compositor.desktop_menu_open == 2) mx = 234;
        if (g_compositor.desktop_menu_open == 3) mx = 288;
        wl_draw_rect(&ds, mx, 34, mw, mh, 0xF0141C2B);
        wl_draw_rect(&ds, mx, 34, mw, 1, 0xFF60A5FA);
        wl_draw_rect(&ds, mx, 34 + mh - 1, mw, 1, 0x553B4658);
        if (g_compositor.desktop_menu_open == 1) {
            wl_draw_text(&ds, mx + 14, 50, "New Terminal", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 80, "New Browser", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 110, "New Calculator", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 140, "File Manager", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 170, "Ethernet Settings", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 200, "Wi-Fi Settings", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 230, "Bluetooth Settings", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 260, "Sound Settings", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 290, "Driver Probe", 0xFFE8EEF7, 0);
        } else if (g_compositor.desktop_menu_open == 2) {
            wl_draw_text(&ds, mx + 14, 50, "Toggle Debug", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 80, "Refresh Screen", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 110, "Display Settings", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 140, "Hide Menu", 0xFFE8EEF7, 0);
        } else {
            wl_draw_text(&ds, mx + 14, 50, "Close Focused", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 80, "Minimize Focused", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 110, "Maximize / Restore", 0xFFE8EEF7, 0);
            wl_draw_text(&ds, mx + 14, 140, "Restore All", 0xFFE8EEF7, 0);
        }
    }

    composite_calendar_popover(t);
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
        i32 mw = 520;
        i32 mh = 440;
        i32 mx = dock_x;
        if (mx + mw > (i32)dw - 12) mx = (i32)dw - mw - 12;
        if (mx < 12) mx = 12;
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
        wl_draw_text(&ds, mx + 274, my + 42, "Settings", 0xFF9AA8BA, 0);

        wl_draw_rect(&ds, mx + 18, my + 68, 230, 42, 0xFF1F2937);
        wl_draw_text(&ds, mx + 34, my + 81, "Terminal", 0xFFBD93F9, 0);
        wl_draw_text(&ds, mx + 176, my + 81, "Shell", 0xFF9AA8BA, 0);

        wl_draw_rect(&ds, mx + 18, my + 116, 230, 42, 0xFF1F2937);
        wl_draw_text(&ds, mx + 34, my + 129, "Browser", 0xFF8BE9FD, 0);
        wl_draw_text(&ds, mx + 176, my + 129, "Web", 0xFF9AA8BA, 0);

        wl_draw_rect(&ds, mx + 18, my + 164, 230, 42, 0xFF1F2937);
        wl_draw_text(&ds, mx + 34, my + 177, "Calculator", 0xFF50FA7B, 0);
        wl_draw_text(&ds, mx + 176, my + 177, "Tools", 0xFF9AA8BA, 0);

        wl_draw_rect(&ds, mx + 18, my + 212, 230, 42, 0xFF1F2937);
        wl_draw_text(&ds, mx + 34, my + 225, "File Manager", 0xFFFFD166, 0);
        wl_draw_text(&ds, mx + 176, my + 225, "Files", 0xFF9AA8BA, 0);

        wl_draw_rect(&ds, mx + 18, my + 260, 230, 42, 0xFF1F2937);
        wl_draw_text(&ds, mx + 34, my + 273, "Driver Probe", 0xFFFFD166, 0);
        wl_draw_text(&ds, mx + 176, my + 273, "Kernel", 0xFF9AA8BA, 0);

        wl_draw_rect(&ds, mx + 272, my + 68, 230, 42, 0xFF1F2937);
        wl_draw_text(&ds, mx + 288, my + 81, "Ethernet", 0xFF86EFAC, 0);
        wl_draw_text(&ds, mx + 430, my + 81, "Wired", 0xFF9AA8BA, 0);

        wl_draw_rect(&ds, mx + 272, my + 116, 230, 42, 0xFF1F2937);
        wl_draw_text(&ds, mx + 288, my + 129, "Wi-Fi", 0xFF93C5FD, 0);
        wl_draw_text(&ds, mx + 430, my + 129, "Radio", 0xFF9AA8BA, 0);

        wl_draw_rect(&ds, mx + 272, my + 164, 230, 42, 0xFF1F2937);
        wl_draw_text(&ds, mx + 288, my + 177, "Bluetooth", 0xFFC4B5FD, 0);
        wl_draw_text(&ds, mx + 430, my + 177, "Pair", 0xFF9AA8BA, 0);

        wl_draw_rect(&ds, mx + 272, my + 212, 230, 42, 0xFF1F2937);
        wl_draw_text(&ds, mx + 288, my + 225, "Sound", 0xFF5EEAD4, 0);
        wl_draw_text(&ds, mx + 430, my + 225, "Mixer", 0xFF9AA8BA, 0);

        wl_draw_rect(&ds, mx + 272, my + 260, 230, 42, 0xFF1F2937);
        wl_draw_text(&ds, mx + 288, my + 273, "Display", 0xFFFDE68A, 0);
        wl_draw_text(&ds, mx + 430, my + 273, "Screen", 0xFF9AA8BA, 0);

        wl_draw_rect(&ds, mx + 18, my + 326, mw - 36, 34, 0xFF293447);
        wl_draw_text(&ds, mx + 34, my + 335, "Lock / Switch User", 0xFFE8EEF7, 0);

        wl_draw_rect(&ds, mx + 18, my + 380, 230, 32, 0xFF374151);
        wl_draw_text(&ds, mx + 104, my + 390, "Restart", 0xFFE8EEF7, 0);
        wl_draw_rect(&ds, mx + 272, my + 380, 230, 32, 0xFF7F1D1D);
        wl_draw_text(&ds, mx + 354, my + 390, "Shutdown", 0xFFFFCACA, 0);
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
            else if (strstr(s->title, "File")) { glyph = "[]"; accent = 0xFFFFD166; }

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
