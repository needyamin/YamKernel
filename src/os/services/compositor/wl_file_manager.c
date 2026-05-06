/* YamKernel - compositor-native File Manager */
#include <nexus/types.h>
#include "../sched/sched.h"
#include "../sched/wait.h"
#include "fs/vfs.h"
#include "compositor.h"
#include "wl_draw.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

#define FM_W 900
#define FM_H 560
#define FM_MAX_ITEMS 128
#define FM_VISIBLE_ROWS 18
#define FM_TEXT_MAX 4096
#define FM_HISTORY_MAX 24

#define COL_APP_BG    0xFFF6F8FB
#define COL_SIDEBAR   0xFFEAF0F8
#define COL_PANEL     0xFFFFFFFF
#define COL_LINE      0xFFD8E0EA
#define COL_TEXT      0xFF172033
#define COL_MUTED     0xFF657186
#define COL_PRIMARY   0xFF2563EB
#define COL_PRIMARY_D 0xFF1D4ED8
#define COL_DANGER    0xFFB42318
#define COL_OK        0xFF047857

typedef enum {
    FM_MODE_BROWSE = 0,
    FM_MODE_NEW_FILE,
    FM_MODE_NEW_FOLDER,
    FM_MODE_EDIT_TEXT,
    FM_MODE_ADDRESS,
    FM_MODE_SEARCH
} fm_mode_t;

typedef enum {
    FM_SORT_NAME = 0,
    FM_SORT_TYPE,
    FM_SORT_SIZE
} fm_sort_t;

typedef struct {
    char name[192];
    u64 size;
    u32 is_dir;
} fm_item_t;

static char fm_path[256] = "/home/root";
static char fm_home_dir[256] = "/home/root"; /* resolved per logged-in user */
static char fm_history[FM_HISTORY_MAX][256];
static i32 fm_history_count = 0;
static i32 fm_history_pos = -1;

static fm_item_t fm_items[FM_MAX_ITEMS];
static fm_item_t fm_view[FM_MAX_ITEMS];
static u32 fm_count = 0;
static u32 fm_view_count = 0;
static i32 fm_selected = -1;
static u32 fm_scroll = 0;
static fm_sort_t fm_sort = FM_SORT_NAME;
static bool fm_sort_desc = false;

static char fm_status[192] = "Ready";
static char fm_name_buf[96];
static u32 fm_name_len = 0;
static char fm_search[96];
static u32 fm_search_len = 0;
static char fm_address[256];
static u32 fm_address_len = 0;
static char fm_text[FM_TEXT_MAX];
static u32 fm_text_len = 0;
static char fm_edit_file[256];
static fm_mode_t fm_mode = FM_MODE_BROWSE;
static bool fm_shift = false;
static i32 fm_mouse_x = -1;
static i32 fm_mouse_y = -1;

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

static void fm_copy(char *dst, usize cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) src = "";
    usize i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void fm_set_status(const char *msg) {
    fm_copy(fm_status, sizeof(fm_status), msg);
}

static bool fm_contains(const char *s, const char *needle) {
    if (!needle || !*needle) return true;
    if (!s) return false;
    usize nlen = strlen(needle);
    for (usize i = 0; s[i]; i++) {
        usize j = 0;
        while (j < nlen && s[i + j] && s[i + j] == needle[j]) j++;
        if (j == nlen) return true;
    }
    return false;
}

