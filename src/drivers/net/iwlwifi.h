#ifndef _DRIVERS_NET_IWLWIFI_H
#define _DRIVERS_NET_IWLWIFI_H

#include <nexus/types.h>

typedef enum {
    IWLWIFI_CONN_DISCONNECTED = 0,
    IWLWIFI_CONN_SCANNING,
    IWLWIFI_CONN_CONNECTED,
    IWLWIFI_CONN_BLOCKED,
} iwlwifi_connection_state_t;

typedef enum {
    IWLWIFI_OP_OK = 0,
    IWLWIFI_OP_RADIO_OFF,
    IWLWIFI_OP_NO_ADAPTER,
    IWLWIFI_OP_NO_FIRMWARE,
} iwlwifi_op_result_t;

typedef struct {
    bool initialized;
    bool present;
    bool radio_enabled;
    bool firmware_loaded;
    bool scanning;
    iwlwifi_connection_state_t connection;
    char device_name[48];
    char ssid[32];
    char last_error[96];
    u8 signal_percent;
} iwlwifi_status_t;

void iwlwifi_init(void);
const iwlwifi_status_t *iwlwifi_get_status(void);
iwlwifi_op_result_t iwlwifi_set_radio_enabled(bool enabled);
iwlwifi_op_result_t iwlwifi_scan(void);
iwlwifi_op_result_t iwlwifi_connect_default(void);
const char *iwlwifi_result_string(iwlwifi_op_result_t result);

#endif
