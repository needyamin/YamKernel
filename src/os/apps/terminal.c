#include "yam.h"
#include "wl_draw_user.h"

extern const uint8_t font_basic_8x16[128][16];

#define TERM_COLS  72
#define TERM_ROWS  22
#define TERM_W     (TERM_COLS * 8)
#define TERM_H     (TERM_ROWS * 16)

#define COL_BG       0xFF1A1A2E
#define COL_FG       0xFF00FF88
#define COL_PROMPT   0xFF00DDFF
#define COL_CURSOR   0xFF00FF88
#define COL_ERR      0xFFFF4444

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

static void term_scroll(void) {
    for (int r = 0; r < TERM_ROWS - 1; r++) {
        for(int c=0; c<=TERM_COLS; c++) screen[r][c] = screen[r+1][c];
        screen_color[r] = screen_color[r + 1];
    }
    for(int c=0; c<=TERM_COLS; c++) screen[TERM_ROWS-1][c] = 0;
    screen_color[TERM_ROWS - 1] = COL_FG;
    if (cur_row > 0) cur_row--;
}

static void term_putchar(char c, u32 color) {
    if (c == '\n') {
        screen_color[cur_row] = color;
        cur_row++; cur_col = 0;
        if (cur_row >= TERM_ROWS) term_scroll();
        return;
    }
    if (c == '\b') {
        if (cur_col > 0) { cur_col--; screen[cur_row][cur_col] = ' '; }
        return;
    }
    if (cur_col < TERM_COLS) {
        screen[cur_row][cur_col] = c;
        screen_color[cur_row] = color;
        cur_col++;
    }
    if (cur_col >= TERM_COLS) {
        cur_col = 0; cur_row++;
        if (cur_row >= TERM_ROWS) term_scroll();
    }
}

static void term_puts(const char *s, u32 color) {
    while (*s) term_putchar(*s++, color);
}

static void process_command(void) {
    input_buf[input_len] = 0;
    char *cmd = input_buf;
    while (*cmd == ' ') cmd++;

    if (*cmd == 0) {
    } else if (strlen(cmd) >= 4 && cmd[0]=='h' && cmd[1]=='e' && cmd[2]=='l' && cmd[3]=='p') {
        term_puts("YamOS Userspace Shell", COL_PROMPT); term_putchar('\n', COL_FG);
        term_puts("Commands: help, clear, uname, whoami, exit", COL_FG); term_putchar('\n', COL_FG);
    } else if (strlen(cmd) >= 5 && cmd[0]=='c' && cmd[1]=='l' && cmd[2]=='e' && cmd[3]=='a' && cmd[4]=='r') {
        for (int r = 0; r < TERM_ROWS; r++) {
            for(int c=0; c<=TERM_COLS; c++) screen[r][c] = 0;
            screen_color[r] = COL_FG;
        }
        cur_row = 0; cur_col = 0;
    } else if (strlen(cmd) >= 5 && cmd[0]=='u' && cmd[1]=='n' && cmd[2]=='a' && cmd[3]=='m' && cmd[4]=='e') {
        term_puts("YamOS v0.3.0 x86_64 (Ring 3)", COL_PROMPT); term_putchar('\n', COL_FG);
    } else if (strlen(cmd) >= 4 && cmd[0]=='e' && cmd[1]=='x' && cmd[2]=='i' && cmd[3]=='t') {
        exit(0);
    } else {
        term_puts(cmd, COL_ERR); term_puts(": command not found", COL_ERR); term_putchar('\n', COL_FG);
    }
    input_len = 0;
}

static void show_prompt(void) {
    term_puts("root@yam", COL_PROMPT);
    term_puts(":~$ ", COL_FG);
}

void _start(void) {
    i32 sid = wl_create_surface("Terminal", 50, 50, TERM_W, TERM_H);
    if (sid < 0) exit(1);

    void *buffer_vaddr = (void *)0x20000000;
    if (wl_map_buffer(sid, buffer_vaddr) < 0) exit(2);

    wl_user_buffer_t buf = { .pixels = (u32 *)buffer_vaddr, .width = TERM_W, .height = TERM_H };

    for(int r=0; r<TERM_ROWS; r++) screen_color[r] = COL_FG;
    term_puts("YamOS Terminal v0.3 (Ring 3)", COL_PROMPT); term_putchar('\n', COL_FG);
    term_puts("Isolation Active.", COL_FG); term_putchar('\n', COL_FG);
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
                    if (input_len > 0) { input_len--; term_putchar('\b', COL_FG); }
                } else if (c >= 32 && c < 127 && input_len < 126) {
                    input_buf[input_len++] = c;
                    term_putchar(c, COL_FG);
                }
            }
        }

        /* Draw */
        wl_user_draw_rect(&buf, 0, 0, TERM_W, TERM_H, COL_BG);
        for (int r = 0; r < TERM_ROWS; r++) {
            if (screen[r][0] != 0 || r == cur_row) {
                wl_user_draw_text(&buf, 0, r * 16, screen[r], screen_color[r]);
            }
        }
        /* Cursor */
        static int blink = 0;
        if ((++blink / 15) & 1) wl_user_draw_rect(&buf, cur_col * 8, cur_row * 16, 8, 16, COL_CURSOR);

        wl_commit(sid);
        sleep_ms(33);
    }
}
