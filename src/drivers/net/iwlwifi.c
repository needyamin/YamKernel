/* ============================================================================
 * YamKernel — Intel Wireless WiFi (iwlwifi) Stub Driver
 * ============================================================================ */
#include "../bus/pci.h"
#include "../../lib/kprintf.h"

void iwlwifi_init(void) {
    /* Common Intel wireless IDs: 0x0082 (Centrino 6205), 0x4232 (5100) */
    pci_device_t *wifi = pci_get_device(0x8086, 0x0082);
    if (!wifi) wifi = pci_get_device(0x8086, 0x4232);

    if (wifi) {
        kprintf_color(0xFF00FF88, "[wlan0] Intel Wireless NIC detected at %02x:%02x.%u\n",
                      wifi->bus, wifi->slot, wifi->func);
    } else {
        kprintf_color(0xFF888888, "[wlan0] No Intel wireless NIC present (firmware blob required)\n");
    }
}
