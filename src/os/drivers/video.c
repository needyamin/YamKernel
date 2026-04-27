#include "driver.h"
void _start(void) {
    print("[OS_VIDEO] Video Service Active\n");
    print("[OS_VIDEO] Controlling DRM/KMS scanout...\n");
    while(1) sleep_ms(5000);
}
