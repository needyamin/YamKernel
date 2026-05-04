/* YamKernel - compositor-native Wi-Fi and Bluetooth settings windows */
#include <nexus/types.h>
#include "sched/sched.h"
#include "sched/wait.h"
#include "compositor.h"
#include "wl_draw.h"
#include "drivers/net/iwlwifi.h"
#include "drivers/bluetooth/hci_usb.h"
#include "drivers/audio/audio.h"
#include "net/net.h"
#include "lib/kprintf.h"
#include "lib/string.h"

#define RADIO_W 560
#define RADIO_H 460

#define COL_BG      0xFFF4F7FB
#define COL_PANEL   0xFFFFFFFF
#define COL_LINE    0xFFD8E0EA
#define COL_TEXT    0xFF172033
#define COL_MUTED   0xFF657186
#define COL_BLUE    0xFF2563EB
#define COL_GREEN   0xFF16A34A
#define COL_WARN    0xFFD97706
#define COL_DANGER  0xFFB91C1C
#define COL_BT      0xFF7C3AED
#define COL_SOUND   0xFF0F766E

void wl_wifi_settings_task(void *arg);
void wl_bluetooth_settings_task(void *arg);
void wl_network_settings_task(void *arg);
void wl_sound_settings_task(void *arg);

typedef enum {
    RADIO_NETWORK = 0,
    RADIO_WIFI,
    RADIO_BT,
    RADIO_SOUND
} radio_panel_t;

static i32 wifi_mouse_x = -1;
static i32 wifi_mouse_y = -1;
static i32 bt_mouse_x = -1;
static i32 bt_mouse_y = -1;
static i32 net_mouse_x = -1;
static i32 net_mouse_y = -1;
static i32 sound_mouse_x = -1;
static i32 sound_mouse_y = -1;
static char wifi_action_msg[96] = "";
static bool wifi_action_error = false;
static char bt_action_msg[96] = "";
static bool bt_action_error = false;

static void draw_text_fit_radio(wl_surface_t *s, i32 x, i32 y, const char *text, i32 max_w, u32 color) {
    if (!text || max_w < 8) return;
    char buf[96];
    int max_chars = max_w / 8;
    if (max_chars > 95) max_chars = 95;
    int i = 0;
    while (text[i] && i < max_chars) {
        buf[i] = text[i];
        i++;
    }
    if (text[i] && i > 1) {
        buf[i - 1] = '>';
        buf[i] = 0;
    } else {
        buf[i] = 0;
    }
    wl_draw_text(s, x, y, buf, color, 0);
}

static void draw_button(wl_surface_t *s, i32 x, i32 y, i32 w, const char *label, bool primary, bool enabled) {
    u32 bg = enabled ? (primary ? COL_BLUE : COL_PANEL) : 0xFFE5E7EB;
    u32 fg = enabled ? (primary ? 0xFFFFFFFF : COL_TEXT) : 0xFF94A3B8;
    wl_draw_rounded_rect(s, x, y, w, 34, 8, bg);
    wl_draw_rounded_outline(s, x, y, w, 34, 8, primary ? COL_BLUE : COL_LINE);
    i32 tx = x + (w - (i32)strlen(label) * 8) / 2;
    if (tx < x + 10) tx = x + 10;
    wl_draw_text(s, tx, y + 10, label, fg, 0);
}

static void draw_toggle(wl_surface_t *s, i32 x, i32 y, bool on, bool usable) {
    u32 track = on ? (usable ? COL_BLUE : 0xFF64748B) : 0xFFCBD5E1;
    wl_draw_rounded_rect(s, x, y, 56, 30, 15, track);
    wl_draw_filled_circle(s, x + (on ? 41 : 15), y + 15, 11, usable ? 0xFFFFFFFF : 0xFFE2E8F0);
}

static const char *wifi_status_line(const iwlwifi_status_t *wifi) {
    if (!wifi->radio_enabled) return "Radio off";
    if (!wifi->present) return "No Wi-Fi adapter detected";
    if (!wifi->firmware_loaded) return "Firmware and 802.11 MAC layer pending";
    if (wifi->connection == IWLWIFI_CONN_CONNECTED) return "Connected";
    if (wifi->connection == IWLWIFI_CONN_SCANNING) return "Scanning";
    return "Ready";
}

static const char *bt_status_line(const hci_status_t *bt) {
    if (!bt->radio_enabled) return "Radio off";
    if (!bt->controller_present) return "No Bluetooth controller detected";
    if (!bt->usb_backend_ready) return "USB HCI transport pending";
    if (bt->connection == HCI_BT_CONNECTED) return "Connected";
    if (bt->connection == HCI_BT_SCANNING) return "Scanning";
    return "Ready";
}

