#include "yam.h"
#include "wl_draw_user.h"

extern const uint8_t font_basic_8x16[128][16];

#define BROWSER_W  640
#define BROWSER_H  430

#define C_BG        0xFF111827
#define C_TOP       0xFF1F2937
#define C_PANEL     0xFF243244
#define C_CARD      0xFF172033
#define C_TEXT      0xFFF3F7FB
#define C_MUTED     0xFF9AA8BA
#define C_BLUE      0xFF60A5FA
#define C_GREEN     0xFF34D399
#define C_YELLOW    0xFFFBBF24
#define C_RED       0xFFFB7185

static char url_buffer[128] = "yamos.local";
static char page_title[64] = "YamOS";
static bool is_loading = false;
static int load_progress = 0;
static const char *browser_status = "Ready";
static const char *browser_content =
    "YamOS Desktop\n"
    "A small userspace desktop running on a custom kernel.\n"
    "\n"
    "Highlights:\n"
    "- Wayland-style surfaces backed by shared memory\n"
    "- Ring 3 applications with keyboard and pointer input\n"
    "- Auth service, compositor, browser, terminal, calculator\n"
    "\n"
    "Try: google.com, github.com, kernel.org, wikipedia.org";

static const char sc_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',  0,
    '\\','z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
};

static void draw_text_line(wl_user_buffer_t *buf, int x, int y, const char *s, u32 color) {
    char line[76];
    int n = 0;
    while (*s && *s != '\n' && n < 75) line[n++] = *s++;
    line[n] = 0;
    wl_user_draw_text(buf, x, y, line, color);
}

static void draw_multiline(wl_user_buffer_t *buf, int x, int y, const char *s, u32 color) {
    int line_y = y;
    while (*s && line_y < BROWSER_H - 34) {
        const char *line = s;
        draw_text_line(buf, x, line_y, line, color);
        while (*s && *s != '\n') s++;
        if (*s == '\n') s++;
        line_y += 18;
    }
}

static void draw_button(wl_user_buffer_t *buf, int x, int y, int w, const char *label, u32 bg, u32 fg) {
    wl_user_draw_rect(buf, x, y, w, 24, bg);
    wl_user_draw_text(buf, x + 10, y + 5, label, fg);
}

static void draw_browser(wl_user_buffer_t *buf) {
    wl_user_draw_rect(buf, 0, 0, BROWSER_W, BROWSER_H, C_BG);
    wl_user_draw_rect(buf, 0, 0, BROWSER_W, 70, C_TOP);
    wl_user_draw_rect(buf, 0, 69, BROWSER_W, 1, 0xFF374151);

    wl_user_draw_rect(buf, 10, 8, 150, 30, C_BG);
    wl_user_draw_rect(buf, 10, 38, 150, 2, C_BLUE);
    wl_user_draw_text(buf, 22, 15, page_title, C_TEXT);
    wl_user_draw_text(buf, 178, 15, "+", C_MUTED);

    draw_button(buf, 12, 42, 30, "<", C_PANEL, C_TEXT);
    draw_button(buf, 48, 42, 30, ">", C_PANEL, C_TEXT);
    draw_button(buf, 84, 42, 30, "R", C_PANEL, C_TEXT);

    wl_user_draw_rect(buf, 124, 42, 408, 24, C_BG);
    wl_user_draw_rect(buf, 124, 65, 408, 1, C_BLUE);
    wl_user_draw_text(buf, 134, 47, url_buffer, C_TEXT);
    draw_button(buf, 542, 42, 86, "LOAD", C_GREEN, C_BG);

    if (is_loading) {
        wl_user_draw_rect(buf, 0, 70, (BROWSER_W * load_progress) / 100, 3, C_GREEN);
    }

    wl_user_draw_rect(buf, 0, 73, 132, BROWSER_H - 95, 0xFF141C2B);
    wl_user_draw_text(buf, 14, 92, "Bookmarks", C_MUTED);
    draw_button(buf, 14, 120, 104, "YamOS", C_PANEL, C_TEXT);
    draw_button(buf, 14, 154, 104, "GitHub", C_PANEL, C_TEXT);
    draw_button(buf, 14, 188, 104, "Kernel", C_PANEL, C_TEXT);
    draw_button(buf, 14, 222, 104, "Wiki", C_PANEL, C_TEXT);

    wl_user_draw_rect(buf, 150, 92, 460, 248, C_CARD);
    wl_user_draw_rect(buf, 150, 92, 460, 4, C_BLUE);
    if (is_loading) {
        wl_user_draw_text(buf, 330, 204, "Loading...", C_MUTED);
    } else {
        draw_multiline(buf, 170, 112, browser_content, C_TEXT);
    }

    wl_user_draw_rect(buf, 150, 354, 138, 42, C_PANEL);
    wl_user_draw_text(buf, 164, 366, "Engine", C_MUTED);
    wl_user_draw_text(buf, 164, 382, "YamNet demo", C_GREEN);
    wl_user_draw_rect(buf, 304, 354, 138, 42, C_PANEL);
    wl_user_draw_text(buf, 318, 366, "Sandbox", C_MUTED);
    wl_user_draw_text(buf, 318, 382, "Ring 3", C_YELLOW);
    wl_user_draw_rect(buf, 458, 354, 152, 42, C_PANEL);
    wl_user_draw_text(buf, 472, 366, "Render", C_MUTED);
    wl_user_draw_text(buf, 472, 382, "Shared buffer", C_BLUE);

    wl_user_draw_rect(buf, 0, BROWSER_H - 24, BROWSER_W, 24, C_TOP);
    wl_user_draw_text(buf, 12, BROWSER_H - 19, browser_status, C_MUTED);
}

