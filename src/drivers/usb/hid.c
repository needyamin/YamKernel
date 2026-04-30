/* ============================================================================
 * YamKernel — USB HID Class Driver Implementation
 * Translates HID boot protocol reports into evdev events.
 * ============================================================================ */
#include "hid.h"
#include "xhci.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"
#include "../../mem/heap.h"
#include "../../drivers/input/evdev.h"
#include "../../drivers/input/keyboard.h"

/* ---- HID device registry ---- */
#define HID_MAX_DEVICES 8
typedef struct {
    bool         active;
    xhci_ctrl_t *ctrl;
    int          slot;
    u8           protocol; /* HID_BOOT_KEYBOARD or HID_BOOT_MOUSE */
    u8           last_kbd_report[8];
    u8           last_mouse_report[4];
} hid_device_t;

static hid_device_t g_hid_devs[HID_MAX_DEVICES];
static int g_hid_count = 0;

/* ---- HID boot keyboard scancode → evdev keycode mapping ---- */
static const u8 hid_to_ps2[256] = {
    [0x04]=30, [0x05]=48, [0x06]=46, [0x07]=32, [0x08]=18,
    [0x09]=33, [0x0A]=34, [0x0B]=35, [0x0C]=23, [0x0D]=36,
    [0x0E]=37, [0x0F]=38, [0x10]=50, [0x11]=49, [0x12]=24,
    [0x13]=25, [0x14]=16, [0x15]=19, [0x16]=31, [0x17]=20,
    [0x18]=22, [0x19]=47, [0x1A]=17, [0x1B]=45, [0x1C]=21,
    [0x1D]=44,
    [0x1E]=2,  [0x1F]=3,  [0x20]=4,  [0x21]=5,  [0x22]=6,
    [0x23]=7,  [0x24]=8,  [0x25]=9,  [0x26]=10, [0x27]=11,
    [0x28]=28, /* Enter */
    [0x29]=1,  /* Escape */
    [0x2A]=14, /* Backspace */
    [0x2B]=15, /* Tab */
    [0x2C]=57, /* Space */
    [0x3A]=59, [0x3B]=60, [0x3C]=61, [0x3D]=62, [0x3E]=63,
    [0x3F]=64, [0x40]=65, [0x41]=66, [0x42]=67, [0x43]=68,
    [0x44]=87, [0x45]=88, /* F1-F12 */
    [0x50]=75, [0x51]=80, [0x52]=72, [0x4F]=77, /* arrows */
    [0xE0]=29, [0xE1]=42, [0xE2]=56, [0xE4]=97, [0xE5]=54, [0xE6]=100, /* modifiers */
};

static void hid_process_keyboard(hid_device_t *dev, u8 *report) {
    /* HID boot keyboard report: [modifier, reserved, key1..key6] */
    u8 modifier = report[0];
    (void)modifier;

    /* Compare with last report */
    for (int k = 2; k < 8; k++) {
        u8 kc = report[k];
        if (kc == 0) continue;

        /* Check if this key was in last report */
        bool was_down = false;
        for (int j = 2; j < 8; j++) {
            if (dev->last_kbd_report[j] == kc) { was_down = true; break; }
        }
        if (!was_down) {
            u8 ps2 = hid_to_ps2[kc];
            if (ps2) {
                evdev_push_event(EV_KEY, ps2, KEY_PRESSED);
            }
        }
    }
    /* Key releases */
    for (int k = 2; k < 8; k++) {
        u8 kc = dev->last_kbd_report[k];
        if (kc == 0) continue;
        bool still_down = false;
        for (int j = 2; j < 8; j++) {
            if (report[j] == kc) { still_down = true; break; }
        }
        if (!still_down) {
            u8 ps2 = hid_to_ps2[kc];
            if (ps2) {
                evdev_push_event(EV_KEY, ps2, KEY_RELEASED);
            }
        }
    }
    memcpy(dev->last_kbd_report, report, 8);
}

static void hid_process_mouse(hid_device_t *dev, u8 *report) {
    /* HID boot mouse report: [buttons, X, Y, wheel] */
    u8  buttons = report[0];
    i8  dx      = (i8)report[1];
    i8  dy      = (i8)report[2];
    (void)dev;

    if (dx) {
        evdev_push_event(EV_REL, REL_X, dx);
    }
    if (dy) {
        evdev_push_event(EV_REL, REL_Y, dy);
    }
    if (buttons & 1) {
        evdev_push_event(EV_KEY, BTN_LEFT, KEY_PRESSED);
    } else if (dev->last_mouse_report[0] & 1) {
        evdev_push_event(EV_KEY, BTN_LEFT, KEY_RELEASED);
    }
    if (buttons & 2) {
        evdev_push_event(EV_KEY, BTN_RIGHT, KEY_PRESSED);
    } else if (dev->last_mouse_report[0] & 2) {
        evdev_push_event(EV_KEY, BTN_RIGHT, KEY_RELEASED);
    }
    
    dev->last_mouse_report[0] = buttons;
}

void hid_probe(xhci_ctrl_t *ctrl, int slot, u8 protocol) {
    if (g_hid_count >= HID_MAX_DEVICES) return;
    hid_device_t *dev = &g_hid_devs[g_hid_count++];
    memset(dev, 0, sizeof(*dev));
    dev->active   = true;
    dev->ctrl     = ctrl;
    dev->slot     = slot;
    dev->protocol = protocol;

    /* Set HID boot protocol */
    xhci_control_transfer(ctrl, slot,
        0x21, USB_REQ_SET_PROTOCOL, HID_PROTO_BOOT, 0, 0, NULL);
    /* Set idle rate to 0 (only report on change) */
    xhci_control_transfer(ctrl, slot,
        0x21, USB_REQ_SET_IDLE, 0, 0, 0, NULL);

    kprintf_color(0xFF00FF88, "[HID] Registered %s (slot %d)\n",
                  protocol == HID_BOOT_KEYBOARD ? "keyboard" : "mouse/touch", slot);
}

void hid_poll_all(void) {
    for (int i = 0; i < g_hid_count; i++) {
        hid_device_t *dev = &g_hid_devs[i];
        if (!dev->active) continue;

        u8 report[8] = {0};
        /* GET_REPORT (0x01 = Input report) */
        bool ok = xhci_control_transfer(dev->ctrl, dev->slot,
            0xA1, 0x01, 0x0100, 0, 8, report);
        if (!ok) continue;

        if (dev->protocol == HID_BOOT_KEYBOARD) hid_process_keyboard(dev, report);
        else                                    hid_process_mouse(dev, report);
    }
}
