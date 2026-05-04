/* ============================================================================
 * YamKernel - Bluetooth HCI status foundation
 * ============================================================================ */
#include "hci_usb.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"

static hci_status_t g_hci_status = {
    .initialized = false,
    .controller_present = false,
    .radio_enabled = false,
    .usb_backend_ready = false,
    .scanning = false,
    .connection = HCI_BT_DISCONNECTED,
    .device_name = "No Bluetooth controller",
    .peer_name = "",
    .last_error = "Not initialized",
};

static hci_op_result_t hci_blocker(void) {
    if (!g_hci_status.radio_enabled) return HCI_OP_RADIO_OFF;
    if (!g_hci_status.controller_present) return HCI_OP_NO_CONTROLLER;
    if (!g_hci_status.usb_backend_ready) return HCI_OP_NO_USB_BACKEND;
    return HCI_OP_OK;
}

const char *hci_result_string(hci_op_result_t result) {
    switch (result) {
        case HCI_OP_OK: return "OK";
        case HCI_OP_RADIO_OFF: return "Bluetooth radio is off";
        case HCI_OP_NO_CONTROLLER: return "No Bluetooth controller detected";
        case HCI_OP_NO_USB_BACKEND: return "USB HCI transport/backend is pending";
    }
    return "Unknown Bluetooth error";
}

static void hci_set_error(hci_op_result_t result) {
    strncpy(g_hci_status.last_error, hci_result_string(result),
            sizeof(g_hci_status.last_error) - 1);
    g_hci_status.last_error[sizeof(g_hci_status.last_error) - 1] = '\0';
}

void hci_init(void) {
    g_hci_status.initialized = true;
    g_hci_status.controller_present = false;
    g_hci_status.radio_enabled = false;
    g_hci_status.usb_backend_ready = false;
    g_hci_status.connection = HCI_BT_BLOCKED;
    strcpy(g_hci_status.device_name, "Bluetooth HCI");
    hci_set_error(HCI_OP_NO_CONTROLLER);
    kprintf_color(0xFF00DDFF, "[hci0] Bluetooth HCI service initialized; USB transport/backend pending\n");
}

const hci_status_t *hci_get_status(void) {
    return &g_hci_status;
}

hci_op_result_t hci_set_radio_enabled(bool enabled) {
    g_hci_status.radio_enabled = enabled;
    g_hci_status.scanning = false;
    if (!enabled) {
        g_hci_status.connection = HCI_BT_DISCONNECTED;
        g_hci_status.peer_name[0] = '\0';
        hci_set_error(HCI_OP_RADIO_OFF);
        kprintf("[hci0] radio disabled by desktop settings\n");
        return HCI_OP_OK;
    }

    hci_op_result_t blocker = hci_blocker();
    if (blocker != HCI_OP_OK) {
        g_hci_status.connection = HCI_BT_BLOCKED;
        hci_set_error(blocker);
        kprintf("[hci0] radio enabled, but pairing is blocked: controller=%u usb_backend=%u\n",
                g_hci_status.controller_present ? 1 : 0,
                g_hci_status.usb_backend_ready ? 1 : 0);
        return blocker;
    }
    g_hci_status.connection = HCI_BT_DISCONNECTED;
    hci_set_error(HCI_OP_OK);
    kprintf("[hci0] radio enabled\n");
    return HCI_OP_OK;
}

hci_op_result_t hci_scan(void) {
    hci_op_result_t blocker = hci_blocker();
    if (blocker != HCI_OP_OK) {
        g_hci_status.connection = HCI_BT_BLOCKED;
        hci_set_error(blocker);
        kprintf("[hci0] scan blocked: %s\n", hci_result_string(blocker));
        return blocker;
    }
    g_hci_status.scanning = true;
    g_hci_status.connection = HCI_BT_SCANNING;
    hci_set_error(HCI_OP_OK);
    kprintf("[hci0] inquiry scan started\n");
    return HCI_OP_OK;
}

hci_op_result_t hci_connect_default(void) {
    hci_op_result_t blocker = hci_blocker();
    if (blocker != HCI_OP_OK) {
        g_hci_status.connection = HCI_BT_BLOCKED;
        hci_set_error(blocker);
        kprintf("[hci0] connect blocked: %s\n", hci_result_string(blocker));
        return blocker;
    }
    g_hci_status.scanning = false;
    g_hci_status.connection = HCI_BT_CONNECTED;
    strcpy(g_hci_status.peer_name, "YamDevice");
    hci_set_error(HCI_OP_OK);
    kprintf("[hci0] connected peer='%s'\n", g_hci_status.peer_name);
    return HCI_OP_OK;
}
