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
    /* Dracula Theme Background */
    wl_draw_rect(s, 0, 0, 240, 360, 0xFF282A36);
    
    /* Display area (Glass effect) */
    wl_draw_rect(s, 10, 10, 220, 50, 0xFF44475A);
    wl_draw_rect(s, 10, 59, 220, 1, 0xFF6272A4); /* Underline highlight */
    
    /* Right align text in display */
    int len = 0; while(display_buf[len]) len++;
    wl_draw_text(s, 230 - (len * 8) - 10, 26, display_buf, 0xFFF8F8F2, 0);
    
    /* Buttons */
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
            
            u32 color = 0xFF44475A; /* Default button */
            u32 text_color = 0xFFF8F8F2;
            
            if (bx == 3) color = 0xFFBD93F9; /* Operators are purple */
            if (btn[by][bx] == '=') color = 0xFF50FA7B; /* Equals is green */
            if (btn[by][bx] == 'C') color = 0xFFFF5555; /* Clear is red */
            
            if (color == 0xFF50FA7B || color == 0xFFFF5555 || color == 0xFFBD93F9)
                text_color = 0xFF282A36; /* Dark text for bright buttons */
                
            /* Draw rounded-ish rect (just a smaller rect with padding) */
            wl_draw_rect(s, px, py, 50, 60, color);
            
            /* Draw character centered */
            char str[2] = { btn[by][bx], 0 };
            wl_draw_text(s, px + 21, py + 22, str, text_color, 0);
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
    
    u32 my_id = s->id;
    
    while (s->state == WL_SURFACE_ACTIVE && s->id == my_id) {
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
