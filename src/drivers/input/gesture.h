/* YamKernel — Gesture Recognition Engine v0.3.0 */
#ifndef _DRIVERS_INPUT_GESTURE_H
#define _DRIVERS_INPUT_GESTURE_H
#include <nexus/types.h>

typedef enum {
    GESTURE_NONE        = 0,
    GESTURE_TAP         = 1,
    GESTURE_DOUBLE_TAP  = 2,
    GESTURE_LONG_PRESS  = 3,
    GESTURE_SWIPE_UP    = 4,
    GESTURE_SWIPE_DOWN  = 5,
    GESTURE_SWIPE_LEFT  = 6,
    GESTURE_SWIPE_RIGHT = 7,
    GESTURE_PINCH_IN    = 8,
    GESTURE_PINCH_OUT   = 9,
    GESTURE_ROTATE      = 10,
    GESTURE_TWO_FINGER_SCROLL = 11,
} gesture_type_t;

typedef enum {
    GSTATE_IDLE,
    GSTATE_POSSIBLE,
    GSTATE_RECOGNIZED,
    GSTATE_COMPLETED,
} gesture_state_t;

typedef struct {
    gesture_type_t  type;
    gesture_state_t state;
    i32             x, y;           /* Center position */
    i32             dx, dy;         /* Delta (for swipe/scroll) */
    i32             scale;          /* For pinch (fixed-point, 256 = 1.0x) */
    i32             angle;          /* For rotate (degrees * 100) */
    u32             finger_count;
} gesture_event_t;

/* Configuration */
typedef struct {
    u32  tap_timeout_ms;       /* Max duration for a tap (default: 200) */
    u32  double_tap_gap_ms;    /* Max gap between two taps (default: 300) */
    u32  long_press_ms;        /* Min hold time (default: 500) */
    i32  swipe_min_px;         /* Min distance for swipe (default: 50) */
    u32  swipe_max_ms;         /* Max time for swipe (default: 400) */
    i32  pinch_min_px;         /* Min distance change for pinch (default: 30) */
    i32  rotate_min_deg;       /* Min angle change for rotate (default: 15) */
} gesture_config_t;

void gesture_init(void);
void gesture_set_config(const gesture_config_t *cfg);
void gesture_process_frame(void);   /* Called after touch_report_sync */
bool gesture_pop_event(gesture_event_t *out);

#endif
