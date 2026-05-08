#include <nexus/types.h>
#include "sched/sched.h"
#include "sched/wait.h"
#include "compositor.h"
#include "wl_draw.h"
#include "drivers/timer/rtc.h"
#include "drivers/timer/pit.h"
#include "fs/vfs.h"
#include "lib/string.h"
#include "lib/kprintf.h"

#define SYS_W 700
#define SYS_H 470
#define SYS_MIN_W 700
#define SYS_MIN_H 470

#define COL_BG     0xFFEAF0F8
#define COL_PANEL  0xFFFFFFFF
#define COL_LINE   0xFFD2DCEA
#define COL_TEXT   0xFF102033
#define COL_MUTED  0xFF5B6B84
#define COL_BLUE   0xFF1D4ED8
#define COL_GREEN  0xFF16A34A
#define COL_WARN   0xFFD97706
#define COL_ERR    0xFFB91C1C

static i32 mouse_x = -1;
static i32 mouse_y = -1;
static bool shift_held = false;
static int edit_focus = 0; /* 0 none, 1 username, 2 password, 3 datetime */
static char edit_username[32];
static char edit_password[32];
static char edit_datetime[24]; /* YYYY-MM-DD HH:MM */
static char info_msg[128];
static u32 info_color = COL_MUTED;
static i32 sys_w = SYS_W;
static i32 sys_h = SYS_H;
static i32 desktop_x = 360;
static i32 clock_x = 360;
static i32 info_y = 446;

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

static bool is_leap_year(int year) {
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

static int days_in_month(int year, int month) {
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2 && is_leap_year(year)) return 29;
    if (month < 1 || month > 12) return 30;
    return days[month - 1];
}

