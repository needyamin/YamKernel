/* ============================================================================
 * YamKernel — Wayland Client: Calculator
 * ============================================================================ */
#include <nexus/types.h>
#include "../sched/sched.h"
#include "../sched/wait.h"
#include "compositor.h"
#include "wl_draw.h"
#include "../lib/kprintf.h"

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
    ksnprintf(display_buf, sizeof(display_buf), "%d", current_val);
}

static void calc_execute_op(void) {
    if (current_op == '+') accumulator += current_val;
    else if (current_op == '-') accumulator -= current_val;
    else if (current_op == '*') accumulator *= current_val;
    else if (current_op == '/') { if (current_val != 0) accumulator /= current_val; }
    else accumulator = current_val;
    current_val = accumulator;
    ksnprintf(display_buf, sizeof(display_buf), "%d", current_val);
    new_input = true;
}

static void calc_handle_click(i32 x, i32 y) {
    /* Button layout: 4x4 grid. Size: 240x320. Buttons are 60x80 */
    /* Wait, surface is 240x320? We will create it at 240x360.
       Display is top 40px. Buttons start at y=40. Each button is 60x80 */
    if (y < 40) return; /* Clicked display */
    
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
        ksnprintf(display_buf, sizeof(display_buf), "0");
    } else if (c == '=') {
        calc_execute_op();
        current_op = 0;
    } else {
        /* Operator */
        calc_execute_op();
        current_op = c;
    }
}

static void draw_calculator(wl_surface_t *s) {
    /* Background */
    wl_draw_rect(s, 0, 0, 240, 360, 0xFF222222);
    
    /* Display area */
    wl_draw_rect(s, 0, 0, 240, 40, 0xFFEEEEEE);
    /* Right align text */
    int len = 0; while(display_buf[len]) len++;
    wl_draw_text(s, 240 - (len * 8) - 10, 12, display_buf, 0xFF000000, 0);
    
    /* Buttons */
    char btn[4][4] = {
        {'7', '8', '9', '/'},
        {'4', '5', '6', '*'},
        {'1', '2', '3', '-'},
        {'C', '0', '=', '+'}
    };
    
    for (int by = 0; by < 4; by++) {
        for (int bx = 0; bx < 4; bx++) {
            i32 px = bx * 60;
            i32 py = 40 + by * 80;
            
            u32 color = (bx == 3) ? 0xFFFF9900 : 0xFF444444; /* Operators are orange */
            if (btn[by][bx] == 'C') color = 0xFFDD3333; /* Clear is red */
            
            wl_draw_rect(s, px + 2, py + 2, 56, 76, color);
            
            /* Draw character centered */
            char str[2] = { btn[by][bx], 0 };
            wl_draw_text(s, px + 26, py + 32, str, 0xFFFFFFFF, 0);
        }
    }
}

void wl_calc_task(void *arg) {
    (void)arg;
    /* Wait for compositor to be ready */
    task_sleep_ms(200);
    
    wl_surface_t *s = wl_surface_create("Calculator", 650, 50, 240, 360, sched_current()->id);
    if (!s) return;
    
    draw_calculator(s);
    wl_surface_commit(s);
    
    i32 last_x = -1, last_y = -1;
    
    while (1) {
        input_event_t ev;
        while (wl_surface_pop_event(s, &ev)) {
            if (ev.type == EV_ABS && ev.code == 0) last_x = ev.value;
            if (ev.type == EV_ABS && ev.code == 1) last_y = ev.value;
            
            if (ev.type == EV_KEY && ev.value == KEY_PRESSED) {
                if (last_x >= 0 && last_y >= 0) {
                    calc_handle_click(last_x, last_y);
                    draw_calculator(s);
                    wl_surface_commit(s);
                }
            }
        }
        
        task_sleep_ms(16);
    }
}
