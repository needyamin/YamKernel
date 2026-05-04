/* ============================================================================
 * YamKernel - Intel Wireless Wi-Fi status foundation
 * ============================================================================ */
#include "iwlwifi.h"
#include "../bus/pci.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"

static iwlwifi_status_t g_iwlwifi_status = {
    .initialized = false,
    .present = false,
    .radio_enabled = false,
    .firmware_loaded = false,
    .scanning = false,
    .connection = IWLWIFI_CONN_DISCONNECTED,
    .device_name = "No Wi-Fi adapter",
    .ssid = "",
    .last_error = "Not initialized",
    .signal_percent = 0,
};

static iwlwifi_op_result_t iwlwifi_blocker(void) {
    if (!g_iwlwifi_status.radio_enabled) return IWLWIFI_OP_RADIO_OFF;
    if (!g_iwlwifi_status.present) return IWLWIFI_OP_NO_ADAPTER;
    if (!g_iwlwifi_status.firmware_loaded) return IWLWIFI_OP_NO_FIRMWARE;
    return IWLWIFI_OP_OK;
}

const char *iwlwifi_result_string(iwlwifi_op_result_t result) {
    switch (result) {
        case IWLWIFI_OP_OK: return "OK";
        case IWLWIFI_OP_RADIO_OFF: return "Wi-Fi radio is off";
        case IWLWIFI_OP_NO_ADAPTER: return "No supported Wi-Fi adapter detected";
        case IWLWIFI_OP_NO_FIRMWARE: return "Intel firmware and 802.11 MAC layer are pending";
    }
    return "Unknown Wi-Fi error";
}

static void iwlwifi_set_error(iwlwifi_op_result_t result) {
    strncpy(g_iwlwifi_status.last_error, iwlwifi_result_string(result),
            sizeof(g_iwlwifi_status.last_error) - 1);
    g_iwlwifi_status.last_error[sizeof(g_iwlwifi_status.last_error) - 1] = '\0';
}

void iwlwifi_init(void) {
    g_iwlwifi_status.initialized = true;

    /* Common Intel wireless IDs: 0x0082 (Centrino 6205), 0x4232 (5100). */
    pci_device_t *wifi = pci_get_device(0x8086, 0x0082);
    if (!wifi) wifi = pci_get_device(0x8086, 0x4232);

    if (wifi) {
        g_iwlwifi_status.present = true;
        g_iwlwifi_status.radio_enabled = true;
        g_iwlwifi_status.firmware_loaded = false;
        g_iwlwifi_status.connection = IWLWIFI_CONN_BLOCKED;
        strcpy(g_iwlwifi_status.device_name, "Intel Wireless NIC");
        iwlwifi_set_error(IWLWIFI_OP_NO_FIRMWARE);
        kprintf_color(0xFF00FF88, "[wlan0] Intel Wireless NIC detected at %02x:%02x.%u\n",
                      wifi->bus, wifi->slot, wifi->func);
        kprintf_color(0xFFFFD166, "[wlan0] Firmware loader/MAC layer pending; Wi-Fi connection blocked\n");
    } else {
        g_iwlwifi_status.present = false;
        g_iwlwifi_status.radio_enabled = false;
        g_iwlwifi_status.firmware_loaded = false;
        g_iwlwifi_status.connection = IWLWIFI_CONN_BLOCKED;
        strcpy(g_iwlwifi_status.device_name, "No Wi-Fi adapter");
        iwlwifi_set_error(IWLWIFI_OP_NO_ADAPTER);
        kprintf_color(0xFF888888, "[wlan0] No Intel wireless NIC present (firmware blob required)\n");
    }
}

const iwlwifi_status_t *iwlwifi_get_status(void) {
    return &g_iwlwifi_status;
}

iwlwifi_op_result_t iwlwifi_set_radio_enabled(bool enabled) {
    g_iwlwifi_status.radio_enabled = enabled;
    g_iwlwifi_status.scanning = false;
    if (!enabled) {
        g_iwlwifi_status.connection = IWLWIFI_CONN_DISCONNECTED;
        g_iwlwifi_status.ssid[0] = '\0';
        g_iwlwifi_status.signal_percent = 0;
        iwlwifi_set_error(IWLWIFI_OP_RADIO_OFF);
        kprintf("[wlan0] radio disabled by desktop settings\n");
        return IWLWIFI_OP_OK;
    }

    iwlwifi_op_result_t blocker = iwlwifi_blocker();
    if (blocker != IWLWIFI_OP_OK) {
        g_iwlwifi_status.connection = IWLWIFI_CONN_BLOCKED;
        iwlwifi_set_error(blocker);
        kprintf("[wlan0] radio enabled, but connection is blocked: adapter=%u firmware=%u\n",
                g_iwlwifi_status.present ? 1 : 0,
                g_iwlwifi_status.firmware_loaded ? 1 : 0);
        return blocker;
    }

    g_iwlwifi_status.connection = IWLWIFI_CONN_DISCONNECTED;
    iwlwifi_set_error(IWLWIFI_OP_OK);
    kprintf("[wlan0] radio enabled\n");
    return IWLWIFI_OP_OK;
}

iwlwifi_op_result_t iwlwifi_scan(void) {
    iwlwifi_op_result_t blocker = iwlwifi_blocker();
    if (blocker != IWLWIFI_OP_OK) {
        g_iwlwifi_status.connection = IWLWIFI_CONN_BLOCKED;
        iwlwifi_set_error(blocker);
        kprintf("[wlan0] scan blocked: %s\n", iwlwifi_result_string(blocker));
        return blocker;
    }
    g_iwlwifi_status.scanning = true;
    g_iwlwifi_status.connection = IWLWIFI_CONN_SCANNING;
    iwlwifi_set_error(IWLWIFI_OP_OK);
    kprintf("[wlan0] scan started\n");
    return IWLWIFI_OP_OK;
}

iwlwifi_op_result_t iwlwifi_connect_default(void) {
    iwlwifi_op_result_t blocker = iwlwifi_blocker();
    if (blocker != IWLWIFI_OP_OK) {
        g_iwlwifi_status.connection = IWLWIFI_CONN_BLOCKED;
        iwlwifi_set_error(blocker);
        kprintf("[wlan0] connect blocked: %s\n", iwlwifi_result_string(blocker));
        return blocker;
    }
    g_iwlwifi_status.scanning = false;
    g_iwlwifi_status.connection = IWLWIFI_CONN_CONNECTED;
    strcpy(g_iwlwifi_status.ssid, "YamNet");
    g_iwlwifi_status.signal_percent = 82;
    iwlwifi_set_error(IWLWIFI_OP_OK);
    kprintf("[wlan0] connected ssid='%s' signal=%u\n",
            g_iwlwifi_status.ssid, g_iwlwifi_status.signal_percent);
    return IWLWIFI_OP_OK;
}