static void draw_device_header(wl_surface_t *s, radio_panel_t panel) {
    const char *title = "Device";
    const char *detail = "System settings";
    u32 accent = COL_BLUE;
    if (panel == RADIO_NETWORK) {
        title = "Ethernet Settings";
        detail = "Wired e1000 link and DHCP lease";
        accent = COL_GREEN;
    } else if (panel == RADIO_WIFI) {
        title = "Wi-Fi Settings";
        detail = "Radio, adapter, scan, and connection state";
        accent = COL_BLUE;
    } else if (panel == RADIO_BT) {
        title = "Bluetooth Settings";
        detail = "Controller, discovery, and pairing state";
        accent = COL_BT;
    } else if (panel == RADIO_SOUND) {
        title = "Sound Settings";
        detail = "Output device and mixer volume";
        accent = COL_SOUND;
    }

    wl_draw_rect(s, 0, 0, RADIO_W, RADIO_H, COL_BG);
    wl_draw_rect(s, 0, 0, RADIO_W, 72, COL_PANEL);
    wl_draw_rect(s, 0, 71, RADIO_W, 1, COL_LINE);
    wl_draw_rounded_rect(s, 24, 22, 10, 28, 5, accent);
    wl_draw_text(s, 48, 20, title, COL_TEXT, 0);
    draw_text_fit_radio(s, 48, 46, detail, 430, COL_MUTED);
}

