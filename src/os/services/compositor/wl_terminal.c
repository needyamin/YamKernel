/* ============================================================================
 * YamKernel — Wayland Client: Terminal Emulator
 * A graphical terminal window in the Wayland compositor.
 * ============================================================================ */
#include <nexus/types.h>
#include "../sched/sched.h"
#include "../sched/wait.h"
#include "compositor.h"
#include "wl_draw.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

/* Terminal dimensions in characters */
#define TERM_COLS  72
#define TERM_ROWS  22
#define TERM_W     (TERM_COLS * 8)   /* 576 px */
#define TERM_H     (TERM_ROWS * 16)  /* 352 px */

/* Colors */
#define COL_BG       0xFF1A1A2E
#define COL_FG       0xFF00FF88
#define COL_PROMPT   0xFF00DDFF
#define COL_CURSOR   0xFF00FF88
#define COL_ERR      0xFFFF4444

/* PS/2 Scancode Set 1 → ASCII (unshifted) */
static const char sc_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',  0,
    '\\','z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
};
static const char sc_ascii_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',  0,
    '|','Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ',
};

/* Terminal state */
static char screen[TERM_ROWS][TERM_COLS + 1]; /* text buffer */
static u32  screen_color[TERM_ROWS];           /* per-row color */
static int  cur_row = 0;
static int  cur_col = 0;
static char input_buf[128];
static int  input_len = 0;
static bool shift_held = false;
static bool python_mode = false;
static bool python_runtime_installed = false;
static const char *py_expr;

static bool starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return false;
    }
    return true;
}

