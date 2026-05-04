#ifndef _DRIVERS_BLUETOOTH_HCI_USB_H
#define _DRIVERS_BLUETOOTH_HCI_USB_H

#include <nexus/types.h>

typedef enum {
    HCI_BT_DISCONNECTED = 0,
    HCI_BT_SCANNING,
    HCI_BT_CONNECTED,
    HCI_BT_BLOCKED,
} hci_bt_connection_state_t;

typedef enum {
    HCI_OP_OK = 0,
    HCI_OP_RADIO_OFF,
    HCI_OP_NO_CONTROLLER,
    HCI_OP_NO_USB_BACKEND,
} hci_op_result_t;

typedef struct {
    bool initialized;
    bool controller_present;
    bool radio_enabled;
    bool usb_backend_ready;
    bool scanning;
    hci_bt_connection_state_t connection;
    char device_name[48];
    char peer_name[32];
    char last_error[96];
} hci_status_t;

void hci_init(void);
const hci_status_t *hci_get_status(void);
hci_op_result_t hci_set_radio_enabled(bool enabled);
hci_op_result_t hci_scan(void);
hci_op_result_t hci_connect_default(void);
const char *hci_result_string(hci_op_result_t result);

#endif