static void rtc_read_local_bdt_adjusted(rtc_time_t *out, int extra_minutes) {
    rtc_read(out);
    int total_minutes = (int)out->hour * 60 + (int)out->minute + 360 + extra_minutes;
    while (total_minutes < 0) {
        total_minutes += 24 * 60;
        out->day--;
        if (out->day < 1) {
            out->month--;
            if (out->month < 1) {
                out->month = 12;
                out->year--;
            }
            out->day = (u8)days_in_month(out->year, out->month);
        }
    }
    while (total_minutes >= 24 * 60) {
        total_minutes -= 24 * 60;
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
    out->hour = (u8)(total_minutes / 60);
    out->minute = (u8)(total_minutes % 60);
}

static int parse_fixed_int(const char *s, int count, int *out) {
    int v = 0;
    for (int i = 0; i < count; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return 1;
}

static int parse_datetime_local(const char *text, rtc_time_t *t) {
    if (!text || !t) return 0;
    if (strlen(text) != 16) return 0;
    if (text[4] != '-' || text[7] != '-' || text[10] != ' ' || text[13] != ':') return 0;
    int year, month, day, hour, minute;
    if (!parse_fixed_int(text + 0, 4, &year)) return 0;
    if (!parse_fixed_int(text + 5, 2, &month)) return 0;
    if (!parse_fixed_int(text + 8, 2, &day)) return 0;
    if (!parse_fixed_int(text + 11, 2, &hour)) return 0;
    if (!parse_fixed_int(text + 14, 2, &minute)) return 0;
    if (year < 2000 || year > 2099) return 0;
    if (month < 1 || month > 12) return 0;
    if (day < 1 || day > days_in_month(year, month)) return 0;
    if (hour < 0 || hour > 23) return 0;
    if (minute < 0 || minute > 59) return 0;
    t->year = (u16)year;
    t->month = (u8)month;
    t->day = (u8)day;
    t->hour = (u8)hour;
    t->minute = (u8)minute;
    t->second = 0;
    return 1;
}

static void local_to_utc(rtc_time_t *t, int offset_minutes) {
    int total_minutes = (int)t->hour * 60 + (int)t->minute - 360 - offset_minutes;
    while (total_minutes < 0) {
        total_minutes += 24 * 60;
        t->day--;
        if (t->day < 1) {
            t->month--;
            if (t->month < 1) {
                t->month = 12;
                t->year--;
            }
            t->day = (u8)days_in_month(t->year, t->month);
        }
    }
    while (total_minutes >= 24 * 60) {
        total_minutes -= 24 * 60;
        t->day++;
        if (t->day > days_in_month(t->year, t->month)) {
            t->day = 1;
            t->month++;
            if (t->month > 12) {
                t->month = 1;
                t->year++;
            }
        }
    }
    t->hour = (u8)(total_minutes / 60);
    t->minute = (u8)(total_minutes % 60);
}

static bool hit(i32 x, i32 y, i32 rx, i32 ry, i32 rw, i32 rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void update_layout(wl_surface_t *s) {
    if (!s) return;
    sys_w = (i32)s->width;
    sys_h = (i32)s->height;
    if (sys_w < SYS_MIN_W) sys_w = SYS_MIN_W;
    if (sys_h < SYS_MIN_H) sys_h = SYS_MIN_H;
    desktop_x = sys_w - 340;
    clock_x = desktop_x;
    if (desktop_x < 360) desktop_x = 360;
    if (clock_x < 360) clock_x = 360;
    info_y = sys_h - 24;
}

static void draw_button(wl_surface_t *s, i32 x, i32 y, i32 w, const char *label, bool primary) {
    wl_draw_rounded_rect(s, x, y, w, 32, 8, primary ? COL_BLUE : COL_PANEL);
    wl_draw_rounded_outline(s, x, y, w, 32, 8, primary ? COL_BLUE : COL_LINE);
    i32 tx = x + (w - (i32)strlen(label) * 8) / 2;
    if (tx < x + 10) tx = x + 10;
    wl_draw_text(s, tx, y + 9, label, primary ? 0xFFFFFFFF : COL_TEXT, 0);
}

static void draw_field(wl_surface_t *s, i32 x, i32 y, const char *label, const char *value, bool password, bool focused) {
    wl_draw_text(s, x, y - 16, label, COL_MUTED, 0);
    wl_draw_rounded_rect(s, x, y, 250, 34, 8, COL_PANEL);
    wl_draw_rounded_outline(s, x, y, 250, 34, 8, focused ? COL_BLUE : COL_LINE);
    char masked[40];
    const char *shown = value;
    if (password) {
        int n = strlen(value);
        if (n > 31) n = 31;
        for (int i = 0; i < n; i++) masked[i] = '*';
        masked[n] = 0;
        shown = masked;
    }
    wl_draw_text(s, x + 10, y + 10, shown, COL_TEXT, 0);
}

static int find_current_user_index(void) {
    wl_compositor_t *comp = wl_get_compositor();
    for (u32 i = 0; i < comp->user_count; i++) {
        if (comp->users[i].active && strcmp(comp->users[i].username, comp->current_user) == 0) return (int)i;
    }
    return -1;
}

static bool username_in_use(const char *name, int skip_idx) {
    wl_compositor_t *comp = wl_get_compositor();
    for (u32 i = 0; i < comp->user_count; i++) {
        if ((int)i == skip_idx) continue;
        if (comp->users[i].active && strcmp(comp->users[i].username, name) == 0) return true;
    }
    return false;
}

static bool valid_username(const char *name) {
    int n = strlen(name);
    if (n < 2 || n > 30) return false;
    for (int i = 0; i < n; i++) {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

static void apply_username_change(void) {
    wl_compositor_t *comp = wl_get_compositor();
    int idx = find_current_user_index();
    if (idx < 0) {
        ksnprintf(info_msg, sizeof(info_msg), "No current user account found.");
        info_color = COL_ERR;
        return;
    }
    if (!valid_username(edit_username)) {
        ksnprintf(info_msg, sizeof(info_msg), "Username must be 2-30 chars: A-Z a-z 0-9 _ -");
        info_color = COL_WARN;
        return;
    }
    if (username_in_use(edit_username, idx)) {
        ksnprintf(info_msg, sizeof(info_msg), "Username already exists.");
        info_color = COL_WARN;
        return;
    }
    if (strcmp(comp->users[idx].username, edit_username) == 0) {
        ksnprintf(info_msg, sizeof(info_msg), "Username is unchanged.");
        info_color = COL_MUTED;
        return;
    }

    char old_home[64];
    char new_home[64];
    ksnprintf(old_home, sizeof(old_home), "/home/%s", comp->users[idx].username);
    ksnprintf(new_home, sizeof(new_home), "/home/%s", edit_username);
    sys_rename(old_home, new_home);
    sys_mkdir(new_home, 0755);

    strcpy(comp->users[idx].username, edit_username);
    strcpy(comp->users[idx].display_name, edit_username);
    strcpy(comp->current_user, edit_username);
    if (strcmp(comp->login_user, old_home + 6) == 0) strcpy(comp->login_user, edit_username);

    if (wl_compositor_save_profile()) {
        ksnprintf(info_msg, sizeof(info_msg), "Username changed to '%s'.", edit_username);
        info_color = COL_GREEN;
    } else {
        ksnprintf(info_msg, sizeof(info_msg), "Saved failed after username change.");
        info_color = COL_ERR;
    }
}

static void apply_password_change(void) {
    wl_compositor_t *comp = wl_get_compositor();
    int idx = find_current_user_index();
    if (idx < 0) {
        ksnprintf(info_msg, sizeof(info_msg), "No current user account found.");
        info_color = COL_ERR;
        return;
    }
    if (strlen(edit_password) < 4) {
        ksnprintf(info_msg, sizeof(info_msg), "Password must be at least 4 characters.");
        info_color = COL_WARN;
        return;
    }
    wl_password_hash(edit_password, comp->users[idx].password);
    if (wl_compositor_save_profile()) {
        ksnprintf(info_msg, sizeof(info_msg), "Password updated and saved.");
        info_color = COL_GREEN;
        edit_password[0] = 0;
    } else {
        ksnprintf(info_msg, sizeof(info_msg), "Failed to save password.");
        info_color = COL_ERR;
    }
}

static void cycle_wallpaper(void) {
    wl_compositor_t *comp = wl_get_compositor();
    comp->wallpaper_mode = (u8)((comp->wallpaper_mode + 1) % 4);
    wl_compositor_save_profile();
    ksnprintf(info_msg, sizeof(info_msg), "Background theme changed (mode %u).", comp->wallpaper_mode);
    info_color = COL_GREEN;
}

static void change_time_offset(int delta_minutes) {
    wl_compositor_t *comp = wl_get_compositor();
    int next = comp->time_offset_minutes + delta_minutes;
    if (next < -720) next = -720;
    if (next > 720) next = 720;
    comp->time_offset_minutes = next;
    wl_compositor_save_profile();
    ksnprintf(info_msg, sizeof(info_msg), "Clock offset updated to %+d min.", comp->time_offset_minutes);
    info_color = COL_GREEN;
}

static void apply_datetime_change(void) {
    wl_compositor_t *comp = wl_get_compositor();
    rtc_time_t local_t;
    if (!parse_datetime_local(edit_datetime, &local_t)) {
        ksnprintf(info_msg, sizeof(info_msg), "Use format YYYY-MM-DD HH:MM");
        info_color = COL_WARN;
        return;
    }
    local_to_utc(&local_t, comp->time_offset_minutes);
    rtc_set(&local_t);
    ksnprintf(info_msg, sizeof(info_msg), "RTC date/time updated.");
    info_color = COL_GREEN;
}

static void draw_system_settings(wl_surface_t *s) {
    update_layout(s);
    wl_compositor_t *comp = wl_get_compositor();
    wl_draw_rect(s, 0, 0, sys_w, sys_h, COL_BG);
    wl_draw_vgradient(s, 0, 0, sys_w, 74, 0xFF0F172A, 0xFF1E293B);
    wl_draw_rect(s, 0, 73, sys_w, 1, 0xFF334155);
    wl_draw_text(s, 22, 22, "System Settings", 0xFFF8FAFC, 0);
    wl_draw_text(s, 178, 22, "Account, desktop, and clock essentials", 0xFFB9C4D3, 0);
    wl_draw_text(s, 22, 48, "YamOS Control Center", 0xFF93A4BA, 0);

    wl_draw_rounded_rect(s, 20, 92, 320, 230, 12, COL_PANEL);
    wl_draw_rounded_outline(s, 20, 92, 320, 230, 12, COL_LINE);
    wl_draw_rect(s, 20, 128, 320, 1, 0xFFE2E8F0);
    wl_draw_text(s, 32, 106, "Account", COL_TEXT, 0);
    draw_field(s, 32, 144, "Username", edit_username, false, edit_focus == 1);
    draw_button(s, 32, 186, 146, "Save Username", true);
    draw_field(s, 32, 236, "Password", edit_password, true, edit_focus == 2);
    draw_button(s, 32, 278, 146, "Save Password", true);

    wl_draw_rounded_rect(s, desktop_x, 92, 320, 152, 12, COL_PANEL);
    wl_draw_rounded_outline(s, desktop_x, 92, 320, 152, 12, COL_LINE);
    wl_draw_rect(s, desktop_x, 128, 320, 1, 0xFFE2E8F0);
    wl_draw_text(s, desktop_x + 12, 106, "Desktop", COL_TEXT, 0);
    const char *mode = "Boot wallpaper image";
    if (comp->wallpaper_mode == 1) mode = "Indigo/Pink gradient";
    else if (comp->wallpaper_mode == 2) mode = "Midnight blue gradient";
    else if (comp->wallpaper_mode == 3) mode = "Emerald gradient";
    wl_draw_text(s, desktop_x + 12, 150, mode, COL_MUTED, 0);
    draw_button(s, desktop_x + 12, 184, 188, "Change Background", false);

    wl_draw_rounded_rect(s, clock_x, 260, 320, 190, 12, COL_PANEL);
    wl_draw_rounded_outline(s, clock_x, 260, 320, 190, 12, COL_LINE);
    wl_draw_rect(s, clock_x, 296, 320, 1, 0xFFE2E8F0);
    wl_draw_text(s, clock_x + 12, 274, "Clock", COL_TEXT, 0);
    rtc_time_t now;
    rtc_read_local_bdt_adjusted(&now, comp->time_offset_minutes);
    char clock_line[64];
    ksnprintf(clock_line, sizeof(clock_line), "%02u:%02u:%02u  %02u/%02u/%04u",
              now.hour, now.minute, now.second, now.day, now.month, now.year);
    wl_draw_text(s, clock_x + 12, 308, clock_line, COL_TEXT, 0);
    char offset_line[48];
    ksnprintf(offset_line, sizeof(offset_line), "Offset from BDT: %+d min", comp->time_offset_minutes);
    wl_draw_text(s, clock_x + 12, 332, offset_line, COL_MUTED, 0);
    draw_button(s, clock_x + 12, 356, 72, "-30m", false);
    draw_button(s, clock_x + 92, 356, 72, "+30m", false);
    draw_button(s, clock_x + 172, 356, 96, "Reset", false);
    draw_field(s, clock_x + 12, 392, "Set Date/Time", edit_datetime, false, edit_focus == 3);
    draw_button(s, clock_x + 260, 392, 52, "Set", true);

    if (!info_msg[0]) {
        ksnprintf(info_msg, sizeof(info_msg), "Tip: click fields, press Tab to switch, Enter to apply.");
        info_color = COL_MUTED;
    }
    wl_draw_text(s, 20, info_y, info_msg, info_color, 0);
}

static void handle_click(i32 x, i32 y) {
    if (hit(x, y, 32, 124, 250, 34)) {
        edit_focus = 1;
        return;
    }
    if (hit(x, y, 32, 214, 250, 34)) {
        edit_focus = 2;
        return;
    }
    if (hit(x, y, clock_x + 12, 392, 250, 34)) {
        edit_focus = 3;
        return;
    }
    edit_focus = 0;
    if (hit(x, y, 32, 166, 120, 32)) apply_username_change();
    else if (hit(x, y, 32, 256, 120, 32)) apply_password_change();
    else if (hit(x, y, desktop_x + 12, 184, 188, 32)) cycle_wallpaper();
    else if (hit(x, y, clock_x + 12, 356, 72, 32)) change_time_offset(-30);
    else if (hit(x, y, clock_x + 92, 356, 72, 32)) change_time_offset(30);
    else if (hit(x, y, clock_x + 172, 356, 96, 32)) change_time_offset(-wl_get_compositor()->time_offset_minutes);
    else if (hit(x, y, clock_x + 260, 392, 52, 32)) apply_datetime_change();
}

static void field_backspace(char *buf) {
    int len = strlen(buf);
    if (len > 0) buf[len - 1] = 0;
}

static void field_append(char *buf, char c) {
    int len = strlen(buf);
    if (len < 31) {
        buf[len] = c;
        buf[len + 1] = 0;
    }
}

static void handle_key(u16 sc) {
    if (sc == 0x2A || sc == 0x36) return;
    if (edit_focus == 0) return;
    if (sc >= 128) return;
    char c = shift_held ? sc_ascii_shift[sc] : sc_ascii[sc];
    char *field = (edit_focus == 1) ? edit_username : (edit_focus == 2 ? edit_password : edit_datetime);
    if (c == '\b') field_backspace(field);
    else if (c == '\t') {
        if (edit_focus == 1) edit_focus = 2;
        else if (edit_focus == 2) edit_focus = 3;
        else edit_focus = 1;
    }
    else if (c == '\n') {
        if (edit_focus == 1) apply_username_change();
        else if (edit_focus == 2) apply_password_change();
        else apply_datetime_change();
    } else if (c >= 32 && c <= 126) {
        field_append(field, c);
    }
}

void wl_system_settings_task(void *arg) {
    (void)arg;
    task_sleep_ms(120);
    wl_compositor_t *comp = wl_get_compositor();
    wl_surface_t *s = wl_surface_create("System Settings", 280, 84, SYS_W, SYS_H, sched_current()->id);
    if (!s) return;
    wl_surface_set_constraints(s, true, SYS_MIN_W, SYS_MIN_H);

    strcpy(edit_username, comp->current_user[0] ? comp->current_user : comp->login_user);
    edit_password[0] = 0;
    rtc_time_t now;
    rtc_read_local_bdt_adjusted(&now, comp->time_offset_minutes);
    ksnprintf(edit_datetime, sizeof(edit_datetime), "%04u-%02u-%02u %02u:%02u",
              now.year, now.month, now.day, now.hour, now.minute);
    info_msg[0] = 0;
    info_color = COL_MUTED;
    edit_focus = 0;

    draw_system_settings(s);
    wl_surface_commit(s);

    u32 my_id = s->id;
    u64 last_clock_refresh_ms = pit_uptime_ms();
    while (s->state == WL_SURFACE_ACTIVE && s->id == my_id) {
        input_event_t ev;
        bool dirty = false;
        while (wl_surface_pop_event(s, &ev)) {
            if (ev.type == EV_RESIZE) dirty = true;
            else if (ev.type == EV_ABS && ev.code == 0) mouse_x = ev.value;
            else if (ev.type == EV_ABS && ev.code == 1) mouse_y = ev.value;
            else if (ev.type == EV_KEY) {
                if (ev.code == 0x2A || ev.code == 0x36) shift_held = (ev.value == KEY_PRESSED);
                if (ev.value == KEY_PRESSED && ev.code >= 0x110 && mouse_x >= 0 && mouse_y >= 0) {
                    handle_click(mouse_x, mouse_y);
                    dirty = true;
                } else if (ev.value == KEY_PRESSED && ev.code < 0x110) {
                    handle_key(ev.code);
                    dirty = true;
                }
            }
        }
        if (dirty) {
            draw_system_settings(s);
            wl_surface_commit(s);
        }
        u64 now_ms = pit_uptime_ms();
        if ((now_ms - last_clock_refresh_ms) >= 1000) {
            draw_system_settings(s);
            wl_surface_commit(s);
            last_clock_refresh_ms = now_ms;
        }
        task_sleep_ms(150);
    }
}
