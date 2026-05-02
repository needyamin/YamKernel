/* ============================================================================
 * YamKernel - compositor-native Calculator
 * Professional desktop calculator for YamOS: standard/scientific operations,
 * memory register, history, keyboard input, and compositor clipboard support.
 * ============================================================================ */
#include <nexus/types.h>
#include "../sched/sched.h"
#include "../sched/wait.h"
#include "compositor.h"
#include "wl_draw.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

#define CALC_W 520
#define CALC_H 560
#define SCALE 10000LL
#define MAX_DIGITS 18
#define HIST_MAX 6

#define COL_BG       0xFFF5F7FB
#define COL_PANEL    0xFFFFFFFF
#define COL_LINE     0xFFD8E0EA
#define COL_TEXT     0xFF172033
#define COL_MUTED    0xFF657186
#define COL_PRIMARY  0xFF2563EB
#define COL_OP       0xFFE8F0FF
#define COL_FN       0xFFF1F5F9
#define COL_DANGER   0xFFFFE4E6
#define COL_OK       0xFFDCFCE7

typedef enum {
    CALC_MODE_STANDARD = 0,
    CALC_MODE_SCIENTIFIC
} calc_mode_t;

typedef struct {
    const char *label;
    i32 x, y, w, h;
    u32 color;
} calc_button_t;

static i64 calc_value = 0;
static i64 calc_left = 0;
static i64 calc_memory = 0;
static char calc_op = 0;
static bool calc_new_input = true;
static bool calc_error = false;
static calc_mode_t calc_mode = CALC_MODE_STANDARD;
static char display_buf[64] = "0";
static char expr_buf[96] = "";
static char status_buf[128] = "GUI + keyboard + clipboard ready";
static char history[HIST_MAX][96];
static u32 history_count = 0;
static i32 last_x = -1;
static i32 last_y = -1;
static bool shift_held = false;

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

static i64 iabs64(i64 v) {
    return v < 0 ? -v : v;
}

static i64 isqrt64(i64 n) {
    if (n <= 0) return 0;
    i64 x = n;
    i64 y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

static void format_fixed(i64 v, char *out, usize cap) {
    bool neg = v < 0;
    u64 av = (u64)iabs64(v);
    u64 whole = av / SCALE;
    u64 frac = av % SCALE;
    char tmp[64];
    if (frac == 0) {
        ksnprintf(tmp, sizeof(tmp), "%s%lu", neg ? "-" : "", whole);
    } else {
        ksnprintf(tmp, sizeof(tmp), "%s%lu.%04lu", neg ? "-" : "", whole, frac);
        int len = strlen(tmp);
        while (len > 0 && tmp[len - 1] == '0') tmp[--len] = 0;
    }
    copy_text(out, cap, tmp);
}

static bool parse_fixed(const char *s, i64 *out) {
    if (!s || !*s || !out) return false;
    bool neg = false;
    if (*s == '-') {
        neg = true;
        s++;
    }
    i64 whole = 0;
    i64 frac = 0;
    i64 place = SCALE / 10;
    bool any = false;
    while (*s >= '0' && *s <= '9') {
        whole = whole * 10 + (*s++ - '0');
        any = true;
    }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9' && place > 0) {
            frac += (*s++ - '0') * place;
            place /= 10;
            any = true;
        }
    }
    if (!any) return false;
    *out = (whole * SCALE + frac) * (neg ? -1 : 1);
    return true;
}

static void update_display(void) {
    if (calc_error) {
        copy_text(display_buf, sizeof(display_buf), "Error");
        return;
    }
    format_fixed(calc_value, display_buf, sizeof(display_buf));
}

static void set_status(const char *s) {
    copy_text(status_buf, sizeof(status_buf), s);
}

static void add_history(const char *line) {
    if (!line || !*line) return;
    if (history_count < HIST_MAX) {
        copy_text(history[history_count++], sizeof(history[0]), line);
    } else {
        for (u32 i = 1; i < HIST_MAX; i++) copy_text(history[i - 1], sizeof(history[0]), history[i]);
        copy_text(history[HIST_MAX - 1], sizeof(history[0]), line);
    }
}

static void clear_all(void) {
    calc_value = 0;
    calc_left = 0;
    calc_op = 0;
    calc_new_input = true;
    calc_error = false;
    expr_buf[0] = 0;
    update_display();
    set_status("Ready");
}

static void clear_entry(void) {
    calc_value = 0;
    calc_new_input = true;
    calc_error = false;
    update_display();
}

static bool apply_op(i64 left, i64 right, char op, i64 *out) {
    if (!out) return false;
    if (op == '+') *out = left + right;
    else if (op == '-') *out = left - right;
    else if (op == '*') *out = (left * right) / SCALE;
    else if (op == '/') {
        if (right == 0) return false;
        *out = (left * SCALE) / right;
    } else *out = right;
    return true;
}

