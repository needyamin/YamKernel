#include "yam.h"
#include "wl_draw_user.h"

/* The font data must be linked or included */
extern const uint8_t font_basic_8x16[128][16];

static char display_buf[32] = "0";
static i32 current_val = 0;
static i32 accumulator = 0;
static char current_op = 0;
static bool new_input = true;

static void calc_push_digit(int d) {
    if (new_input) {
        current_val = d;
        new_input = false;
    } else {
        current_val = current_val * 10 + d;
    }
    utoa(display_buf, current_val);
}

static void calc_execute_op(void) {
    if (current_op == '+') accumulator += current_val;
    else if (current_op == '-') accumulator -= current_val;
    else if (current_op == '*') accumulator *= current_val;
    else if (current_op == '/') { if (current_val != 0) accumulator /= current_val; }
    else accumulator = current_val;
    current_val = accumulator;
    utoa(display_buf, current_val);
    new_input = true;
}

static void calc_handle_click(i32 x, i32 y) {
    if (y < 40) return;
    int bx = x / 60;
    int by = (y - 40) / 80;
    if (bx < 0 || bx > 3 || by < 0 || by > 3) return;
    
    char btn[4][4] = {
        {'7', '8', '9', '/'},
        {'4', '5', '6', '*'},
        {'1', '2', '3', '-'},
        {'C', '0', '=', '+'}
    };
    char c = btn[by][bx];
    if (c >= '0' && c <= '9') {
        calc_push_digit(c - '0');
    } else if (c == 'C') {
        current_val = 0;
        accumulator = 0;
        current_op = 0;
        new_input = true;
        display_buf[0] = '0'; display_buf[1] = 0;
    } else if (c == '=') {
        calc_execute_op();
        current_op = 0;
    } else {
        calc_execute_op();
        current_op = c;
    }
}

static void draw_calculator(wl_user_buffer_t *buf) {
    /* Dracula Theme Background */
    wl_user_draw_rect(buf, 0, 0, 240, 360, 0xFF282A36);
    
    /* Display area */
    wl_user_draw_rect(buf, 10, 10, 220, 50, 0xFF44475A);
    wl_user_draw_rect(buf, 10, 59, 220, 1, 0xFF6272A4);
    
    int len = strlen(display_buf);
    wl_user_draw_text(buf, 230 - (len * 8) - 10, 26, display_buf, 0xFFF8F8F2);
    
    char btn[4][4] = {
        {'7', '8', '9', '/'},
        {'4', '5', '6', '*'},
        {'1', '2', '3', '-'},
        {'C', '0', '=', '+'}
    };
    
    for (int by = 0; by < 4; by++) {
        for (int bx = 0; bx < 4; bx++) {
            i32 px = 10 + bx * 58;
            i32 py = 75 + by * 70;
            u32 color = 0xFF44475A;
            u32 text_color = 0xFFF8F8F2;
            if (bx == 3) color = 0xFFBD93F9;
            if (btn[by][bx] == '=') color = 0xFF50FA7B;
            if (btn[by][bx] == 'C') color = 0xFFFF5555;
            if (color == 0xFF50FA7B || color == 0xFFFF5555 || color == 0xFFBD93F9)
                text_color = 0xFF282A36;
            wl_user_draw_rect(buf, px, py, 50, 60, color);
            char str[2] = { btn[by][bx], 0 };
            wl_user_draw_text(buf, px + 21, py + 22, str, text_color);
        }
    }
}

void _start(void) {
    i32 sid = wl_create_surface("Calculator", 650, 50, 240, 360);
    if (sid < 0) exit(1);

    /* Use a fixed virtual address for the buffer mapping in userspace */
    void *buffer_vaddr = (void *)0x10000000;
    if (wl_map_buffer(sid, buffer_vaddr) < 0) exit(2);

    wl_user_buffer_t buf = {
        .pixels = (u32 *)buffer_vaddr,
        .width = 240,
        .height = 360
    };

    draw_calculator(&buf);
    wl_commit(sid);

    i32 last_x = -1, last_y = -1;
    while (1) {
        input_event_t ev;
        while (wl_poll_event(sid, &ev)) {
            if (ev.type == EV_ABS && ev.code == 0) last_x = ev.value;
            if (ev.type == EV_ABS && ev.code == 1) last_y = ev.value;
            if (ev.type == EV_KEY && ev.value == KEY_PRESSED) {
                if (last_x >= 0 && last_y >= 0) {
                    calc_handle_click(last_x, last_y);
                    draw_calculator(&buf);
                    wl_commit(sid);
                }
            }
        }
        sleep_ms(16);
    }
}
