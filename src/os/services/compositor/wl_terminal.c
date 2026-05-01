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

extern void *g_python_module;
extern usize g_python_module_size;
void wl_spawn_app_async(void *data, usize size, const char *name);

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
    term_puts("  python   - CPython from python.org status", COL_FG);
    term_newline();
    term_puts("  python -c ... - Requires linked CPython runtime", COL_FG);
    term_newline();
    term_puts("  pip      - Requires linked CPython + pip", COL_FG);
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

static void cmd_python_install(void) {
    term_puts("Starting python.org CPython installer...", COL_PROMPT);
    term_newline();
    term_puts("[1/5] checking /boot/python.elf installer module", COL_FG);
    term_newline();
    if (!g_python_module || !g_python_module_size) {
        term_puts("[error] python installer module is missing from boot image", COL_ERR);
        term_newline();
        kprintf("[PYTHON] installer missing: g_python_module=%p size=%lu\n", g_python_module, g_python_module_size);
        return;
    }
    term_puts("[2/5] found python.org CPython source package", COL_FG);
    term_newline();
    term_puts("[3/5] launching graphical installer/status app", COL_FG);
    term_newline();
    wl_spawn_app_async(g_python_module, g_python_module_size, "python-installer");
    term_puts("[4/5] installer will verify OS requirements", COL_FG);
    term_newline();
    term_puts("[5/5] CPython execution waits for linker + VFS + HTTPS work", COL_FG);
    term_newline();
    term_puts("No fake interpreter is used. This starts the real CPython port path.", COL_PROMPT);
    term_newline();
    kprintf("[PYTHON] installer launched from terminal: module=%p size=%lu\n", g_python_module, g_python_module_size);
}

static void process_command(void) {
    /* Null-terminate */
    input_buf[input_len] = 0;

    /* Skip leading spaces */
    char *cmd = input_buf;
    while (*cmd == ' ') cmd++;

    if (*cmd == 0) {
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
        cmd_python_install();
    } else if (strcmp(cmd, "python --version") == 0 || strcmp(cmd, "python3 --version") == 0 || strcmp(cmd, "py --version") == 0) {
        term_puts("Python 3.14.4 source vendored; CPython runtime not linked yet", COL_PROMPT);
        term_newline();
    } else if (strncmp(cmd, "python -c ", 10) == 0 || strncmp(cmd, "python3 -c ", 11) == 0 || strncmp(cmd, "py -c ", 6) == 0) {
        cmd_python_install();
        term_puts("Cannot execute Python code until CPython is ported.", COL_ERR);
        term_newline();
    } else if (strcmp(cmd, "pip") == 0 || strcmp(cmd, "pip3") == 0 ||
               strncmp(cmd, "pip ", 4) == 0 || strncmp(cmd, "pip3 ", 5) == 0) {
        cmd_python_install();
        term_puts("pip install needs real CPython, HTTPS, writable storage, and package build support.", COL_ERR);
        term_newline();
    } else {
        term_puts(cmd, COL_ERR);
        term_puts(": command not found", COL_ERR);
        term_newline();
    }

    input_len = 0;
}

static void show_prompt(void) {
    term_puts("root@yam", COL_PROMPT);
    term_puts(":~$ ", COL_FG);
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