static int fm_cmp_name(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static const char *fm_type_text(const fm_item_t *it) {
    if (!it) return "";
    return it->is_dir ? "Folder" : "Text file";
}

static int fm_compare(const fm_item_t *a, const fm_item_t *b) {
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    int rc = 0;
    if (fm_sort == FM_SORT_SIZE) {
        if (a->size < b->size) rc = -1;
        else if (a->size > b->size) rc = 1;
        else rc = fm_cmp_name(a->name, b->name);
    } else if (fm_sort == FM_SORT_TYPE) {
        rc = fm_cmp_name(fm_type_text(a), fm_type_text(b));
        if (rc == 0) rc = fm_cmp_name(a->name, b->name);
    } else {
        rc = fm_cmp_name(a->name, b->name);
    }
    return fm_sort_desc ? -rc : rc;
}

static void fm_sort_view(void) {
    for (u32 i = 0; i < fm_view_count; i++) {
        u32 best = i;
        for (u32 j = i + 1; j < fm_view_count; j++) {
            if (fm_compare(&fm_view[j], &fm_view[best]) < 0) best = j;
        }
        if (best != i) {
            fm_item_t tmp = fm_view[i];
            fm_view[i] = fm_view[best];
            fm_view[best] = tmp;
        }
    }
}

static void fm_join(char *out, usize cap, const char *dir, const char *name) {
    if (!out || cap == 0) return;
    fm_copy(out, cap, dir);
    if (strcmp(out, "/") != 0) {
        usize len = strlen(out);
        if (len + 1 < cap && out[len - 1] != '/') {
            out[len++] = '/';
            out[len] = 0;
        }
    }
    usize pos = strlen(out);
    for (usize i = 0; name && name[i] && pos + 1 < cap; i++) out[pos++] = name[i];
    out[pos] = 0;
}

static void fm_push_history(const char *path) {
    if (!path || !*path) return;
    if (fm_history_pos >= 0 && strcmp(fm_history[fm_history_pos], path) == 0) return;
    if (fm_history_pos + 1 < fm_history_count) fm_history_count = fm_history_pos + 1;
    if (fm_history_count >= FM_HISTORY_MAX) {
        for (i32 i = 1; i < FM_HISTORY_MAX; i++) fm_copy(fm_history[i - 1], sizeof(fm_history[0]), fm_history[i]);
        fm_history_count = FM_HISTORY_MAX - 1;
        fm_history_pos = fm_history_count - 1;
    }
    fm_copy(fm_history[fm_history_count], sizeof(fm_history[0]), path);
    fm_history_pos = fm_history_count;
    fm_history_count++;
}

static void fm_apply_filter_sort(void) {
    fm_view_count = 0;
    for (u32 i = 0; i < fm_count && fm_view_count < FM_MAX_ITEMS; i++) {
        if (fm_contains(fm_items[i].name, fm_search)) {
            fm_view[fm_view_count++] = fm_items[i];
        }
    }
    fm_sort_view();
    if (fm_selected >= (i32)fm_view_count) fm_selected = (i32)fm_view_count - 1;
    if (fm_view_count == 0) fm_selected = -1;
    if (fm_scroll > fm_view_count) fm_scroll = 0;
}

static void fm_refresh(void) {
    fm_count = 0;
    memset(fm_items, 0, sizeof(fm_items));
    for (u32 i = 0; i < FM_MAX_ITEMS; i++) {
        vfs_dirent_t ent;
        if (sys_readdir(fm_path, i, &ent) != 0) break;
        fm_copy(fm_items[fm_count].name, sizeof(fm_items[fm_count].name), ent.name);
        fm_items[fm_count].size = ent.size;
        fm_items[fm_count].is_dir = ent.is_dir;
        fm_count++;
    }
    fm_apply_filter_sort();
    char line[160];
    ksnprintf(line, sizeof(line), "%lu item(s), %lu shown", (u64)fm_count, (u64)fm_view_count);
    fm_set_status(line);
}

static void fm_navigate(const char *path, bool history) {
    if (!path || !*path) return;
    fm_copy(fm_path, sizeof(fm_path), path);
    fm_selected = -1;
    fm_scroll = 0;
    if (history) fm_push_history(fm_path);
    fm_refresh();
}

static void fm_parent(void) {
    if (strcmp(fm_path, "/") == 0) return;
    char next[256];
    fm_copy(next, sizeof(next), fm_path);
    usize len = strlen(next);
    while (len > 1 && next[len - 1] == '/') next[--len] = 0;
    while (len > 1 && next[len - 1] != '/') len--;
    if (len <= 1) fm_copy(next, sizeof(next), "/");
    else next[len - 1] = 0;
    fm_navigate(next, true);
}

static const fm_item_t *fm_selected_item(void) {
    if (fm_selected < 0 || fm_selected >= (i32)fm_view_count) return NULL;
    return &fm_view[fm_selected];
}

static void fm_create_file(const char *name) {
    char path[256];
    fm_join(path, sizeof(path), fm_path, name);
    int fd = sys_open(path, 0x40 | 0x200 | 0x2);
    if (fd < 0) {
        fm_set_status("Create file failed");
        return;
    }
    sys_close(fd);
    fm_set_status("File created");
    fm_refresh();
}

static void fm_create_folder(const char *name) {
    char path[256];
    fm_join(path, sizeof(path), fm_path, name);
    if (sys_mkdir(path, 0755) == 0) {
        fm_set_status("Folder created");
        fm_refresh();
    } else {
        fm_set_status("Create folder failed");
    }
}

static void fm_delete_selected(void) {
    const fm_item_t *it = fm_selected_item();
    if (!it) return;
    char path[256];
    fm_join(path, sizeof(path), fm_path, it->name);
    if (it->is_dir) {
        fm_set_status("Folder delete is not available yet");
        return;
    }
    if (sys_unlink(path) == 0) {
        fm_set_status("Deleted");
        fm_selected = -1;
        fm_refresh();
    } else {
        fm_set_status("Delete failed");
    }
}

static void fm_open_selected(void) {
    const fm_item_t *it = fm_selected_item();
    if (!it) return;
    char path[256];
    fm_join(path, sizeof(path), fm_path, it->name);
    if (it->is_dir) {
        fm_navigate(path, true);
        return;
    }

    memset(fm_text, 0, sizeof(fm_text));
    fm_text_len = 0;
    int fd = sys_open(path, 0);
    if (fd >= 0) {
        isize n = sys_read(fd, fm_text, sizeof(fm_text) - 1);
        if (n > 0) fm_text_len = (u32)n;
        sys_close(fd);
    }
    fm_copy(fm_edit_file, sizeof(fm_edit_file), path);
    fm_mode = FM_MODE_EDIT_TEXT;
    fm_set_status("Editing file");
}

static void fm_save_text(void) {
    int fd = sys_open(fm_edit_file, 0x40 | 0x200 | 0x2);
    if (fd < 0) {
        fm_set_status("Save failed");
        return;
    }
    sys_write(fd, fm_text, fm_text_len);
    sys_close(fd);
    fm_set_status("Saved");
    fm_mode = FM_MODE_BROWSE;
    fm_refresh();
}

static bool hit(i32 x, i32 y, i32 w, i32 h) {
    return fm_mouse_x >= x && fm_mouse_x < x + w && fm_mouse_y >= y && fm_mouse_y < y + h;
}

static void draw_text_clip(wl_surface_t *s, i32 x, i32 y, const char *text, u32 color, u32 max_chars) {
    char buf[128];
    u32 i = 0;
    if (max_chars >= sizeof(buf)) max_chars = sizeof(buf) - 1;
    while (text && text[i] && i < max_chars) {
        buf[i] = text[i];
        i++;
    }
    buf[i] = 0;
    wl_draw_text(s, x, y, buf, color, 0);
}

static void draw_button(wl_surface_t *s, i32 x, i32 y, i32 w, const char *label, u32 color, bool enabled) {
    u32 fill = enabled ? color : 0xFFE5EAF1;
    u32 fg = enabled ? 0xFFFFFFFF : 0xFF8A95A5;
    wl_draw_rounded_rect(s, x, y, w, 30, 6, fill);
    wl_draw_rounded_outline(s, x, y, w, 30, 6, enabled ? 0xFFBFD0E6 : 0xFFD6DEE8);
    wl_draw_text(s, x + 10, y + 8, label, fg, 0);
}

static void draw_icon_box(wl_surface_t *s, i32 x, i32 y, const char *glyph, u32 bg, u32 fg) {
    wl_draw_rounded_rect(s, x, y, 24, 24, 5, bg);
    wl_draw_text(s, x + 8, y + 5, glyph, fg, 0);
}

static void draw_sidebar_item(wl_surface_t *s, i32 y, const char *label, const char *path, const char *glyph) {
    bool active = strcmp(fm_path, path) == 0;
    wl_draw_rounded_rect(s, 14, y, 166, 34, 6, active ? 0xFFDCEBFF : COL_SIDEBAR);
    if (active) wl_draw_rect(s, 14, y + 7, 3, 20, COL_PRIMARY);
    wl_draw_text(s, 28, y + 10, glyph, active ? COL_PRIMARY : COL_MUTED, 0);
    wl_draw_text(s, 54, y + 10, label, active ? COL_TEXT : 0xFF334155, 0);
}

static void draw_path_breadcrumbs(wl_surface_t *s) {
    wl_draw_rounded_rect(s, 306, 50, 378, 32, 6, fm_mode == FM_MODE_ADDRESS ? 0xFFE0ECFF : COL_PANEL);
    wl_draw_rounded_outline(s, 306, 50, 378, 32, 6, fm_mode == FM_MODE_ADDRESS ? COL_PRIMARY : COL_LINE);
    draw_text_clip(s, 320, 59, fm_mode == FM_MODE_ADDRESS ? fm_address : fm_path, COL_TEXT, 43);
}

static void draw_details(wl_surface_t *s) {
    wl_draw_rect(s, 712, 96, 1, 420, COL_LINE);
    wl_draw_text(s, 732, 112, "Details", COL_TEXT, 0);
    const fm_item_t *it = fm_selected_item();
    if (!it) {
        wl_draw_text(s, 732, 146, "No selection", COL_MUTED, 0);
        wl_draw_text(s, 732, 176, "Location", COL_MUTED, 0);
        draw_text_clip(s, 732, 198, fm_path, COL_TEXT, 18);
        return;
    }
    draw_icon_box(s, 732, 146, it->is_dir ? "D" : "F", it->is_dir ? 0xFFDBEAFE : 0xFFE5E7EB, it->is_dir ? COL_PRIMARY : 0xFF475569);
    draw_text_clip(s, 766, 151, it->name, COL_TEXT, 15);
    wl_draw_text(s, 732, 194, "Type", COL_MUTED, 0);
    wl_draw_text(s, 732, 216, fm_type_text(it), COL_TEXT, 0);
    wl_draw_text(s, 732, 250, "Size", COL_MUTED, 0);
    char size[48];
    ksnprintf(size, sizeof(size), it->is_dir ? "-" : "%lu bytes", it->size);
    wl_draw_text(s, 732, 272, size, COL_TEXT, 0);
    wl_draw_text(s, 732, 306, "Path", COL_MUTED, 0);
    char full[256];
    fm_join(full, sizeof(full), fm_path, it->name);
    draw_text_clip(s, 732, 328, full, COL_TEXT, 18);
}

static void draw_new_dialog(wl_surface_t *s) {
    wl_draw_rounded_rect(s, 294, 190, 312, 140, 8, 0xFFFFFFFF);
    wl_draw_rounded_outline(s, 294, 190, 312, 140, 8, 0xFF9DB7D8);
    wl_draw_rect(s, 294, 190, 312, 3, COL_PRIMARY);
    wl_draw_text(s, 318, 216, fm_mode == FM_MODE_NEW_FILE ? "New file" : "New folder", COL_TEXT, 0);
    wl_draw_rounded_rect(s, 318, 252, 264, 34, 6, 0xFFF8FAFC);
    wl_draw_rounded_outline(s, 318, 252, 264, 34, 6, COL_PRIMARY);
    draw_text_clip(s, 330, 262, fm_name_buf, COL_TEXT, 30);
    wl_draw_rect(s, 330 + (i32)fm_name_len * 8, 262, 8, 16, COL_PRIMARY);
    wl_draw_text(s, 318, 304, "Enter to create, Esc to cancel", COL_MUTED, 0);
}

static void draw_editor_lines(wl_surface_t *s) {
    i32 x = 34;
    i32 y = 104;
    char line[104];
    u32 col = 0;
    u32 rows = 0;
    for (u32 i = 0; i <= fm_text_len && rows < 23; i++) {
        char c = fm_text[i];
        if (c == '\n' || c == 0 || col >= 98) {
            line[col] = 0;
            wl_draw_text(s, x, y + (i32)rows * 16, line, COL_TEXT, 0);
            rows++;
            col = 0;
            if (c == 0) break;
        } else if (c >= 32 && c <= 126) {
            line[col++] = c;
        }
    }
}

static void fm_draw_editor(wl_surface_t *s) {
    wl_draw_rect(s, 0, 0, FM_W, FM_H, COL_APP_BG);
    wl_draw_rect(s, 0, 0, FM_W, 54, 0xFFFFFFFF);
    wl_draw_rect(s, 0, 54, FM_W, 1, COL_LINE);
    wl_draw_text(s, 22, 20, "Text Editor", COL_TEXT, 0);
    draw_text_clip(s, 126, 20, fm_edit_file, COL_MUTED, 64);
    draw_button(s, 710, 12, 74, "Save", COL_PRIMARY, true);
    draw_button(s, 794, 12, 82, "Cancel", 0xFF64748B, true);
    wl_draw_rounded_rect(s, 22, 82, FM_W - 44, FM_H - 124, 7, 0xFFFFFFFF);
    wl_draw_rounded_outline(s, 22, 82, FM_W - 44, FM_H - 124, 7, COL_LINE);
    draw_editor_lines(s);
    wl_draw_rect(s, 0, FM_H - 28, FM_W, 28, 0xFFFFFFFF);
    wl_draw_rect(s, 0, FM_H - 29, FM_W, 1, COL_LINE);
    char line[128];
    ksnprintf(line, sizeof(line), "%lu byte(s)", (u64)fm_text_len);
    wl_draw_text(s, 18, FM_H - 20, line, COL_MUTED, 0);
}

static void fm_draw(wl_surface_t *s) {
    if (fm_mode == FM_MODE_EDIT_TEXT) {
        fm_draw_editor(s);
        return;
    }

    wl_draw_rect(s, 0, 0, FM_W, FM_H, COL_APP_BG);
    wl_draw_rect(s, 0, 0, FM_W, 48, 0xFFFFFFFF);
    wl_draw_rect(s, 0, 48, FM_W, 1, COL_LINE);
    wl_draw_text(s, 20, 18, "File Manager", COL_TEXT, 0);
    wl_draw_text(s, 130, 18, "YamOS Explorer", COL_MUTED, 0);

    wl_draw_rect(s, 0, 49, 194, FM_H - 78, COL_SIDEBAR);
    wl_draw_rect(s, 193, 49, 1, FM_H - 78, COL_LINE);
    wl_draw_text(s, 22, 72, "Quick access", COL_MUTED, 0);
    draw_sidebar_item(s, 100, "Home", fm_home_dir, "H");
    draw_sidebar_item(s, 140, "Users", "/home", "U");
    draw_sidebar_item(s, 180, "Root", "/", "R");
    draw_sidebar_item(s, 220, "Temp", "/tmp", "T");
    draw_sidebar_item(s, 260, "System", "/var", "S");
    draw_sidebar_item(s, 300, "Volumes", "/mnt", "V");

    draw_button(s, 210, 52, 38, "<", 0xFF64748B, fm_history_pos > 0);
    draw_button(s, 254, 52, 38, ">", 0xFF64748B, fm_history_pos + 1 < fm_history_count);
    draw_path_breadcrumbs(s);
    wl_draw_rounded_rect(s, 696, 50, 184, 32, 6, fm_mode == FM_MODE_SEARCH ? 0xFFE0ECFF : COL_PANEL);
    wl_draw_rounded_outline(s, 696, 50, 184, 32, 6, fm_mode == FM_MODE_SEARCH ? COL_PRIMARY : COL_LINE);
    draw_text_clip(s, 710, 59, fm_search_len ? fm_search : "Search", fm_search_len ? COL_TEXT : COL_MUTED, 19);

    draw_button(s, 210, 96, 50, "Up", 0xFF64748B, true);
    draw_button(s, 268, 96, 76, "Refresh", 0xFF64748B, true);
    draw_button(s, 352, 96, 82, "New file", COL_PRIMARY, true);
    draw_button(s, 442, 96, 96, "New folder", COL_PRIMARY, true);
    draw_button(s, 546, 96, 70, "Open", COL_OK, fm_selected_item() != NULL);
    draw_button(s, 624, 96, 78, "Delete", COL_DANGER, fm_selected_item() != NULL);

    wl_draw_rounded_rect(s, 210, 140, 486, 338, 7, COL_PANEL);
    wl_draw_rounded_outline(s, 210, 140, 486, 338, 7, COL_LINE);
    wl_draw_rect(s, 210, 170, 486, 1, COL_LINE);
    wl_draw_text(s, 228, 151, fm_sort == FM_SORT_NAME ? (fm_sort_desc ? "Name v" : "Name ^") : "Name", COL_MUTED, 0);
    wl_draw_text(s, 512, 151, fm_sort == FM_SORT_TYPE ? (fm_sort_desc ? "Type v" : "Type ^") : "Type", COL_MUTED, 0);
    wl_draw_text(s, 624, 151, fm_sort == FM_SORT_SIZE ? (fm_sort_desc ? "Size v" : "Size ^") : "Size", COL_MUTED, 0);

    for (u32 row = 0; row < FM_VISIBLE_ROWS; row++) {
        u32 idx = fm_scroll + row;
        if (idx >= fm_view_count) break;
        fm_item_t *it = &fm_view[idx];
        i32 y = 178 + (i32)row * 16;
        if ((i32)idx == fm_selected) wl_draw_rect(s, 216, y - 3, 468, 18, 0xFFDCEBFF);
        else if (row % 2 == 1) wl_draw_rect(s, 216, y - 3, 468, 18, 0xFFF8FAFC);
        draw_icon_box(s, 224, y - 5, it->is_dir ? "D" : "F", it->is_dir ? 0xFFDBEAFE : 0xFFE5E7EB, it->is_dir ? COL_PRIMARY : 0xFF475569);
        draw_text_clip(s, 258, y, it->name, COL_TEXT, 29);
        wl_draw_text(s, 512, y, fm_type_text(it), COL_MUTED, 0);
        if (!it->is_dir) {
            char size[32];
            ksnprintf(size, sizeof(size), "%lu", it->size);
            wl_draw_text(s, 624, y, size, COL_MUTED, 0);
        }
    }

    if (fm_view_count == 0) {
        wl_draw_text(s, 356, 300, fm_search_len ? "No matching items" : "Folder is empty", COL_MUTED, 0);
    }

    draw_details(s);

    if (fm_mode == FM_MODE_NEW_FILE || fm_mode == FM_MODE_NEW_FOLDER) draw_new_dialog(s);

    wl_draw_rect(s, 0, FM_H - 29, FM_W, 1, COL_LINE);
    wl_draw_rect(s, 0, FM_H - 28, FM_W, 28, 0xFFFFFFFF);
    wl_draw_text(s, 18, FM_H - 20, fm_status, COL_MUTED, 0);
    char count[96];
    ksnprintf(count, sizeof(count), "%lu total | %lu shown", (u64)fm_count, (u64)fm_view_count);
    wl_draw_text(s, 720, FM_H - 20, count, COL_MUTED, 0);
}

static void fm_begin_name(fm_mode_t mode) {
    fm_mode = mode;
    fm_name_len = 0;
    fm_name_buf[0] = 0;
}

static void fm_finish_name(void) {
    if (fm_name_len == 0) {
        fm_mode = FM_MODE_BROWSE;
        return;
    }
    if (fm_mode == FM_MODE_NEW_FILE) fm_create_file(fm_name_buf);
    if (fm_mode == FM_MODE_NEW_FOLDER) fm_create_folder(fm_name_buf);
    fm_mode = FM_MODE_BROWSE;
}

static void fm_sort_by(fm_sort_t sort) {
    if (fm_sort == sort) fm_sort_desc = !fm_sort_desc;
    else {
        fm_sort = sort;
        fm_sort_desc = false;
    }
    fm_apply_filter_sort();
}

static void fm_click(void) {
    if (fm_mode == FM_MODE_EDIT_TEXT) {
        if (hit(710, 12, 74, 30)) fm_save_text();
        else if (hit(794, 12, 82, 30)) fm_mode = FM_MODE_BROWSE;
        return;
    }

    if (hit(14, 100, 166, 34)) { fm_navigate(fm_home_dir, true); return; }
    if (hit(14, 140, 166, 34)) { fm_navigate("/home", true); return; }
    if (hit(14, 180, 166, 34)) { fm_navigate("/", true); return; }
    if (hit(14, 220, 166, 34)) { fm_navigate("/tmp", true); return; }
    if (hit(14, 260, 166, 34)) { fm_navigate("/var", true); return; }
    if (hit(14, 300, 166, 34)) { fm_navigate("/mnt", true); return; }

    if (hit(210, 52, 38, 30) && fm_history_pos > 0) {
        fm_history_pos--;
        fm_navigate(fm_history[fm_history_pos], false);
        return;
    }
    if (hit(254, 52, 38, 30) && fm_history_pos + 1 < fm_history_count) {
        fm_history_pos++;
        fm_navigate(fm_history[fm_history_pos], false);
        return;
    }
    if (hit(306, 50, 378, 32)) {
        fm_copy(fm_address, sizeof(fm_address), fm_path);
        fm_address_len = strlen(fm_address);
        fm_mode = FM_MODE_ADDRESS;
        return;
    }
    if (hit(696, 50, 184, 32)) {
        fm_mode = FM_MODE_SEARCH;
        return;
    }
    if (hit(210, 96, 50, 30)) { fm_parent(); return; }
    if (hit(268, 96, 76, 30)) { fm_refresh(); return; }
    if (hit(352, 96, 82, 30)) { fm_begin_name(FM_MODE_NEW_FILE); return; }
    if (hit(442, 96, 96, 30)) { fm_begin_name(FM_MODE_NEW_FOLDER); return; }
    if (hit(546, 96, 70, 30)) { fm_open_selected(); return; }
    if (hit(624, 96, 78, 30)) { fm_delete_selected(); return; }
    if (hit(220, 146, 250, 24)) { fm_sort_by(FM_SORT_NAME); return; }
    if (hit(500, 146, 98, 24)) { fm_sort_by(FM_SORT_TYPE); return; }
    if (hit(612, 146, 74, 24)) { fm_sort_by(FM_SORT_SIZE); return; }

    if (fm_mouse_x >= 216 && fm_mouse_x < 684 && fm_mouse_y >= 175 && fm_mouse_y < 466) {
        i32 row = (fm_mouse_y - 175) / 16;
        i32 idx = (i32)fm_scroll + row;
        if (idx >= 0 && idx < (i32)fm_view_count) {
            if (fm_selected == idx) fm_open_selected();
            else fm_selected = idx;
        }
    }
}

static void fm_text_input(char *buf, u32 *len, u32 cap, char c) {
    if (c == '\b' && *len > 0) {
        buf[--(*len)] = 0;
    } else if (c >= 32 && c <= 126 && *len + 1 < cap) {
        buf[(*len)++] = c;
        buf[*len] = 0;
    }
}

static void fm_key(u16 sc) {
    char c = 0;
    if (sc < 128) c = fm_shift ? sc_ascii_shift[sc] : sc_ascii[sc];

    if (fm_mode == FM_MODE_NEW_FILE || fm_mode == FM_MODE_NEW_FOLDER) {
        if (c == '\n') fm_finish_name();
        else if (c == 27) fm_mode = FM_MODE_BROWSE;
        else fm_text_input(fm_name_buf, &fm_name_len, sizeof(fm_name_buf), c);
        return;
    }
    if (fm_mode == FM_MODE_ADDRESS) {
        if (c == '\n') {
            fm_mode = FM_MODE_BROWSE;
            fm_navigate(fm_address, true);
        } else if (c == 27) {
            fm_mode = FM_MODE_BROWSE;
        } else {
            fm_text_input(fm_address, &fm_address_len, sizeof(fm_address), c);
        }
        return;
    }
    if (fm_mode == FM_MODE_SEARCH) {
        if (c == '\n' || c == 27) fm_mode = FM_MODE_BROWSE;
        else {
            fm_text_input(fm_search, &fm_search_len, sizeof(fm_search), c);
            fm_apply_filter_sort();
        }
        return;
    }
    if (fm_mode == FM_MODE_EDIT_TEXT) {
        if (c == 27) fm_mode = FM_MODE_BROWSE;
        else if (c == '\b' && fm_text_len > 0) fm_text[--fm_text_len] = 0;
        else if (c == '\n' && fm_text_len + 1 < FM_TEXT_MAX) {
            fm_text[fm_text_len++] = '\n';
            fm_text[fm_text_len] = 0;
        } else if (c >= 32 && c <= 126 && fm_text_len + 1 < FM_TEXT_MAX) {
            fm_text[fm_text_len++] = c;
            fm_text[fm_text_len] = 0;
        }
        return;
    }

    if (sc == 0x0E) fm_delete_selected();       /* Backspace */
    else if (sc == 0x1C) fm_open_selected();    /* Enter */
    else if (sc == 0x48 && fm_selected > 0) {   /* Up */
        fm_selected--;
        if (fm_selected < (i32)fm_scroll) fm_scroll = (u32)fm_selected;
    } else if (sc == 0x50 && fm_selected + 1 < (i32)fm_view_count) { /* Down */
        fm_selected++;
        if (fm_selected >= (i32)(fm_scroll + FM_VISIBLE_ROWS)) fm_scroll++;
    } else if (sc == 0x49 && fm_scroll > 0) {   /* PgUp */
        fm_scroll = fm_scroll > FM_VISIBLE_ROWS ? fm_scroll - FM_VISIBLE_ROWS : 0;
    } else if (sc == 0x51 && fm_scroll + FM_VISIBLE_ROWS < fm_view_count) { /* PgDn */
        fm_scroll += FM_VISIBLE_ROWS;
    } else if (c == '/') {
        fm_mode = FM_MODE_SEARCH;
    }
}

void wl_file_manager_task(void *arg) {
    (void)arg;
    task_sleep_ms(250);

    /* Resolve home dir from currently logged-in user */
    wl_compositor_t *comp = wl_get_compositor();
    if (comp && comp->current_user[0]) {
        ksnprintf(fm_home_dir, sizeof(fm_home_dir), "/home/%s", comp->current_user);
        /* Also set initial path if we haven't navigated yet */
        if (strcmp(fm_path, "/home/root") == 0 || fm_path[0] == 0) {
            fm_copy(fm_path, sizeof(fm_path), fm_home_dir);
        }
    } else {
        fm_copy(fm_home_dir, sizeof(fm_home_dir), "/home/root");
        fm_copy(fm_path, sizeof(fm_path), "/home/root");
    }
    wl_surface_t *s = wl_surface_create("File Manager", 120, 70, FM_W, FM_H, sched_current()->id);
    if (!s) return;
    fm_push_history(fm_path);
    fm_refresh();
    fm_draw(s);
    wl_surface_commit(s);

    u32 my_id = s->id;
    while (s->state == WL_SURFACE_ACTIVE && s->id == my_id) {
        input_event_t ev;
        while (wl_surface_pop_event(s, &ev)) {
            if (ev.type == EV_ABS && ev.code == 0) fm_mouse_x = ev.value;
            else if (ev.type == EV_ABS && ev.code == 1) fm_mouse_y = ev.value;
            else if (ev.type == EV_KEY) {
                if (ev.code == 0x2A || ev.code == 0x36) {
                    fm_shift = ev.value == KEY_PRESSED;
                } else if (ev.value == KEY_PRESSED) {
                    if (ev.code >= 0x110) fm_click();
                    else fm_key(ev.code);
                }
            }
        }
        fm_draw(s);
        wl_surface_commit(s);
        task_sleep_ms(16);
    }
}
