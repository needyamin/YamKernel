/* YamKernel — Multi-Touch Input Subsystem v0.3.0
 * Slot management, calibration, palm rejection, evdev integration */
#include "touch.h"
#include "evdev.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"
#include "../../lib/spinlock.h"

/* ABS codes for MT (Linux-compatible) */
#define ABS_MT_SLOT          0x2F
#define ABS_MT_TOUCH_MAJOR   0x30
#define ABS_MT_TOUCH_MINOR   0x31
#define ABS_MT_ORIENTATION   0x34
#define ABS_MT_POSITION_X    0x35
#define ABS_MT_POSITION_Y    0x36
#define ABS_MT_TRACKING_ID   0x39
#define ABS_MT_PRESSURE      0x3A

static touch_slot_t slots[TOUCH_MAX_SLOTS];
static touch_slot_t prev_slots[TOUCH_MAX_SLOTS]; /* Previous frame for delta */
static touch_calibration_t calibration;
static i32 palm_threshold = 200; /* Major axis > this = palm */
static spinlock_t touch_lock = SPINLOCK_INIT;
static u32 active_slots = 0;

/* Identity calibration matrix (no transform) */
static void set_identity_cal(void) {
    calibration.a[0] = 65536; /* a00 = 1.0 */
    calibration.a[1] = 0;     /* a01 = 0 */
    calibration.a[2] = 0;     /* a02 = 0 (x offset) */
    calibration.a[3] = 0;     /* a10 = 0 */
    calibration.a[4] = 65536; /* a11 = 1.0 */
    calibration.a[5] = 0;     /* a12 = 0 (y offset) */
}

static void apply_calibration(i32 raw_x, i32 raw_y, i32 *cal_x, i32 *cal_y) {
    /* Affine transform: x' = a00*x + a01*y + a02, y' = a10*x + a11*y + a12 */
    *cal_x = (calibration.a[0] * raw_x + calibration.a[1] * raw_y + calibration.a[2]) >> 16;
    *cal_y = (calibration.a[3] * raw_x + calibration.a[4] * raw_y + calibration.a[5]) >> 16;
}

void touch_init(void) {
    memset(slots, 0, sizeof(slots));
    memset(prev_slots, 0, sizeof(prev_slots));
    for (int i = 0; i < TOUCH_MAX_SLOTS; i++) {
        slots[i].tracking_id = -1;
        prev_slots[i].tracking_id = -1;
    }
    set_identity_cal();
    active_slots = 0;
    kprintf_color(0xFF00FF88, "[TOUCH] Multi-touch subsystem initialized (%d slots)\n",
                  TOUCH_MAX_SLOTS);
}

void touch_set_calibration(const touch_calibration_t *cal) {
    if (cal) {
        for (int i = 0; i < TOUCH_MATRIX_SIZE; i++)
            calibration.a[i] = cal->a[i];
        kprintf("[TOUCH] Calibration matrix updated\n");
    }
}

void touch_set_palm_threshold(i32 major_threshold) {
    palm_threshold = major_threshold;
}

const touch_slot_t *touch_get_slot(u8 slot) {
    if (slot >= TOUCH_MAX_SLOTS) return NULL;
    return &slots[slot];
}

u32 touch_active_count(void) { return active_slots; }

void touch_report_slot(u8 slot, i32 tracking_id, i32 raw_x, i32 raw_y,
                       i32 pressure, i32 major, i32 minor) {
    if (slot >= TOUCH_MAX_SLOTS) return;

    u64 f = spin_lock_irqsave(&touch_lock);

    /* Palm rejection: ignore contacts with large major axis */
    if (major > palm_threshold && palm_threshold > 0) {
        /* Treat as palm — ignore */
        spin_unlock_irqrestore(&touch_lock, f);
        return;
    }

    /* Apply calibration */
    i32 cal_x, cal_y;
    apply_calibration(raw_x, raw_y, &cal_x, &cal_y);

    slots[slot].tracking_id = tracking_id;
    slots[slot].x = cal_x;
    slots[slot].y = cal_y;
    slots[slot].pressure = pressure;
    slots[slot].touch_major = major;
    slots[slot].touch_minor = minor;
    slots[slot].active = (tracking_id >= 0);

    spin_unlock_irqrestore(&touch_lock, f);
}

void touch_report_sync(void) {
    u64 f = spin_lock_irqsave(&touch_lock);
    active_slots = 0;

    for (int i = 0; i < TOUCH_MAX_SLOTS; i++) {
        bool was_active = prev_slots[i].active;
        bool is_active = slots[i].active;

        if (is_active) active_slots++;

        if (!was_active && is_active) {
            /* TOUCH_DOWN */
            evdev_push_event(EV_ABS, ABS_MT_SLOT, i);
            evdev_push_event(EV_ABS, ABS_MT_TRACKING_ID, slots[i].tracking_id);
            evdev_push_event(EV_ABS, ABS_MT_POSITION_X, slots[i].x);
            evdev_push_event(EV_ABS, ABS_MT_POSITION_Y, slots[i].y);
            evdev_push_event(EV_ABS, ABS_MT_PRESSURE, slots[i].pressure);
            evdev_push_event(EV_ABS, ABS_MT_TOUCH_MAJOR, slots[i].touch_major);
        } else if (was_active && !is_active) {
            /* TOUCH_UP */
            evdev_push_event(EV_ABS, ABS_MT_SLOT, i);
            evdev_push_event(EV_ABS, ABS_MT_TRACKING_ID, -1);
        } else if (is_active) {
            /* TOUCH_MOVE (only if position changed) */
            if (slots[i].x != prev_slots[i].x || slots[i].y != prev_slots[i].y) {
                evdev_push_event(EV_ABS, ABS_MT_SLOT, i);
                evdev_push_event(EV_ABS, ABS_MT_POSITION_X, slots[i].x);
                evdev_push_event(EV_ABS, ABS_MT_POSITION_Y, slots[i].y);
                if (slots[i].pressure != prev_slots[i].pressure) {
                    evdev_push_event(EV_ABS, ABS_MT_PRESSURE, slots[i].pressure);
                }
            }
        }

        prev_slots[i] = slots[i];
    }

    /* SYN_REPORT marks end of frame */
    evdev_push_event(EV_SYN, 0, 0);
    spin_unlock_irqrestore(&touch_lock, f);
}
