/* YamKernel — Gesture Recognition Engine v0.3.0
 * State machine processing touch slot history */
#include "gesture.h"
#include "touch.h"
#include "evdev.h"
#include "../../cpu/percpu.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"
#include "../../lib/spinlock.h"

#define EV_GESTURE 0x06  /* Custom event type for gestures */

#define GESTURE_RING_SIZE 32
static gesture_event_t g_ring[GESTURE_RING_SIZE];
static volatile u32 g_head = 0, g_tail = 0;
static spinlock_t gesture_lock = SPINLOCK_INIT;

/* Configuration */
static gesture_config_t g_cfg = {
    .tap_timeout_ms    = 200,
    .double_tap_gap_ms = 300,
    .long_press_ms     = 500,
    .swipe_min_px      = 50,
    .swipe_max_ms      = 400,
    .pinch_min_px      = 30,
    .rotate_min_deg    = 15,
};

/* State tracking */
typedef struct {
    bool     down;
    u64      down_tick;
    i32      start_x, start_y;
    i32      last_x, last_y;
} finger_state_t;

static finger_state_t fingers[10];
static gesture_state_t g_state = GSTATE_IDLE;
static u64 last_tap_tick = 0;
static i32 last_tap_x = 0, last_tap_y = 0;

static u64 ms_to_ticks(u32 ms) { return (ms + 9) / 10; }

static i32 abs_i32(i32 x) { return x < 0 ? -x : x; }

/* Integer square root approximation */
static i32 isqrt(i32 v) {
    if (v <= 0) return 0;
    i32 x = v, y = (x + 1) / 2;
    while (y < x) { x = y; y = (v / x + x) / 2; }
    return x;
}

static void push_gesture(gesture_type_t type, i32 x, i32 y, i32 dx, i32 dy,
                         i32 scale, i32 angle, u32 fingers_n) {
    u64 f = spin_lock_irqsave(&gesture_lock);
    u32 next = (g_head + 1) % GESTURE_RING_SIZE;
    if (next != g_tail) {
        g_ring[g_head].type = type;
        g_ring[g_head].state = GSTATE_RECOGNIZED;
        g_ring[g_head].x = x;
        g_ring[g_head].y = y;
        g_ring[g_head].dx = dx;
        g_ring[g_head].dy = dy;
        g_ring[g_head].scale = scale;
        g_ring[g_head].angle = angle;
        g_ring[g_head].finger_count = fingers_n;
        g_head = next;
    }
    spin_unlock_irqrestore(&gesture_lock, f);

    /* Also push to evdev for compositor */
    evdev_push_event(EV_GESTURE, (u16)type, (i32)fingers_n);
}

void gesture_init(void) {
    memset(fingers, 0, sizeof(fingers));
    g_state = GSTATE_IDLE;
    g_head = g_tail = 0;
    kprintf_color(0xFF00FF88, "[GESTURE] Recognition engine initialized\n");
}

void gesture_set_config(const gesture_config_t *cfg) {
    if (cfg) g_cfg = *cfg;
}

