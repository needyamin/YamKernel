#include "../lib/libc/stdlib.c"
#include "../lib/libc/string.c"
#include "../lib/libc/stdio.c"
#include "../lib/libyam/syscall.h"
#include "../lib/libyam/ipc.h"

int main() {
    printf("[AUTHD] Authentication Daemon started in Ring 3!\n");
    
    // Wait for the compositor to create the "auth_channel"
    u32 chan_id = -1;
    while (1) {
        chan_id = yam_channel_lookup("auth_channel");
        if (chan_id != (u32)-1) break;
        syscall1(SYS_SLEEPMS, 100);
    }
    
    printf("[AUTHD] Found auth_channel! Listening for auth requests...\n");
    
    while(1) {
        yam_message_t msg;
        if (yam_channel_recv(chan_id, &msg)) {
            // Check if it's an auth request
            // For now, we only support msg_type 1 (AUTH_REQ)
            if (msg.msg_type == 1) {
                // The data contains the password string
                char password[64] = {0};
                int len = msg.length < 64 ? msg.length : 63;
                for (int i = 0; i < len; i++) {
                    password[i] = (char)msg.data[i];
                }
                
                printf("[AUTHD] Received auth request: '%s'\n", password);
                
                // Hardcoded password verification
                bool success = false;
                if (strcmp(password, "yamin") == 0) {
                    success = true;
                    printf("[AUTHD] Access GRANTED!\n");
                } else {
                    printf("[AUTHD] Access DENIED!\n");
                }
                
                // Send reply back to the sender
                u32 reply_type = success ? 2 : 3; // 2=SUCCESS, 3=FAIL
                yam_channel_send(chan_id, reply_type, NULL, 0);
            }
        } else {
            syscall1(SYS_SLEEPMS, 50); // polling
        }
    }
    
    return 0;
}
