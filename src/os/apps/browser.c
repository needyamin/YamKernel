#include "yam.h"
#include "wl_draw_user.h"

extern const uint8_t font_basic_8x16[128][16];

static char url_buffer[128] = "google.com";
static char page_title[64] = "Google";
static bool is_loading = false;
static int load_progress = 0;
static const char *browser_status = "Ready";

static const char *browser_content = 
    "Welcome to YamOS Browser (Ring 3)!\n"
    "==================================\n\n"
    "This browser is now running as a separate ELF process\n"
    "isolated from the kernel. It uses shared-memory\n"
    "graphics for zero-copy rendering.\n\n"
    "Status: Process ID [N/A]\n"
    "Memory: 0.5 MB Mapped\n\n"
    "Try typing: wikipedia.org, github.com, kernel.org";

static void draw_browser(wl_user_buffer_t *buf) {
    /* Dracula Background */
    wl_user_draw_rect(buf, 0, 0, 600, 400, 0xFF282A36);
    /* Address Bar */
    wl_user_draw_rect(buf, 0, 0, 600, 50, 0xFF44475A);
    /* Tab */
    wl_user_draw_rect(buf, 5, 5, 120, 25, 0xFF282A36);
    wl_user_draw_text(buf, 15, 10, page_title, 0xFFF8F8F2);
    /* Navigation Icons */
    wl_user_draw_text(buf, 15, 32, "<  >  O", 0xFFBD93F9); 
    /* URL Field */
    wl_user_draw_rect(buf, 80, 28, 450, 22, 0xFF282A36);
    wl_user_draw_rect(buf, 80, 48, 450, 2, 0xFFBD93F9);
    wl_user_draw_text(buf, 90, 32, url_buffer, 0xFFF8F8F2);
    /* Go Button */
    wl_user_draw_rect(buf, 535, 28, 55, 22, 0xFF50FA7B);
    wl_user_draw_text(buf, 545, 32, "GO", 0xFF282A36);
    /* Progress Bar */
    if (is_loading) wl_user_draw_rect(buf, 0, 50, (600 * load_progress) / 100, 2, 0xFF50FA7B);
    /* Content Area */
    wl_user_draw_rect(buf, 0, 52, 600, 328, 0xFF1E1F29);
    if (is_loading) wl_user_draw_text(buf, 240, 200, "Loading...", 0xFF6272A4);
    else wl_user_draw_text(buf, 20, 70, browser_content, 0xFFF8F8F2);
    /* Status Bar */
    wl_user_draw_rect(buf, 0, 380, 600, 20, 0xFF44475A);
    wl_user_draw_text(buf, 10, 383, browser_status, 0xFF6272A4);
}

static void browser_load_page(u32 sid, wl_user_buffer_t *buf) {
    is_loading = true;
    browser_status = "Connecting...";
    for (int p = 0; p <= 100; p += 25) {
        load_progress = p;
        draw_browser(buf);
        wl_commit(sid);
        sleep_ms(100);
    }
    if (strstr(url_buffer, "google.com")) {
        browser_content = "Google\n\nSearch: [ __________ ]\n\nYamOS v0.3.0 is out!";
        strcpy(page_title, "Google");
    } else if (strstr(url_buffer, "github.com")) {
        browser_content = "GitHub - needyamin/YamOS\n\n[ Code ] [ Issues ]\n\nUserspace migration 90% complete.";
        strcpy(page_title, "GitHub");
    } else {
        browser_content = "Welcome to the future of YamOS.\nThis site was loaded in Userspace!";
        strcpy(page_title, url_buffer);
    }
    browser_status = "Done";
    is_loading = false;
    draw_browser(buf);
    wl_commit(sid);
}

void _start(void) {
    i32 sid = wl_create_surface("YamBrowser", 150, 420, 600, 400);
    if (sid < 0) exit(1);
    void *buffer_vaddr = (void *)0x30000000;
    if (wl_map_buffer(sid, buffer_vaddr) < 0) exit(2);
    wl_user_buffer_t buf = { .pixels = (u32 *)buffer_vaddr, .width = 600, .height = 400 };
    draw_browser(&buf);
    wl_commit(sid);
    while (1) {
        input_event_t ev;
        bool changed = false;
        while (wl_poll_event(sid, &ev)) {
            if (ev.type == EV_KEY && ev.value == 1) {
                u16 sc = ev.code;
                if (sc == 28) { browser_load_page(sid, &buf); }
                else if (sc == 14) { int len = strlen(url_buffer); if (len > 0) url_buffer[len-1] = 0; changed = true; }
                else if (sc == 57) { int len = strlen(url_buffer); if (len < 127) { url_buffer[len] = ' '; url_buffer[len+1] = 0; } changed = true; }
                else if (sc < 128) {
                    /* Primitive scancode mapping for demo */
                    char c = (sc == 2) ? '1' : (sc == 3) ? '2' : (sc == 4) ? '3' : (sc == 5) ? '4' : (sc == 6) ? '5' : (sc == 7) ? '6' : (sc == 8) ? '7' : (sc == 9) ? '8' : (sc == 10) ? '9' : (sc == 11) ? '0' : 0;
                    if (!c && sc == 31) c = 's'; 
                    if (!c && sc == 30) c = 'a'; 
                    if (!c && sc == 48) c = 'b';
                    if (!c && sc == 24) c = 'o'; 
                    if (!c && sc == 49) c = 'n'; 
                    if (!c && sc == 50) c = 'm';
                    if (!c && sc == 52) c = '.'; 
                    if (!c && sc == 32) c = 'd'; 
                    if (!c && sc == 18) c = 'e';
                    if (!c && sc == 33) c = 'f'; 
                    if (!c && sc == 34) c = 'g'; 
                    if (!c && sc == 35) c = 'h';
                    if (!c && sc == 23) c = 'i'; 
                    if (!c && sc == 37) c = 'k'; 
                    if (!c && sc == 38) c = 'l';
                    if (!c && sc == 25) c = 'p'; 
                    if (!c && sc == 16) c = 'q'; 
                    if (!c && sc == 19) c = 'r';
                    if (!c && sc == 20) c = 't'; 
                    if (!c && sc == 22) c = 'u';
                    if (!c && sc == 47) c = 'v'; 
                    if (!c && sc == 17) c = 'w'; 
                    if (!c && sc == 45) c = 'x';
                    if (!c && sc == 21) c = 'y'; 
                    if (!c && sc == 44) c = 'z';
                    if (c) { int len = strlen(url_buffer); if (len < 127) { url_buffer[len] = c; url_buffer[len+1] = 0; } changed = true; }
                }
            }
        }
        if (changed) { draw_browser(&buf); wl_commit(sid); }
        sleep_ms(33);
    }
}