static void ip_to_string_radio(u32 ip, char *out, usize cap) {
    ksnprintf(out, cap, "%u.%u.%u.%u",
              (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}

static void draw_info_row(wl_surface_t *s, i32 x, i32 y, const char *label, const char *value, u32 value_color) {
    wl_draw_text(s, x, y, label, COL_MUTED, 0);
    draw_text_fit_radio(s, x + 132, y, value, 220, value_color);
}

static void draw_network(wl_surface_t *s) {
    draw_device_header(s, RADIO_NETWORK);

    char ip[32], gw[32], dns[32], mac[32], state[48];
    ip_to_string_radio(g_net_iface.ip_addr, ip, sizeof(ip));
    ip_to_string_radio(g_net_iface.gateway, gw, sizeof(gw));
    ip_to_string_radio(g_net_iface.dns_server, dns, sizeof(dns));
    ksnprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
              g_net_iface.mac_addr[0], g_net_iface.mac_addr[1], g_net_iface.mac_addr[2],
              g_net_iface.mac_addr[3], g_net_iface.mac_addr[4], g_net_iface.mac_addr[5]);
    ksnprintf(state, sizeof(state), "%s, DHCP %s",
              g_net_iface.is_up ? "Link up" : "Link down",
              g_net_iface.dhcp_done ? "ready" : "pending");

    wl_draw_rounded_rect(s, 24, 96, 512, 178, 10, COL_PANEL);
    wl_draw_rounded_outline(s, 24, 96, 512, 178, 10, COL_LINE);
    draw_info_row(s, 48, 120, "Status", state, g_net_iface.dhcp_done ? COL_GREEN : COL_WARN);
    draw_info_row(s, 48, 150, "IPv4 address", g_net_iface.dhcp_done ? ip : "No lease", g_net_iface.dhcp_done ? COL_TEXT : COL_WARN);
    draw_info_row(s, 48, 180, "Gateway", g_net_iface.dhcp_done ? gw : "Not set", COL_TEXT);
    draw_info_row(s, 48, 210, "DNS server", g_net_iface.dhcp_done ? dns : "Not set", COL_TEXT);
    draw_info_row(s, 48, 240, "MAC address", mac, COL_TEXT);

    wl_draw_rounded_rect(s, 24, 302, 512, 64, 10, COL_PANEL);
    wl_draw_rounded_outline(s, 24, 302, 512, 64, 10, COL_LINE);
    wl_draw_text(s, 48, 322, "Driver", COL_MUTED, 0);
    wl_draw_text(s, 180, 322, "Intel e1000", COL_TEXT, 0);
    wl_draw_text(s, 48, 344, "Mode", COL_MUTED, 0);
    wl_draw_text(s, 180, 344, "DHCP via kernel net stack", COL_TEXT, 0);

    draw_button(s, 24, 398, 128, "Renew DHCP", true, g_net_iface.is_up);
    wl_draw_text(s, 172, 408, g_net_iface.is_up ? "Request a fresh wired lease." : "Wired link is down.", COL_MUTED, 0);
}

static void draw_wifi(wl_surface_t *s) {
    const iwlwifi_status_t *wifi = iwlwifi_get_status();
    bool usable = wifi->present && wifi->firmware_loaded;
    bool can_operate = wifi->radio_enabled && usable;
    draw_device_header(s, RADIO_WIFI);

    wl_draw_rounded_rect(s, 24, 96, 512, 92, 10, COL_PANEL);
    wl_draw_rounded_outline(s, 24, 96, 512, 92, 10, COL_LINE);
    wl_draw_text(s, 48, 120, "Wi-Fi radio", COL_TEXT, 0);
    draw_text_fit_radio(s, 48, 146, wifi_status_line(wifi), 330, usable ? COL_GREEN : (wifi->radio_enabled ? COL_WARN : COL_MUTED));
    draw_toggle(s, 456, 127, wifi->radio_enabled, usable);

    wl_draw_text(s, 24, 214, "Known network", COL_MUTED, 0);
    wl_draw_rounded_rect(s, 24, 240, 512, 82, 10, COL_PANEL);
    wl_draw_rounded_outline(s, 24, 240, 512, 82, 10, COL_LINE);
    wl_draw_text(s, 48, 262, wifi->ssid[0] ? wifi->ssid : "YamNet", COL_TEXT, 0);
    draw_text_fit_radio(s, 48, 286,
                        usable ? "Ready for scan/connect" : wifi->last_error,
                        430, usable ? COL_MUTED : COL_WARN);

    if (wifi_action_msg[0]) {
        draw_text_fit_radio(s, 24, 332, wifi_action_msg, 512,
                            wifi_action_error ? COL_WARN : COL_GREEN);
    }

    draw_button(s, 24, 360, 112, "Scan", false, can_operate);
    draw_button(s, 152, 360, 128, "Connect", true, can_operate);
    draw_button(s, 408, 360, 128, wifi->radio_enabled ? "Turn off" : "Turn on", false, true);
}

static void draw_bluetooth(wl_surface_t *s) {
    const hci_status_t *bt = hci_get_status();
    bool usable = bt->controller_present && bt->usb_backend_ready;
    bool can_operate = bt->radio_enabled && usable;
    draw_device_header(s, RADIO_BT);

    wl_draw_rounded_rect(s, 24, 96, 512, 92, 10, COL_PANEL);
    wl_draw_rounded_outline(s, 24, 96, 512, 92, 10, COL_LINE);
    wl_draw_text(s, 48, 120, "Bluetooth radio", COL_TEXT, 0);
    draw_text_fit_radio(s, 48, 146, bt_status_line(bt), 330, usable ? COL_GREEN : (bt->radio_enabled ? COL_WARN : COL_MUTED));
    draw_toggle(s, 456, 127, bt->radio_enabled, usable);

    wl_draw_text(s, 24, 214, "Nearby device", COL_MUTED, 0);
    wl_draw_rounded_rect(s, 24, 240, 512, 82, 10, COL_PANEL);
    wl_draw_rounded_outline(s, 24, 240, 512, 82, 10, COL_LINE);
    wl_draw_text(s, 48, 262, bt->peer_name[0] ? bt->peer_name : "YamDevice", COL_TEXT, 0);
    draw_text_fit_radio(s, 48, 286,
                        usable ? "Ready for discovery/pairing" : bt->last_error,
                        430, usable ? COL_MUTED : COL_WARN);

    if (bt_action_msg[0]) {
        draw_text_fit_radio(s, 24, 332, bt_action_msg, 512,
                            bt_action_error ? COL_WARN : COL_GREEN);
    }

    draw_button(s, 24, 360, 112, "Scan", false, can_operate);
    draw_button(s, 152, 360, 128, "Pair", true, can_operate);
    draw_button(s, 408, 360, 128, bt->radio_enabled ? "Turn off" : "Turn on", false, true);
}

static void draw_sound(wl_surface_t *s) {
    const audio_status_t *audio = audio_get_status();
    draw_device_header(s, RADIO_SOUND);

    wl_draw_rounded_rect(s, 24, 96, 512, 100, 10, COL_PANEL);
    wl_draw_rounded_outline(s, 24, 96, 512, 100, 10, COL_LINE);
    wl_draw_text(s, 48, 122, "Output", COL_TEXT, 0);
    draw_text_fit_radio(s, 48, 150,
                        audio->output_available ? audio->device_name : "No audio output device",
                        430, audio->output_available ? COL_GREEN : COL_WARN);

    wl_draw_rounded_rect(s, 24, 226, 512, 114, 10, COL_PANEL);
    wl_draw_rounded_outline(s, 24, 226, 512, 114, 10, COL_LINE);
    wl_draw_text(s, 48, 252, "Volume", COL_TEXT, 0);
    char vol[32];
    ksnprintf(vol, sizeof(vol), "%u%%", audio->muted ? 0 : audio->volume_percent);
    wl_draw_text(s, 464, 252, vol, COL_TEXT, 0);
    wl_draw_rounded_rect(s, 48, 292, 440, 12, 6, 0xFFE2E8F0);
    u32 fill = audio->muted ? 0 : (audio->volume_percent * 440) / 100;
    wl_draw_rounded_rect(s, 48, 292, fill, 12, 6, audio->muted ? 0xFF94A3B8 : COL_SOUND);

    draw_button(s, 24, 374, 92, "-", false, true);
    draw_button(s, 132, 374, 92, "+", true, true);
    draw_button(s, 396, 374, 140, audio->muted ? "Unmute" : "Mute", false, true);
}

static bool hit(i32 mx, i32 my, i32 x, i32 y, i32 w, i32 h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

static void handle_wifi_click(i32 x, i32 y) {
    const iwlwifi_status_t *wifi = iwlwifi_get_status();
    iwlwifi_op_result_t result = IWLWIFI_OP_OK;
    if (hit(x, y, 456, 127, 56, 30) || hit(x, y, 408, 360, 128, 34)) {
        result = iwlwifi_set_radio_enabled(!wifi->radio_enabled);
        ksnprintf(wifi_action_msg, sizeof(wifi_action_msg), "%s",
                  wifi->radio_enabled ? iwlwifi_result_string(result) : "Wi-Fi radio is off");
    } else if (hit(x, y, 24, 360, 112, 34)) {
        result = iwlwifi_scan();
        ksnprintf(wifi_action_msg, sizeof(wifi_action_msg), "Scan: %s",
                  iwlwifi_result_string(result));
    } else if (hit(x, y, 152, 360, 128, 34)) {
        result = iwlwifi_connect_default();
        ksnprintf(wifi_action_msg, sizeof(wifi_action_msg), "Connect: %s",
                  iwlwifi_result_string(result));
    } else {
        return;
    }
    wifi_action_error = (result != IWLWIFI_OP_OK);
}

static void handle_bt_click(i32 x, i32 y) {
    const hci_status_t *bt = hci_get_status();
    hci_op_result_t result = HCI_OP_OK;
    if (hit(x, y, 456, 127, 56, 30) || hit(x, y, 408, 360, 128, 34)) {
        result = hci_set_radio_enabled(!bt->radio_enabled);
        ksnprintf(bt_action_msg, sizeof(bt_action_msg), "%s",
                  bt->radio_enabled ? hci_result_string(result) : "Bluetooth radio is off");
    } else if (hit(x, y, 24, 360, 112, 34)) {
        result = hci_scan();
        ksnprintf(bt_action_msg, sizeof(bt_action_msg), "Scan: %s",
                  hci_result_string(result));
    } else if (hit(x, y, 152, 360, 128, 34)) {
        result = hci_connect_default();
        ksnprintf(bt_action_msg, sizeof(bt_action_msg), "Pair: %s",
                  hci_result_string(result));
    } else {
        return;
    }
    bt_action_error = (result != HCI_OP_OK);
}

static void handle_network_click(i32 x, i32 y) {
    if (hit(x, y, 24, 398, 128, 34) && g_net_iface.is_up) {
        dhcp_start();
        kprintf("[NETSET] DHCP renew requested from Ethernet Settings\n");
    }
}

static void handle_sound_click(i32 x, i32 y) {
    if (hit(x, y, 24, 374, 92, 34)) {
        audio_set_volume_percent(audio_volume_percent() >= 10 ? audio_volume_percent() - 10 : 0);
    } else if (hit(x, y, 132, 374, 92, 34)) {
        audio_set_volume_percent(audio_volume_percent() + 10);
    } else if (hit(x, y, 396, 374, 140, 34)) {
        audio_set_muted(!audio_is_muted());
    }
}

void wl_network_settings_task(void *arg) {
    (void)arg;
    task_sleep_ms(120);
    wl_surface_t *s = wl_surface_create("Ethernet Settings", 260, 74, RADIO_W, RADIO_H, sched_current()->id);
    if (!s) return;
    draw_network(s);
    wl_surface_commit(s);
    u32 my_id = s->id;
    while (s->state == WL_SURFACE_ACTIVE && s->id == my_id) {
        input_event_t ev;
        bool dirty = false;
        while (wl_surface_pop_event(s, &ev)) {
            if (ev.type == EV_ABS && ev.code == 0) net_mouse_x = ev.value;
            else if (ev.type == EV_ABS && ev.code == 1) net_mouse_y = ev.value;
            else if (ev.type == EV_KEY && ev.value == KEY_PRESSED && ev.code >= 0x110 &&
                     net_mouse_x >= 0 && net_mouse_y >= 0) {
                handle_network_click(net_mouse_x, net_mouse_y);
                dirty = true;
            }
        }
        if (dirty) {
            draw_network(s);
            wl_surface_commit(s);
        }
        task_sleep_ms(16);
    }
}

void wl_wifi_settings_task(void *arg) {
    (void)arg;
    task_sleep_ms(120);
    wl_surface_t *s = wl_surface_create("Wi-Fi Settings", 300, 86, RADIO_W, RADIO_H, sched_current()->id);
    if (!s) return;
    draw_wifi(s);
    wl_surface_commit(s);
    u32 my_id = s->id;
    while (s->state == WL_SURFACE_ACTIVE && s->id == my_id) {
        input_event_t ev;
        bool dirty = false;
        while (wl_surface_pop_event(s, &ev)) {
            if (ev.type == EV_ABS && ev.code == 0) wifi_mouse_x = ev.value;
            else if (ev.type == EV_ABS && ev.code == 1) wifi_mouse_y = ev.value;
            else if (ev.type == EV_KEY && ev.value == KEY_PRESSED && ev.code >= 0x110 &&
                     wifi_mouse_x >= 0 && wifi_mouse_y >= 0) {
                handle_wifi_click(wifi_mouse_x, wifi_mouse_y);
                dirty = true;
            }
        }
        if (dirty) {
            draw_wifi(s);
            wl_surface_commit(s);
        }
        task_sleep_ms(16);
    }
}

void wl_bluetooth_settings_task(void *arg) {
    (void)arg;
    task_sleep_ms(120);
    wl_surface_t *s = wl_surface_create("Bluetooth Settings", 340, 108, RADIO_W, RADIO_H, sched_current()->id);
    if (!s) return;
    draw_bluetooth(s);
    wl_surface_commit(s);
    u32 my_id = s->id;
    while (s->state == WL_SURFACE_ACTIVE && s->id == my_id) {
        input_event_t ev;
        bool dirty = false;
        while (wl_surface_pop_event(s, &ev)) {
            if (ev.type == EV_ABS && ev.code == 0) bt_mouse_x = ev.value;
            else if (ev.type == EV_ABS && ev.code == 1) bt_mouse_y = ev.value;
            else if (ev.type == EV_KEY && ev.value == KEY_PRESSED && ev.code >= 0x110 &&
                     bt_mouse_x >= 0 && bt_mouse_y >= 0) {
                handle_bt_click(bt_mouse_x, bt_mouse_y);
                dirty = true;
            }
        }
        if (dirty) {
            draw_bluetooth(s);
            wl_surface_commit(s);
        }
        task_sleep_ms(16);
    }
}

void wl_sound_settings_task(void *arg) {
    (void)arg;
    task_sleep_ms(120);
    wl_surface_t *s = wl_surface_create("Sound Settings", 380, 120, RADIO_W, RADIO_H, sched_current()->id);
    if (!s) return;
    draw_sound(s);
    wl_surface_commit(s);
    u32 my_id = s->id;
    while (s->state == WL_SURFACE_ACTIVE && s->id == my_id) {
        input_event_t ev;
        bool dirty = false;
        while (wl_surface_pop_event(s, &ev)) {
            if (ev.type == EV_ABS && ev.code == 0) sound_mouse_x = ev.value;
            else if (ev.type == EV_ABS && ev.code == 1) sound_mouse_y = ev.value;
            else if (ev.type == EV_KEY && ev.value == KEY_PRESSED && ev.code >= 0x110 &&
                     sound_mouse_x >= 0 && sound_mouse_y >= 0) {
                handle_sound_click(sound_mouse_x, sound_mouse_y);
                dirty = true;
            }
        }
        if (dirty) {
            draw_sound(s);
            wl_surface_commit(s);
        }
        task_sleep_ms(16);
    }
}
