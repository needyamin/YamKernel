#include "yam.h"
#include "wl_draw_user.h"

extern const uint8_t font_basic_8x16[128][16];

#define CALC_W  280
#define CALC_H  400

#define C_BG       0xFF111827
#define C_PANEL    0xFF1F2937
#define C_KEY      0xFF374151
#define C_KEY_2    0xFF4B5563
#define C_TEXT     0xFFF9FAFB
#define C_MUTED    0xFF9CA3AF
#define C_OP       0xFF60A5FA
#define C_EQ       0xFF34D399
#define C_CLEAR    0xFFFB7185

static char display_buf[32] = "0";
static char expr_buf[32] = "Ready";
static i32 current_val = 0;
static i32 accumulator = 0;
static char current_op = 0;
static bool new_input = true;

static void set_expr(const char *s) {
    int i = 0;
    while (s[i] && i < 31) {
        expr_buf[i] = s[i];
        i++;
    }
    expr_buf[i] = 0;
}

static void update_display(void) {
    utoa(display_buf, current_val);
}

static void calc_push_digit(int d) {
    if (new_input) {
        current_val = d;
        new_input = false;
    } else if (current_val < 100000000) {
        current_val = current_val * 10 + d;
    }
    update_display();
}

static void calc_reset(void) {
    current_val = 0;
    accumulator = 0;
    current_op = 0;
    new_input = true;
    display_buf[0] = '0';
    display_buf[1] = 0;
    set_expr("Ready");
}

static void calc_execute_op(void) {
    if (current_op == '+') accumulator += current_val;
    else if (current_op == '-') accumulator -= current_val;
    else if (current_op == '*') accumulator *= current_val;
    else if (current_op == '/') {
        if (current_val != 0) accumulator /= current_val;
        else {
            set_expr("Division by zero");
            current_val = 0;
            accumulator = 0;
            update_display();
            new_input = true;
            return;
        }
    } else {
        accumulator = current_val;
    }
    current_val = accumulator;
    update_display();
    new_input = true;
}

static void calc_press(char c) {
    if (c >= '0' && c <= '9') {
        calc_push_digit(c - '0');
    } else if (c == 'C') {
        calc_reset();
    } else if (c == '=') {
        calc_execute_op();
        current_op = 0;
        set_expr("Result");
    } else {
        calc_execute_op();
        current_op = c;
        expr_buf[0] = 'A';
        expr_buf[1] = 'c';
        expr_buf[2] = 'c';
        expr_buf[3] = ' ';
        expr_buf[4] = c;
        expr_buf[5] = 0;
    }
}

static void calc_handle_click(i32 x, i32 y) {
    if (y < 92) return;
    int bx = (x - 12) / 64;
    int by = (y - 92) / 62;
    if (x < 12 || bx < 0 || bx > 3 || by < 0 || by > 3) return;

    char btn[4][4] = {
        {'7', '8', '9', '/'},
        {'4', '5', '6', '*'},
        {'1', '2', '3', '-'},
        {'C', '0', '=', '+'}
    };
    calc_press(btn[by][bx]);
}

static char key_to_calc(u16 sc) {
    if (sc == 2) return '1';
    if (sc == 3) return '2';
    if (sc == 4) return '3';
    if (sc == 5) return '4';
    if (sc == 6) return '5';
    if (sc == 7) return '6';
    if (sc == 8) return '7';
    if (sc == 9) return '8';
    if (sc == 10) return '9';
    if (sc == 11) return '0';
    if (sc == 12) return '-';
    if (sc == 13) return '+';
    if (sc == 28) return '=';
    if (sc == 14) return 'C';
    if (sc == 55) return '*';
    if (sc == 53) return '/';
    return 0;
}

static void draw_calculator(wl_user_buffer_t *buf) {
    wl_user_draw_rect(buf, 0, 0, CALC_W, CALC_H, C_BG);
    wl_user_draw_rect(buf, 12, 14, 256, 62, 0xFF0B1220);
    wl_user_draw_text(buf, 24, 26, expr_buf, C_MUTED);
    int len = strlen(display_buf);
    int dx = 256 - (len * 8);
    if (dx < 24) dx = 24;
    wl_user_draw_text(buf, dx, 52, display_buf, C_TEXT);

    char btn[4][4] = {
        {'7', '8', '9', '/'},
        {'4', '5', '6', '*'},
        {'1', '2', '3', '-'},
        {'C', '0', '=', '+'}
    };

    for (int by = 0; by < 4; by++) {
        for (int bx = 0; bx < 4; bx++) {
            i32 px = 12 + bx * 64;
            i32 py = 92 + by * 62;
            u32 color = C_KEY;
            u32 text_color = C_TEXT;
            if (btn[by][bx] == '/' || btn[by][bx] == '*' || btn[by][bx] == '-' || btn[by][bx] == '+') color = C_OP;
            if (btn[by][bx] == '=') color = C_EQ;
            if (btn[by][bx] == 'C') color = C_CLEAR;
            if (color == C_OP || color == C_EQ || color == C_CLEAR) text_color = C_BG;
            if (btn[by][bx] >= '0' && btn[by][bx] <= '9' && by % 2) color = C_KEY_2;

            wl_user_draw_rect(buf, px, py, 56, 52, color);
            wl_user_draw_rect(buf, px, py + 51, 56, 1, 0xFF0B1220);
            char str[2] = { btn[by][bx], 0 };
            wl_user_draw_text(buf, px + 24, py + 18, str, text_color);
        }
    }

    wl_user_draw_text(buf, 18, CALC_H - 22, "Mouse or keyboard input", C_MUTED);
}

void _start(void) {
    print("[APP_DBG] calculator start\n");
    i32 sid = wl_create_surface("Calculator", 690, 70, CALC_W, CALC_H);
    if (sid < 0) { print("[APP_DBG] calculator create failed\n"); exit(1); }

    void *buffer_vaddr = (void *)0x10000000;
    if (wl_map_buffer(sid, buffer_vaddr) < 0) { print("[APP_DBG] calculator map failed\n"); exit(2); }
    print("[APP_DBG] calculator mapped buffer\n");

    wl_user_buffer_t buf = {
        .pixels = (u32 *)buffer_vaddr,
        .width = CALC_W,
        .height = CALC_H
    };

    draw_calculator(&buf);
    if (wl_commit(sid) < 0) exit(0);
    print("[APP_DBG] calculator first commit\n");

    i32 last_x = -1, last_y = -1;
    while (1) {
        input_event_t ev;
        while (wl_poll_event(sid, &ev)) {
            if (ev.type == EV_CLOSE) {
                print("[APP_DBG] calculator close requested\n");
                exit(0);
            }
            if (ev.type == EV_ABS && ev.code == 0) last_x = ev.value;
            if (ev.type == EV_ABS && ev.code == 1) last_y = ev.value;
            if (ev.type == EV_KEY && ev.value == KEY_PRESSED) {
                char key = key_to_calc(ev.code);
                if (key) {
                    calc_press(key);
                } else if (last_x >= 0 && last_y >= 0) {
                    calc_handle_click(last_x, last_y);
                }
                draw_calculator(&buf);
                if (wl_commit(sid) < 0) {
                    print("[APP_DBG] calculator commit failed; exiting\n");
                    exit(0);
                }
            }
        }
        sleep_ms(16);
    }
}