void gesture_process_frame(void) {
    u64 now = this_cpu()->ticks;
    u32 active = touch_active_count();

    /* Update finger states from touch slots */
    for (int i = 0; i < 10; i++) {
        const touch_slot_t *slot = touch_get_slot((u8)i);
        if (!slot) continue;

        if (slot->active && !fingers[i].down) {
            /* Finger down */
            fingers[i].down = true;
            fingers[i].down_tick = now;
            fingers[i].start_x = slot->x;
            fingers[i].start_y = slot->y;
            fingers[i].last_x = slot->x;
            fingers[i].last_y = slot->y;
        } else if (slot->active && fingers[i].down) {
            /* Finger move */
            fingers[i].last_x = slot->x;
            fingers[i].last_y = slot->y;
        } else if (!slot->active && fingers[i].down) {
            /* Finger up */
            u64 duration = now - fingers[i].down_tick;
            i32 dx = fingers[i].last_x - fingers[i].start_x;
            i32 dy = fingers[i].last_y - fingers[i].start_y;
            i32 dist = isqrt(dx * dx + dy * dy);

            if (active <= 1) {
                /* Single finger gestures */
                if (duration <= ms_to_ticks(g_cfg.tap_timeout_ms) && dist < 10) {
                    /* Check for double tap */
                    if (now - last_tap_tick <= ms_to_ticks(g_cfg.double_tap_gap_ms) &&
                        abs_i32(fingers[i].start_x - last_tap_x) < 30 &&
                        abs_i32(fingers[i].start_y - last_tap_y) < 30) {
                        push_gesture(GESTURE_DOUBLE_TAP, fingers[i].start_x,
                                     fingers[i].start_y, 0, 0, 256, 0, 1);
                        last_tap_tick = 0;
                    } else {
                        push_gesture(GESTURE_TAP, fingers[i].start_x,
                                     fingers[i].start_y, 0, 0, 256, 0, 1);
                        last_tap_tick = now;
                        last_tap_x = fingers[i].start_x;
                        last_tap_y = fingers[i].start_y;
                    }
                } else if (duration >= ms_to_ticks(g_cfg.long_press_ms) && dist < 10) {
                    push_gesture(GESTURE_LONG_PRESS, fingers[i].start_x,
                                 fingers[i].start_y, 0, 0, 256, 0, 1);
                } else if (dist >= g_cfg.swipe_min_px &&
                           duration <= ms_to_ticks(g_cfg.swipe_max_ms)) {
                    /* Determine swipe direction */
                    gesture_type_t swipe;
                    if (abs_i32(dx) > abs_i32(dy)) {
                        swipe = (dx > 0) ? GESTURE_SWIPE_RIGHT : GESTURE_SWIPE_LEFT;
                    } else {
                        swipe = (dy > 0) ? GESTURE_SWIPE_DOWN : GESTURE_SWIPE_UP;
                    }
                    push_gesture(swipe, fingers[i].start_x, fingers[i].start_y,
                                 dx, dy, 256, 0, 1);
                }
            }

            fingers[i].down = false;
        }
    }

    /* Two-finger gestures: pinch and scroll */
    if (active == 2) {
        int f0 = -1, f1 = -1;
        for (int i = 0; i < 10 && f1 < 0; i++) {
            if (fingers[i].down) {
                if (f0 < 0) f0 = i; else f1 = i;
            }
        }
        if (f0 >= 0 && f1 >= 0) {
            i32 cur_dx = fingers[f1].last_x - fingers[f0].last_x;
            i32 cur_dy = fingers[f1].last_y - fingers[f0].last_y;
            i32 start_dx = fingers[f1].start_x - fingers[f0].start_x;
            i32 start_dy = fingers[f1].start_y - fingers[f0].start_y;
            i32 cur_dist = isqrt(cur_dx * cur_dx + cur_dy * cur_dy);
            i32 start_dist = isqrt(start_dx * start_dx + start_dy * start_dy);

            if (start_dist > 0) {
                i32 dist_change = cur_dist - start_dist;
                if (abs_i32(dist_change) > g_cfg.pinch_min_px) {
                    i32 scale = (cur_dist * 256) / start_dist;
                    gesture_type_t pinch = (dist_change > 0) ? GESTURE_PINCH_OUT : GESTURE_PINCH_IN;
                    i32 cx = (fingers[f0].last_x + fingers[f1].last_x) / 2;
                    i32 cy = (fingers[f0].last_y + fingers[f1].last_y) / 2;
                    push_gesture(pinch, cx, cy, 0, 0, scale, 0, 2);
                }
            }

            /* Two-finger scroll */
            i32 avg_dy = ((fingers[f0].last_y - fingers[f0].start_y) +
                          (fingers[f1].last_y - fingers[f1].start_y)) / 2;
            i32 avg_dx = ((fingers[f0].last_x - fingers[f0].start_x) +
                          (fingers[f1].last_x - fingers[f1].start_x)) / 2;
            if (abs_i32(avg_dy) > 20 || abs_i32(avg_dx) > 20) {
                i32 cx = (fingers[f0].last_x + fingers[f1].last_x) / 2;
                i32 cy = (fingers[f0].last_y + fingers[f1].last_y) / 2;
                push_gesture(GESTURE_TWO_FINGER_SCROLL, cx, cy, avg_dx, avg_dy, 256, 0, 2);
            }
        }
    }
}

bool gesture_pop_event(gesture_event_t *out) {
    u64 f = spin_lock_irqsave(&gesture_lock);
    if (g_tail == g_head) {
        spin_unlock_irqrestore(&gesture_lock, f);
        return false;
    }
    *out = g_ring[g_tail];
    g_tail = (g_tail + 1) % GESTURE_RING_SIZE;
    spin_unlock_irqrestore(&gesture_lock, f);
    return true;
}