static void input_digit(int d) {
    if (calc_error) clear_all();
    if (calc_new_input) {
        calc_value = d * SCALE;
        calc_new_input = false;
    } else {
        bool neg = calc_value < 0;
        i64 av = iabs64(calc_value);
        i64 whole = av / SCALE;
        i64 frac = av % SCALE;
        if (whole < 999999999999LL) whole = whole * 10 + d;
        calc_value = (whole * SCALE + frac) * (neg ? -1 : 1);
    }
    update_display();
}

static void input_decimal(void) {
    if (calc_new_input) {
        calc_value = 0;
        calc_new_input = false;
    }
    if (!strstr(display_buf, ".")) {
        usize len = strlen(display_buf);
        if (len + 1 < sizeof(display_buf)) {
            display_buf[len] = '.';
            display_buf[len + 1] = 0;
        }
    }
}

static void input_backspace(void) {
    if (calc_new_input || calc_error) return;
    i64 av = iabs64(calc_value);
    i64 whole = av / SCALE;
    i64 frac = av % SCALE;
    if (frac != 0) frac = (frac / 10) * 10;
    else whole /= 10;
    calc_value = (whole * SCALE + frac) * (calc_value < 0 ? -1 : 1);
    update_display();
}

static void choose_op(char op) {
    if (calc_error) return;
    if (calc_op && !calc_new_input) {
        i64 result = 0;
        if (!apply_op(calc_left, calc_value, calc_op, &result)) {
            calc_error = true;
            update_display();
            set_status("Math error");
            return;
        }
        calc_value = result;
    }
    calc_left = calc_value;
    calc_op = op;
    calc_new_input = true;
    char left[48];
    format_fixed(calc_left, left, sizeof(left));
    ksnprintf(expr_buf, sizeof(expr_buf), "%s %c", left, op);
    update_display();
}

static void equals(void) {
    if (calc_error) return;
    i64 result = 0;
    if (!apply_op(calc_left, calc_value, calc_op, &result)) {
        calc_error = true;
        update_display();
        set_status("Math error");
        return;
    }
    char left[48], right[48], res[48], line[96];
    format_fixed(calc_left, left, sizeof(left));
    format_fixed(calc_value, right, sizeof(right));
    format_fixed(result, res, sizeof(res));
    if (calc_op) {
        ksnprintf(line, sizeof(line), "%s %c %s = %s", left, calc_op, right, res);
        add_history(line);
    }
    calc_value = result;
    calc_left = 0;
    calc_op = 0;
    calc_new_input = true;
    expr_buf[0] = 0;
    update_display();
}

static void unary_percent(void) {
    calc_value /= 100;
    calc_new_input = true;
    update_display();
}

static void unary_negate(void) {
    calc_value = -calc_value;
    update_display();
}

static void unary_square(void) {
    calc_value = (calc_value * calc_value) / SCALE;
    calc_new_input = true;
    update_display();
}

static void unary_sqrt(void) {
    if (calc_value < 0) {
        calc_error = true;
        update_display();
        set_status("Cannot square-root a negative value");
        return;
    }
    calc_value = isqrt64(calc_value * SCALE);
    calc_new_input = true;
    update_display();
}

static void unary_reciprocal(void) {
    if (calc_value == 0) {
        calc_error = true;
        update_display();
        set_status("Division by zero");
        return;
    }
    calc_value = (SCALE * SCALE) / calc_value;
    calc_new_input = true;
    update_display();
}

static void memory_clear(void) { calc_memory = 0; set_status("Memory cleared"); }
static void memory_recall(void) { calc_value = calc_memory; calc_new_input = true; update_display(); set_status("Memory recalled"); }
static void memory_add(void) { calc_memory += calc_value; set_status("Added to memory"); }
static void memory_sub(void) { calc_memory -= calc_value; set_status("Subtracted from memory"); }
static void memory_store(void) { calc_memory = calc_value; set_status("Stored in memory"); }

static void copy_result(void) {
    wl_clipboard_set_text(display_buf, strlen(display_buf));
    set_status("Copied result to compositor clipboard");
}

static void paste_result(void) {
    char buf[96];
    if (wl_clipboard_get_text(buf, sizeof(buf)) <= 0) {
        set_status("Clipboard is empty");
        return;
    }
    i64 v = 0;
    if (parse_fixed(buf, &v)) {
        calc_value = v;
        calc_new_input = true;
        calc_error = false;
        update_display();
        set_status("Pasted number from clipboard");
    } else {
        set_status("Clipboard does not contain a number");
    }
}

