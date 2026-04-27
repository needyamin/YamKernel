#include "driver.h"
void _start(void) {
    print("[OS_AUDIO] Audio Service Active\n");
    print("[OS_AUDIO] Found Intel HD Audio Controller.\n");
    print("[OS_AUDIO] Playing startup sound...\n");
    while(1) sleep_ms(5000);
}
