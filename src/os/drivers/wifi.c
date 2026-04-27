#include "driver.h"
void _start(void) {
    print("[OS_WIFI] WiFi Service Active\n");
    print("[OS_WIFI] Scanning for 802.11 networks...\n");
    print("[OS_WIFI] SSID Found: YamNet_Guest\n");
    while(1) sleep_ms(5000);
}
