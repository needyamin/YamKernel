/* YamKernel — Multi-Touch Input Subsystem v0.3.0
 * Linux MT Protocol B compatible, USB HID / I2C touch support */
#ifndef _DRIVERS_INPUT_TOUCH_H
#define _DRIVERS_INPUT_TOUCH_H
#include <nexus/types.h>

#define TOUCH_MAX_SLOTS     10
#define TOUCH_MATRIX_SIZE   6   /* 2x3 affine transform */

/* Touch event types */
typedef enum {
    TOUCH_DOWN   = 0,
    TOUCH_UP     = 1,
    TOUCH_MOVE   = 2,
    TOUCH_CANCEL = 3,
} touch_event_type_t;

/* Per-finger slot (MT Protocol B) */
typedef struct {
    i32  tracking_id;    /* -1 = inactive, >=0 = active finger */
    i32  x;              /* Calibrated X position */
    i32  y;              /* Calibrated Y position */
    i32  pressure;       /* Contact pressure (0-255) */
    i32  touch_major;    /* Major axis of contact ellipse */
    i32  touch_minor;    /* Minor axis of contact ellipse */
    i32  orientation;    /* Contact angle (-90..+90 degrees) */
    bool active;
} touch_slot_t;

/* Touch device capabilities */
typedef struct {
    u32  max_slots;
    i32  x_min, x_max;
    i32  y_min, y_max;
    i32  pressure_max;
    bool has_pressure;
    bool has_size;
    bool has_orientation;
} touch_caps_t;

/* Calibration matrix (affine 2x3) */
typedef struct {
    i32 a[TOUCH_MATRIX_SIZE]; /* [a00, a01, a02, a10, a11, a12] scaled by 65536 */
} touch_calibration_t;

/* Touch event (pushed to evdev) */
typedef struct {
    touch_event_type_t type;
    u8                 slot;
    i32                x, y;
    i32                pressure;
    i32                tracking_id;
} touch_event_t;

void touch_init(void);
void touch_set_calibration(const touch_calibration_t *cal);
const touch_slot_t *touch_get_slot(u8 slot);
u32  touch_active_count(void);

/* Called by USB HID / I2C drivers when touch data arrives */
void touch_report_slot(u8 slot, i32 tracking_id, i32 raw_x, i32 raw_y,
                       i32 pressure, i32 major, i32 minor);
void touch_report_sync(void);  /* Frame complete — emit events */

/* Palm rejection */
void touch_set_palm_threshold(i32 major_threshold);

#endif
