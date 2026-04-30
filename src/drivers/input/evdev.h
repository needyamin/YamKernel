/* ============================================================================
 * YamKernel — Evdev Input Subsystem
 * Translates raw PS/2 keyboard and mouse interrupts into Linux-compatible
 * struct input_event packets for the Wayland compositor.
 * ============================================================================ */
#ifndef _DRIVERS_INPUT_EVDEV_H
#define _DRIVERS_INPUT_EVDEV_H

#include <nexus/types.h>

/* Event types (subset of Linux input.h) */
#define EV_SYN      0x00
#define EV_KEY      0x01
#define EV_REL      0x02
#define EV_ABS      0x03

/* Relative axes */
#define REL_X       0x00
#define REL_Y       0x01
#define REL_WHEEL   0x08

/* Standard Mouse Buttons */
#define BTN_LEFT    0x110
#define BTN_RIGHT   0x111
#define BTN_MIDDLE  0x112

/* Key states */
#define KEY_RELEASED    0
#define KEY_PRESSED     1

/* Maximum events in ring buffer */
#define EVDEV_RING_SIZE 256

typedef struct input_event {
    u16  type;      /* EV_KEY, EV_REL, EV_ABS, EV_SYN */
    u16  code;      /* Keycode or axis */
    i32  value;     /* Key state or axis delta */
} input_event_t;

/* Initialize evdev subsystem */
void evdev_init(void);

/* Push an event from an ISR (called by keyboard/mouse drivers) */
void evdev_push_event(u16 type, u16 code, i32 value);

/* Pop an event (returns false if empty) */
bool evdev_pop_event(input_event_t *out);

/* Check if events are pending */
bool evdev_has_events(void);

#endif /* _DRIVERS_INPUT_EVDEV_H */
