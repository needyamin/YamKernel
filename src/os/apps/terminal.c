#include "yam.h"
#include "wl_draw_user.h"

extern const uint8_t font_basic_8x16[128][16];

#define TERM_COLS      76
#define TERM_ROWS      23
#define TERM_W         (TERM_COLS * 8)
#define TERM_H         ((TERM_ROWS * 16) + 34)

#define COL_BG         0xFF10131A
#define COL_PANEL      0xFF1B2130
#define COL_PANEL_2    0xFF252D3D
#define COL_FG         0xFFE8EEF7
#define COL_DIM        0xFF7C8798
#define COL_PROMPT     0xFF36D399
#define COL_ACCENT     0xFF60A5FA
#define COL_WARN       0xFFFBBF24
#define COL_ERR        0xFFFF5C70
#define COL_CURSOR     0xFFE8EEF7

static const char sc_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',  0,
    '\\','z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
};

static char screen[TERM_ROWS][TERM_COLS + 1];
static u32  screen_color[TERM_ROWS];
static int  cur_row = 0;
static int  cur_col = 0;
static char input_buf[128];
static int  input_len = 0;
static const char *status_text = "ready";

static bool streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static bool starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return false;
    }
    return true;
}

static void term_clear(void) {
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c <= TERM_COLS; c++) screen[r][c] = 0;
        screen_color[r] = COL_FG;
    }
    cur_row = 0;
    cur_col = 0;
}

static void term_scroll(void) {
    for (int r = 0; r < TERM_ROWS - 1; r++) {
        for (int c = 0; c <= TERM_COLS; c++) screen[r][c] = screen[r + 1][c];
        screen_color[r] = screen_color[r + 1];
    }
    for (int c = 0; c <= TERM_COLS; c++) screen[TERM_ROWS - 1][c] = 0;
    screen_color[TERM_ROWS - 1] = COL_FG;
    if (cur_row > 0) cur_row--;
}

static void term_putchar(char c, u32 color) {
    if (c == '\n') {
        screen_color[cur_row] = color;
        cur_row++;
        cur_col = 0;
        if (cur_row >= TERM_ROWS) term_scroll();
        return;
    }
    if (c == '\b') {
        if (cur_col > 0) {
            cur_col--;
            screen[cur_row][cur_col] = ' ';
        }
        return;
    }
    if (cur_col < TERM_COLS) {
        screen[cur_row][cur_col] = c;
        screen_color[cur_row] = color;
        cur_col++;
    }
    if (cur_col >= TERM_COLS) {
        cur_col = 0;
        cur_row++;
        if (cur_row >= TERM_ROWS) term_scroll();
    }
}

static void term_puts(const char *s, u32 color) {
    while (*s) term_putchar(*s++, color);
}

static void show_prompt(void) {
    term_puts("root@yam", COL_PROMPT);
    term_puts(":desktop$ ", COL_FG);
}

static void process_command(void) {
    input_buf[input_len] = 0;
    char *cmd = input_buf;
    while (*cmd == ' ') cmd++;

    status_text = "command complete";
    if (*cmd == 0) {
    } else if (streq(cmd, "help")) {
        term_puts("Commands:", COL_ACCENT); term_putchar('\n', COL_FG);
        term_puts("  help clear uname whoami apps mem about echo exit", COL_FG); term_putchar('\n', COL_FG);
    } else if (streq(cmd, "clear")) {
        term_clear();
        status_text = "screen cleared";
    } else if (streq(cmd, "uname")) {
        term_puts("YamOS v0.3.0 x86_64 userspace desktop", COL_PROMPT); term_putchar('\n', COL_FG);
    } else if (streq(cmd, "whoami")) {
        term_puts("root", COL_PROMPT); term_putchar('\n', COL_FG);
    } else if (streq(cmd, "apps")) {
        term_puts("Terminal  Browser  Calculator  Compositor  Authd", COL_ACCENT); term_putchar('\n', COL_FG);
    } else if (streq(cmd, "mem")) {
        term_puts("Shared buffers mapped, apps isolated, compositor owns scanout.", COL_FG); term_putchar('\n', COL_FG);
    } else if (streq(cmd, "about")) {
        term_puts("YamOS desktop shell running in ring 3 over Wayland-style IPC.", COL_FG); term_putchar('\n', COL_FG);
    } else if (starts_with(cmd, "echo ")) {
        term_puts(cmd + 5, COL_FG); term_putchar('\n', COL_FG);
    } else if (streq(cmd, "exit")) {
        exit(0);
    } else {
        term_puts(cmd, COL_ERR);
        term_puts(": command not found", COL_ERR);
        term_putchar('\n', COL_FG);
        status_text = "last command failed";
    }
    input_len = 0;
}