static void browser_load_page(u32 sid, wl_user_buffer_t *buf) {
    is_loading = true;
    browser_status = "Connecting...";
    for (int p = 0; p <= 100; p += 20) {
        load_progress = p;
        draw_browser(buf);
        if (wl_commit(sid) < 0) exit(0);
        sleep_ms(70);
    }

    if (strstr(url_buffer, "google.com")) {
        browser_content =
            "Google\n"
            "Search: [ custom kernel wayland desktop ]\n"
            "\n"
            "Results:\n"
            "- YamOS compositor boots to a real desktop\n"
            "- Apps render through shared userspace buffers\n"
            "- Scheduler and syscall path survive GUI workload";
        strcpy(page_title, "Google");
    } else if (strstr(url_buffer, "github.com")) {
        browser_content =
            "GitHub - needyamin/YamOS\n"
            "\n"
            "Repository dashboard\n"
            "Code     Issues     Pull requests     Actions\n"
            "\n"
            "Recent work: userspace apps, compositor polish, syscall fixes.";
        strcpy(page_title, "GitHub");
    } else if (strstr(url_buffer, "kernel.org")) {
        browser_content =
            "kernel.org\n"
            "\n"
            "Kernel engineering notes:\n"
            "- Keep AP scheduler state isolated until SMP runqueues exist\n"
            "- Restore user address space before syscall return\n"
            "- Batch compositor input and pace redraws predictably";
        strcpy(page_title, "kernel.org");
    } else if (strstr(url_buffer, "wikipedia.org")) {
        browser_content =
            "Wikipedia\n"
            "\n"
            "Wayland is a protocol for a compositor to talk to clients.\n"
            "In YamOS the same idea is modeled with surfaces, events,\n"
            "and shared frame buffers owned by userspace applications.";
        strcpy(page_title, "Wikipedia");
    } else {
        browser_content =
            "YamOS local page\n"
            "\n"
            "This demo page was selected by the userspace browser.\n"
            "The UI, keyboard input, loading state, and commit loop are\n"
            "all running outside the kernel.";
        strcpy(page_title, url_buffer);
    }

    browser_status = "Done";
    is_loading = false;
    draw_browser(buf);
    if (wl_commit(sid) < 0) exit(0);
}

void _start(void) {
    print("[APP_DBG] browser start\n");
    i32 sid = wl_create_surface("Browser", 150, 390, BROWSER_W, BROWSER_H);
    if (sid < 0) { print("[APP_DBG] browser create failed\n"); exit(1); }
    void *buffer_vaddr = (void *)0x30000000;
    if (wl_map_buffer(sid, buffer_vaddr) < 0) { print("[APP_DBG] browser map failed\n"); exit(2); }
    print("[APP_DBG] browser mapped buffer\n");
    wl_user_buffer_t buf = { .pixels = (u32 *)buffer_vaddr, .width = BROWSER_W, .height = BROWSER_H };
    draw_browser(&buf);
    if (wl_commit(sid) < 0) exit(0);
    print("[APP_DBG] browser first commit\n");

    while (1) {
        input_event_t ev;
        bool changed = false;
        while (wl_poll_event(sid, &ev)) {
            if (ev.type == EV_CLOSE) {
                print("[APP_DBG] browser close requested\n");
                exit(0);
            }
            if (ev.type == EV_KEY && ev.value == KEY_PRESSED) {
                u16 sc = ev.code;
                char c = (sc < 128) ? sc_ascii[sc] : 0;
                if (c == '\n') {
                    browser_load_page(sid, &buf);
                } else if (c == '\b') {
                    int len = strlen(url_buffer);
                    if (len > 0) url_buffer[len - 1] = 0;
                    browser_status = "Editing address";
                    changed = true;
                } else if (c >= 32 && c < 127) {
                    int len = strlen(url_buffer);
                    if (len < 127) {
                        url_buffer[len] = c;
                        url_buffer[len + 1] = 0;
                    }
                    browser_status = "Editing address";
                    changed = true;
                }
            }
        }
        if (changed) {
            draw_browser(&buf);
            if (wl_commit(sid) < 0) {
                print("[APP_DBG] browser commit failed; exiting\n");
                exit(0);
            }
        }
        sleep_ms(33);
    }
}