static void toggle_mode(void) {
    calc_mode = calc_mode == CALC_MODE_STANDARD ? CALC_MODE_SCIENTIFIC : CALC_MODE_STANDARD;
    set_status(calc_mode == CALC_MODE_STANDARD ? "Standard mode" : "Scientific mode");
}

static void handle_label(const char *label) {
    if (!label || !*label) return;
    if (label[0] >= '0' && label[0] <= '9' && label[1] == 0) input_digit(label[0] - '0');
    else if (strcmp(label, ".") == 0) input_decimal();
    else if (strcmp(label, "+") == 0 || strcmp(label, "-") == 0 ||
             strcmp(label, "*") == 0 || strcmp(label, "/") == 0) choose_op(label[0]);
    else if (strcmp(label, "=") == 0) equals();
    else if (strcmp(label, "C") == 0) clear_all();
    else if (strcmp(label, "CE") == 0) clear_entry();
    else if (strcmp(label, "<") == 0) input_backspace();
    else if (strcmp(label, "+/-") == 0) unary_negate();
    else if (strcmp(label, "%") == 0) unary_percent();
    else if (strcmp(label, "x2") == 0) unary_square();
    else if (strcmp(label, "sqrt") == 0) unary_sqrt();
    else if (strcmp(label, "1/x") == 0) unary_reciprocal();
    else if (strcmp(label, "MC") == 0) memory_clear();
    else if (strcmp(label, "MR") == 0) memory_recall();
    else if (strcmp(label, "M+") == 0) memory_add();
    else if (strcmp(label, "M-") == 0) memory_sub();
    else if (strcmp(label, "MS") == 0) memory_store();
    else if (strcmp(label, "Copy") == 0) copy_result();
    else if (strcmp(label, "Paste") == 0) paste_result();
    else if (strcmp(label, "Mode") == 0) toggle_mode();
}

static calc_button_t buttons[] = {
    { "MC", 24, 188, 58, 34, COL_FN }, { "MR", 88, 188, 58, 34, COL_FN },
    { "M+", 152, 188, 58, 34, COL_FN }, { "M-", 216, 188, 58, 34, COL_FN },
    { "MS", 280, 188, 58, 34, COL_FN }, { "Mode", 344, 188, 72, 34, COL_FN },
    { "Copy", 424, 188, 72, 34, COL_FN },

    { "%", 24, 236, 82, 46, COL_FN }, { "CE", 112, 236, 82, 46, COL_FN },
    { "C", 200, 236, 82, 46, COL_DANGER }, { "<", 288, 236, 82, 46, COL_FN },
    { "/", 376, 236, 82, 46, COL_OP },

    { "1/x", 24, 288, 82, 46, COL_FN }, { "x2", 112, 288, 82, 46, COL_FN },
    { "sqrt", 200, 288, 82, 46, COL_FN }, { "+/-", 288, 288, 82, 46, COL_FN },
    { "*", 376, 288, 82, 46, COL_OP },

    { "7", 24, 340, 82, 46, COL_PANEL }, { "8", 112, 340, 82, 46, COL_PANEL },
    { "9", 200, 340, 82, 46, COL_PANEL }, { "-", 288, 340, 82, 46, COL_OP },
    { "Paste", 376, 340, 82, 46, COL_FN },

    { "4", 24, 392, 82, 46, COL_PANEL }, { "5", 112, 392, 82, 46, COL_PANEL },
    { "6", 200, 392, 82, 46, COL_PANEL }, { "+", 288, 392, 82, 46, COL_OP },
    { "=", 376, 392, 82, 98, COL_OK },

    { "1", 24, 444, 82, 46, COL_PANEL }, { "2", 112, 444, 82, 46, COL_PANEL },
    { "3", 200, 444, 82, 46, COL_PANEL },

    { "0", 24, 496, 170, 46, COL_PANEL }, { ".", 200, 496, 82, 46, COL_PANEL },
};