static void draw_terminal(wl_user_buffer_t *buf) {
    wl_user_draw_rect(buf, 0, 0, TERM_W, TERM_H, COL_BG);
    wl_user_draw_rect(buf, 8, 8, TERM_W - 16, TERM_ROWS * 16 + 8, COL_PANEL_2);
    for (int r = 0; r < TERM_ROWS; r++) {
        if (screen[r][0] != 0 || r == cur_row) {
            wl_user_draw_text(buf, 14, 12 + (r * 16), screen[r], screen_color[r]);
        }
    }

    wl_user_draw_rect(buf, 0, TERM_H - 24, TERM_W, 24, COL_PANEL);
    wl_user_draw_text(buf, 12, TERM_H - 19, "Terminal", COL_ACCENT);
    wl_user_draw_text(buf, TERM_W - 128, TERM_H - 19, status_text, COL_DIM);

    static int blink = 0;
    if ((++blink / 15) & 1) {
        wl_user_draw_rect(buf, 14 + cur_col * 8, 12 + cur_row * 16, 8, 16, COL_CURSOR);
    }
}

void _start(void) {
    print("[APP_DBG] terminal start\n");
    i32 sid = wl_create_surface("Terminal", 50, 70, TERM_W, TERM_H);
    if (sid < 0) { print("[APP_DBG] terminal create failed\n"); exit(1); }

    void *buffer_vaddr = (void *)0x20000000;
    if (wl_map_buffer(sid, buffer_vaddr) < 0) { print("[APP_DBG] terminal map failed\n"); exit(2); }
    print("[APP_DBG] terminal mapped buffer\n");

    wl_user_buffer_t buf = { .pixels = (u32 *)buffer_vaddr, .width = TERM_W, .height = TERM_H };

    term_clear();
    term_puts("YamOS Terminal", COL_ACCENT); term_putchar('\n', COL_FG);
    term_puts("Type 'help' to explore the desktop tools.", COL_DIM); term_putchar('\n', COL_FG);
    term_putchar('\n', COL_FG);
    show_prompt();

    while (1) {
        input_event_t ev;
        while (wl_poll_event(sid, &ev)) {
            if (ev.type == EV_KEY && ev.value == KEY_PRESSED) {
                u16 sc = ev.code;
                char c = (sc < 128) ? sc_ascii[sc] : 0;
                if (c == '\n') {
                    term_putchar('\n', COL_FG);
                    process_command();
                    show_prompt();
                } else if (c == '\b') {
                    if (input_len > 0) {
                        input_len--;
                        term_putchar('\b', COL_FG);
                        status_text = "editing";
                    }
                } else if (c >= 32 && c < 127 && input_len < 126) {
                    input_buf[input_len++] = c;
                    term_putchar(c, COL_FG);
                    status_text = "typing";
                }
            }
        }

        draw_terminal(&buf);
        if (wl_commit(sid) < 0) {
            print("[APP_DBG] terminal commit failed; exiting\n");
            exit(0);
        }
        static bool first_commit_logged = false;
        if (!first_commit_logged) {
            print("[APP_DBG] terminal first commit\n");
            first_commit_logged = true;
        }
        sleep_ms(33);
    }
}