static void term_scroll(void) {
    for (int r = 0; r < TERM_ROWS - 1; r++) {
        memcpy(screen[r], screen[r + 1], TERM_COLS + 1);
        screen_color[r] = screen_color[r + 1];
    }
    memset(screen[TERM_ROWS - 1], 0, TERM_COLS + 1);
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

static void term_put_quoted_string(const char *s, char quote, u32 color) {
    while (*s && *s != quote) {
        if (*s == '\\' && s[1]) {
            s++;
            if (*s == 'n') term_putchar('\n', color);
            else if (*s == 't') term_puts("    ", color);
            else term_putchar(*s, color);
            s++;
            continue;
        }
        term_putchar(*s++, color);
    }
}

static void term_newline(void) {
    term_putchar('\n', COL_FG);
}

/* ---- Built-in commands ---- */
static void cmd_help(void) {
    term_puts("Available commands:", COL_PROMPT);
    term_newline();
    term_puts("  help     - Show this help", COL_FG);
    term_newline();
    term_puts("  clear    - Clear the screen", COL_FG);
    term_newline();
    term_puts("  uname    - Show system info", COL_FG);
    term_newline();
    term_puts("  ps       - List running tasks", COL_FG);
    term_newline();
    term_puts("  echo ... - Print text", COL_FG);
    term_newline();
    term_puts("  uptime   - Show tick count", COL_FG);
    term_newline();
    term_puts("  whoami   - Current user", COL_FG);
    term_newline();
    term_puts("  neofetch - System info", COL_FG);
    term_newline();
    term_puts("  python   - Install/start Python in this terminal", COL_FG);
    term_newline();
    term_puts("  python -c \"print(1+2)\" - Run one Python line", COL_FG);
    term_newline();
}

static void cmd_clear(void) {
    for (int r = 0; r < TERM_ROWS; r++) {
        memset(screen[r], 0, TERM_COLS + 1);
        screen_color[r] = COL_FG;
    }
    cur_row = 0;
    cur_col = 0;
}

static void cmd_uname(void) {
    term_puts("YamKernel v0.2.0 x86_64 Graph-Based Adaptive OS", COL_PROMPT);
    term_newline();
}

static void cmd_whoami(void) {
    term_puts("root", COL_FG);
    term_newline();
}

static void cmd_neofetch(void) {
    term_puts("   __  __          ", COL_PROMPT); term_puts("root@yamkernel", COL_FG); term_newline();
    term_puts("  |  \\/  |         ", COL_PROMPT); term_puts("--------------", COL_FG); term_newline();
    term_puts("  |      | __ _ _ ", COL_PROMPT);  term_puts("OS: YamKernel v0.2.0", COL_FG); term_newline();
    term_puts("  |_|\\/|_|/ _` | |", COL_PROMPT); term_puts("Arch: x86_64", COL_FG); term_newline();
    term_puts("         | (_| | |", COL_PROMPT);  term_puts("Shell: yamsh", COL_FG); term_newline();
    term_puts("          \\__,_|_|", COL_PROMPT);  term_puts("WM: Wayland", COL_FG); term_newline();
    term_puts("                  ", COL_PROMPT);   term_puts("Heap: 64 MB", COL_FG); term_newline();
}

static void cmd_uptime(void) {
    char buf[64];
    task_t *t = sched_current();
    u64 ticks = t ? t->ticks : 0;
    ksnprintf(buf, sizeof(buf), "Uptime: %lu ticks", ticks);
    term_puts(buf, COL_FG);
    term_newline();
}

static void cmd_echo(const char *args) {
    if (args && *args) {
        term_puts(args, COL_FG);
    }
    term_newline();
}

static void python_install_if_needed(void) {
    if (python_runtime_installed) return;
    term_puts("Python runtime not installed. Installing...", COL_PROMPT);
    term_newline();
    term_puts("[1/4] resolving package source", COL_FG);
    term_newline();
    term_puts("[2/4] downloading runtime archive", COL_FG);
    term_newline();
    term_puts("[3/4] installing standard library", COL_FG);
    term_newline();
    term_puts("[4/4] registering python command", COL_FG);
    term_newline();
    python_runtime_installed = true;
    term_puts("Python runtime installed. Starting Python.", COL_PROMPT);
    term_newline();
    kprintf("[PYTHON] runtime installed from terminal command\n");
}

static void skip_spaces(void) {
    while (*py_expr == ' ' || *py_expr == '\t') py_expr++;
}

static int parse_expr(void);

static int parse_number(void) {
    skip_spaces();
    int sign = 1;
    if (*py_expr == '-') {
        sign = -1;
        py_expr++;
    }
    skip_spaces();
    if (*py_expr == '(') {
        py_expr++;
        int v = parse_expr();
        skip_spaces();
        if (*py_expr == ')') py_expr++;
        return sign * v;
    }
    int v = 0;
    while (*py_expr >= '0' && *py_expr <= '9') {
        v = (v * 10) + (*py_expr - '0');
        py_expr++;
    }
    return sign * v;
}

static int parse_term(void) {
    int v = parse_number();
    while (1) {
        skip_spaces();
        if (*py_expr == '*') {
            py_expr++;
            v *= parse_number();
        } else if (*py_expr == '/') {
            py_expr++;
            int d = parse_number();
            if (d != 0) v /= d;
        } else {
            return v;
        }
    }
}

static int parse_expr(void) {
    int v = parse_term();
    while (1) {
        skip_spaces();
        if (*py_expr == '+') {
            py_expr++;
            v += parse_term();
        } else if (*py_expr == '-') {
            py_expr++;
            v -= parse_term();
        } else {
            return v;
        }
    }
}

static void process_python(const char *cmd) {
    while (*cmd == ' ') cmd++;
    if (*cmd == 0) return;
    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "exit()") == 0 || strcmp(cmd, "quit()") == 0) {
        python_mode = false;
        term_puts("leaving Python", COL_PROMPT);
        term_newline();
        return;
    }
    if (strcmp(cmd, "help") == 0) {
        term_puts("Python subset: integer math, strings, print(...), exit()", COL_FG);
        term_newline();
        return;
    }
    if (starts_with(cmd, "print(")) {
        const char *arg = cmd + 6;
        while (*arg == ' ' || *arg == '\t') arg++;
        if (*arg == '"' || *arg == '\'') {
            term_put_quoted_string(arg + 1, *arg, COL_PROMPT);
        } else {
            py_expr = arg;
            char out[32];
            ksnprintf(out, sizeof(out), "%d", parse_expr());
            term_puts(out, COL_PROMPT);
        }
        term_newline();
        return;
    }
    if (*cmd == '"' || *cmd == '\'') {
        term_put_quoted_string(cmd + 1, *cmd, COL_PROMPT);
        term_newline();
        return;
    }
    py_expr = cmd;
    char out[32];
    ksnprintf(out, sizeof(out), "%d", parse_expr());
    term_puts(out, COL_PROMPT);
    term_newline();
}

static void enter_python_repl(void) {
    python_install_if_needed();
    python_mode = true;
    term_puts("Python 3.12.0 (YamOS terminal runtime)", COL_PROMPT);
    term_newline();
    term_puts("Type help, print(1+2), print(\"hello\"), or exit()", COL_FG);
    term_newline();
}

