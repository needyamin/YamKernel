/* ============================================================================
 * YamKernel — USB HID Class Driver
 * Handles keyboard, mouse, and touchscreen via HID boot protocol.
 * ============================================================================ */
#pragma once
#include <nexus/types.h>
#include "xhci.h"

/* HID Protocol values (from SET_PROTOCOL) */
#define HID_PROTO_BOOT   0
#define HID_PROTO_REPORT 1

/* HID Subclass */
#define HID_SUBCLASS_BOOT 1

/* HID Boot Interface Protocol */
#define HID_BOOT_KEYBOARD 1
#define HID_BOOT_MOUSE    2

/* Initialize HID for a discovered interface */
void hid_probe(xhci_ctrl_t *ctrl, int slot, u8 protocol);

/* Poll HID device (called periodically by scheduler) */
void hid_poll_all(void);
