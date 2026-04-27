#include "driver.h"
void _start(void) {
    print("[OS_IMG] Image Decoding Service Active\n");
    print("[OS_IMG] Monitoring /boot/wallpaper.bin...\n");
    while(1) sleep_ms(5000);
}