static bool hit_button(const calc_button_t *b, i32 x, i32 y) {
    return b && x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
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

static void draw_button(wl_surface_t *s, const calc_button_t *b) {
    wl_draw_rounded_rect(s, b->x, b->y, b->w, b->h, 7, b->color);
    wl_draw_rounded_outline(s, b->x, b->y, b->w, b->h, 7, COL_LINE);
    u32 fg = (b->color == COL_OP || b->color == COL_OK) ? COL_PRIMARY : COL_TEXT;
    if (b->color == COL_DANGER) fg = 0xFFB42318;
    i32 tx = b->x + ((b->w - (i32)strlen(b->label) * 8) / 2);
    if (tx < b->x + 8) tx = b->x + 8;
    wl_draw_text(s, tx, b->y + (b->h - 16) / 2, b->label, fg, 0);
}

static void draw_calculator(wl_surface_t *s) {
    wl_draw_rect(s, 0, 0, CALC_W, CALC_H, COL_BG);
    wl_draw_rect(s, 0, 0, CALC_W, 44, COL_PANEL);
    wl_draw_rect(s, 0, 44, CALC_W, 1, COL_LINE);
    wl_draw_text(s, 22, 16, "Calculator", COL_TEXT, 0);
    wl_draw_text(s, 128, 16, calc_mode == CALC_MODE_STANDARD ? "Standard" : "Scientific", COL_MUTED, 0);
    wl_draw_text(s, 330, 16, "GUI Clipboard Memory", COL_MUTED, 0);

    wl_draw_rounded_rect(s, 22, 62, 474, 104, 8, 0xFF111827);
    wl_draw_rounded_outline(s, 22, 62, 474, 104, 8, 0xFF334155);
    draw_text_clip(s, 38, 80, expr_buf, 0xFF94A3B8, 54);
    int len = strlen(display_buf);
    i32 dx = 474 - len * 8;
    if (dx < 36) dx = 36;
    wl_draw_text(s, dx, 114, display_buf, calc_error ? 0xFFFFB4B4 : 0xFFFFFFFF, 0);

    char mem[80];
    char memv[48];
    format_fixed(calc_memory, memv, sizeof(memv));
    ksnprintf(mem, sizeof(mem), "M %s", memv);
    draw_text_clip(s, 38, 148, mem, 0xFFCBD5E1, 32);

    for (u32 i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        draw_button(s, &buttons[i]);
    }

    wl_draw_rect(s, 0, CALC_H - 70, CALC_W, 1, COL_LINE);
    wl_draw_text(s, 24, CALC_H - 56, "History", COL_MUTED, 0);
    u32 start = history_count > 2 ? history_count - 2 : 0;
    for (u32 i = start; i < history_count; i++) {
        draw_text_clip(s, 96, CALC_H - 56 + (i - start) * 18, history[i], COL_TEXT, 48);
    }
    wl_draw_text(s, 24, CALC_H - 18, status_buf, COL_MUTED, 0);
}

static void handle_click(i32 x, i32 y) {
    for (u32 i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        if (hit_button(&buttons[i], x, y)) {
            handle_label(buttons[i].label);
            return;
        }
    }
}

static void handle_key(u16 sc) {
    char c = 0;
    if (sc < 128) c = shift_held ? sc_ascii_shift[sc] : sc_ascii[sc];
    if (c >= '0' && c <= '9') input_digit(c - '0');
    else if (c == '.') input_decimal();
    else if (c == '+') choose_op('+');
    else if (c == '-') choose_op('-');
    else if (c == '*') choose_op('*');
    else if (c == '/') choose_op('/');
    else if (c == '=' || c == '\n') equals();
    else if (c == '\b') input_backspace();
    else if (c == 27) clear_all();
    else if (c == '%') unary_percent();
    else if (c == 'c' || c == 'C') clear_all();
    else if (c == 'm' || c == 'M') memory_store();
    else if (c == 'r' || c == 'R') memory_recall();
    else if (c == 's' || c == 'S') unary_sqrt();
}

void wl_calc_task(void *arg) {
    (void)arg;
    task_sleep_ms(200);
    wl_surface_t *s = wl_surface_create("Calculator", 650, 50, CALC_W, CALC_H, sched_current()->id);
    if (!s) return;

    clear_all();
    kprintf("[CALC] YamOS Calculator ready: gui=1 keyboard=1 memory=1 clipboard=1 fixed_decimal=4\n");
    draw_calculator(s);
    wl_surface_commit(s);

    u32 my_id = s->id;
    while (s->state == WL_SURFACE_ACTIVE && s->id == my_id) {
        input_event_t ev;
        bool dirty = false;
        while (wl_surface_pop_event(s, &ev)) {
            if (ev.type == EV_ABS && ev.code == 0) last_x = ev.value;
            else if (ev.type == EV_ABS && ev.code == 1) last_y = ev.value;
            else if (ev.type == EV_CLIPBOARD && ev.code == CLIPBOARD_COPY) {
                copy_result();
                dirty = true;
            } else if (ev.type == EV_CLIPBOARD && ev.code == CLIPBOARD_PASTE) {
                paste_result();
                dirty = true;
            } else if (ev.type == EV_KEY) {
                if (ev.code == 0x2A || ev.code == 0x36) {
                    shift_held = ev.value == KEY_PRESSED;
                } else if (ev.value == KEY_PRESSED) {
                    if (ev.code >= 0x110 && last_x >= 0 && last_y >= 0) handle_click(last_x, last_y);
                    else handle_key(ev.code);
                    dirty = true;
                }
            }
        }
        if (dirty) {
            draw_calculator(s);
            wl_surface_commit(s);
        }
        task_sleep_ms(16);
    }
}