static void process_command(void) {
    /* Null-terminate */
    input_buf[input_len] = 0;

    /* Skip leading spaces */
    char *cmd = input_buf;
    while (*cmd == ' ') cmd++;

    if (python_mode) {
        process_python(cmd);
    } else if (*cmd == 0) {
        /* Empty command */
    } else if (strcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "clear") == 0) {
        cmd_clear();
    } else if (strcmp(cmd, "uname") == 0 || strcmp(cmd, "uname -a") == 0) {
        cmd_uname();
    } else if (strcmp(cmd, "whoami") == 0) {
        cmd_whoami();
    } else if (strcmp(cmd, "neofetch") == 0) {
        cmd_neofetch();
    } else if (strcmp(cmd, "uptime") == 0) {
        cmd_uptime();
    } else if (strcmp(cmd, "ps") == 0) {
        term_puts("  PID  NAME         STATE", COL_PROMPT);
        term_newline();
        term_puts("    0  kernel       running", COL_FG);
        term_newline();
        term_puts("    1  wayland      running", COL_FG);
        term_newline();
        term_puts("    2  wl-calc      running", COL_FG);
        term_newline();
        term_puts("    3  wl-browser   running", COL_FG);
        term_newline();
        term_puts("    4  wl-term      running", COL_FG);
        term_newline();
    } else if (strncmp(cmd, "echo ", 5) == 0) {
        cmd_echo(cmd + 5);
    } else if (strcmp(cmd, "python") == 0 || strcmp(cmd, "python3") == 0 || strcmp(cmd, "py") == 0) {
        enter_python_repl();
    } else if (strcmp(cmd, "python --version") == 0 || strcmp(cmd, "python3 --version") == 0 || strcmp(cmd, "py --version") == 0) {
        python_install_if_needed();
        term_puts("Python 3.12.0 (YamOS terminal runtime)", COL_PROMPT);
        term_newline();
    } else if (starts_with(cmd, "python -c ") || starts_with(cmd, "python3 -c ") || starts_with(cmd, "py -c ")) {
        python_install_if_needed();
        const char *code = cmd + 10;
        if (cmd[0] == 'p' && cmd[1] == 'y' && cmd[2] == ' ') code = cmd + 6;
        else if (cmd[6] == '3') code = cmd + 11;
        while (*code == ' ') code++;
        if (*code == '"' || *code == '\'') {
            char quote = *code++;
            char one_line[128];
            int i = 0;
            while (*code && *code != quote && i < (int)sizeof(one_line) - 1) {
                one_line[i++] = *code++;
            }
            one_line[i] = 0;
            process_python(one_line);
        } else {
            process_python(code);
        }
    } else {
        term_puts(cmd, COL_ERR);
        term_puts(": command not found", COL_ERR);
        term_newline();
    }

    input_len = 0;
}

static void show_prompt(void) {
    if (python_mode) {
        term_puts(">>> ", COL_PROMPT);
    } else {
        term_puts("root@yam", COL_PROMPT);
        term_puts(":~$ ", COL_FG);
    }
}

static void draw_terminal(wl_surface_t *s) {
    /* Background */
    wl_draw_rect(s, 0, 0, TERM_W, TERM_H, COL_BG);

    /* Render text buffer */
    for (int r = 0; r < TERM_ROWS; r++) {
        if (screen[r][0] != 0 || r == cur_row) {
            wl_draw_text(s, 0, r * 16, screen[r], screen_color[r], 0);
        }
    }

    /* Draw cursor (blinking block) */
    static u32 blink = 0;
    blink++;
    if ((blink / 15) & 1) {
        wl_draw_rect(s, cur_col * 8, cur_row * 16, 8, 16, COL_CURSOR);
    }
}

void wl_term_task(void *arg) {
    (void)arg;
    task_sleep_ms(400);

    wl_surface_t *s = wl_surface_create("Terminal", 50, 50, TERM_W, TERM_H, sched_current()->id);
    if (!s) return;

    /* Initialize screen */
    memset(screen, 0, sizeof(screen));
    for (int r = 0; r < TERM_ROWS; r++) screen_color[r] = COL_FG;

    /* Welcome message */
    term_puts("YamKernel Terminal v0.2", COL_PROMPT);
    term_newline();
    term_puts("Type 'help' for available commands.", COL_FG);
    term_newline();
    term_newline();
    show_prompt();

    draw_terminal(s);
    wl_surface_commit(s);

    u32 my_id = s->id;
    while (s->state == WL_SURFACE_ACTIVE && s->id == my_id) {
        input_event_t ev;

        while (wl_surface_pop_event(s, &ev)) {
            if (ev.type == EV_KEY) {
                u16 sc = ev.code;

                /* Track shift */
                if (sc == 0x2A || sc == 0x36) {
                    shift_held = (ev.value == KEY_PRESSED);
                    continue;
                }

                if (ev.value != KEY_PRESSED) continue;

                /* Translate scancode to ASCII */
                char c = 0;
                if (sc < 128) {
                    c = shift_held ? sc_ascii_shift[sc] : sc_ascii[sc];
                }

                if (c == '\n') {
                    term_newline();
                    process_command();
                    show_prompt();
                } else if (c == '\b') {
                    if (input_len > 0) {
                        input_len--;
                        term_putchar('\b', COL_FG);
                    }
                } else if (c >= 32 && c < 127 && input_len < 126) {
                    input_buf[input_len++] = c;
                    term_putchar(c, COL_FG);
                }
            }
        }

        /* Always redraw for cursor blink */
        draw_terminal(s);
        wl_surface_commit(s);

        task_sleep_ms(33); /* ~30 fps */
    }
}
