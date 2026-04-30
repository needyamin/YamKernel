/* ============================================================================
 * YamKernel — Virtual TTY Engine Implementation
 * 6 independent terminal sessions, Alt+F1..F6 switching, ANSI escapes.
 * ============================================================================ */
#include "vtty.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"
#include "../../drivers/video/framebuffer.h"

static vtty_t g_vttys[VTTY_COUNT];
int g_active_vtty = 0;

/* ---- ANSI color palette (16 VGA colors as ARGB) ---- */
static const u32 g_ansi_colors[16] = {
    0xFF1E1E2E, /* 0: black (Catppuccin base) */
    0xFFFF5555, /* 1: red */
    0xFF50FA7B, /* 2: green */
    0xFFF1FA8C, /* 3: yellow */
    0xFF8BE9FD, /* 4: blue */
    0xFFFF79C6, /* 5: magenta */
    0xFF8BE9FD, /* 6: cyan */
    0xFFF8F8F2, /* 7: white */
    0xFF6272A4, /* 8: bright black */
    0xFFFF6E6E, /* 9: bright red */
    0xFF69FF94, /* 10: bright green */
    0xFFFFFFA5, /* 11: bright yellow */
    0xFFD6ACFF, /* 12: bright blue */
    0xFFFF92DF, /* 13: bright magenta */
    0xFFA4FFFF, /* 14: bright cyan */
    0xFFFFFFFF, /* 15: bright white */
};

/* Font: simple 8x8 bitmap font (we reuse the kernel's framebuffer font) */
extern void fb_draw_char_at(u32 *pixels, u32 pitch_pixels, int x, int y,
                             char c, u32 fg, u32 bg);
extern u32  fb_get_width(void);
extern u32  fb_get_height(void);
extern u32 *fb_get_pixels(void);

#define CELL_W 8
#define CELL_H 16

/* ---- Internal helpers ---- */

static void vtty_scroll_up_internal(vtty_t *v) {
    /* Save row 0 to scrollback */
    if (v->scroll_lines < VTTY_SCROLLBACK) {
        memcpy(v->scrollback[v->scroll_lines], v->screen[0], VTTY_COLS);
        v->scroll_lines++;
    } else {
        /* Shift scrollback */
        for (int i = 0; i < VTTY_SCROLLBACK - 1; i++)
            memcpy(v->scrollback[i], v->scrollback[i+1], VTTY_COLS);
        memcpy(v->scrollback[VTTY_SCROLLBACK-1], v->screen[0], VTTY_COLS);
    }
    /* Scroll screen up */
    for (int r = 0; r < VTTY_ROWS - 1; r++) {
        memcpy(v->screen[r], v->screen[r+1], VTTY_COLS);
        memcpy(v->attrib[r], v->attrib[r+1], VTTY_COLS);
    }
    memset(v->screen[VTTY_ROWS-1], ' ', VTTY_COLS);
    memset(v->attrib[VTTY_ROWS-1], 0x07, VTTY_COLS); /* white on black */
}

static void vtty_handle_escape(vtty_t *v, char c) {
    /* Store escape characters */
    if (v->esc_len < 15) v->esc_buf[v->esc_len++] = c;
    if (c >= '@' && c <= '~') {
        /* End of escape sequence */
        v->escape_seq = false;
        /* Parse simple cases: \033[H = home, \033[2J = clear, \033[Xm = color */
        if (c == 'H' && v->esc_len == 2) {
            v->cursor_row = 0; v->cursor_col = 0;
        } else if (c == 'J' && v->esc_len >= 2 && v->esc_buf[0] == '[') {
            vtty_clear((int)(v - g_vttys));
        } else if (c == 'm') {
            /* Color attribute — simple parsing */
            int n = 0;
            for (int i = 1; i < v->esc_len - 1; i++) {
                if (v->esc_buf[i] >= '0' && v->esc_buf[i] <= '9')
                    n = n * 10 + (v->esc_buf[i] - '0');
            }
            if (n == 0) { v->fg = 7; v->bg = 0; }
            else if (n >= 30 && n <= 37) v->fg = (u8)(n - 30);
            else if (n >= 40 && n <= 47) v->bg = (u8)(n - 40);
            else if (n >= 90 && n <= 97) v->fg = (u8)(n - 90 + 8);
            else if (n == 1) v->fg |= 8; /* bold = bright */
        } else if (c == 'A') { /* cursor up */
            if (v->cursor_row > 0) v->cursor_row--;
        } else if (c == 'B') { /* cursor down */
            if (v->cursor_row < VTTY_ROWS-1) v->cursor_row++;
        } else if (c == 'C') { /* cursor right */
            if (v->cursor_col < VTTY_COLS-1) v->cursor_col++;
        } else if (c == 'D') { /* cursor left */
            if (v->cursor_col > 0) v->cursor_col--;
        }
        v->esc_len = 0;
    }
}

/* ---- Public API ---- */

void vtty_init(void) {
    memset(g_vttys, 0, sizeof(g_vttys));
    for (int i = 0; i < VTTY_COUNT; i++) {
        g_vttys[i].active = true;
        g_vttys[i].fg = 7;
        g_vttys[i].bg = 0;
        g_vttys[i].cursor_col = 0;
        g_vttys[i].cursor_row = 0;
        /* Fill with spaces */
        for (int r = 0; r < VTTY_ROWS; r++) {
            memset(g_vttys[i].screen[r], ' ', VTTY_COLS);
            memset(g_vttys[i].attrib[r], 0x07, VTTY_COLS);
        }
    }
    g_active_vtty = 0;

    /* Print welcome to each tty */
    const char *welcome = "YamOS Virtual Console — type 'help' for commands\n";
    for (int i = 0; i < VTTY_COUNT; i++) {
        vtty_write(i, welcome, strlen(welcome));
    }

    kprintf_color(0xFF00FF88, "[VTTY] %d virtual consoles initialized (Alt+F1..F6)\n", VTTY_COUNT);
}

void vtty_switch(int id) {
    if (id < 0 || id >= VTTY_COUNT) return;
    g_active_vtty = id;
    /* Signal compositor to suspend GUI and show VTY */
    kprintf_color(0xFF8BE9FD, "[VTTY] Switched to tty%d\n", id);
    vtty_render();
}

int vtty_active(void) { return g_active_vtty; }

void vtty_write_char(int id, char c) {
    if (id < 0 || id >= VTTY_COUNT) return;
    vtty_t *v = &g_vttys[id];

    /* Handle escape sequences */
    if (v->escape_seq) { vtty_handle_escape(v, c); return; }
    if (c == '\033') { v->escape_seq = true; v->esc_len = 0; return; }

    switch (c) {
        case '\n':
            v->cursor_row++;
            v->cursor_col = 0;
            break;
        case '\r':
            v->cursor_col = 0;
            break;
        case '\b':
            if (v->cursor_col > 0) {
                v->cursor_col--;
                v->screen[v->cursor_row][v->cursor_col] = ' ';
                v->attrib[v->cursor_row][v->cursor_col] = (v->bg << 4) | v->fg;
            }
            return;
        case '\t':
            v->cursor_col = (v->cursor_col + 8) & ~7;
            if (v->cursor_col >= VTTY_COLS) { v->cursor_col = 0; v->cursor_row++; }
            break;
        default:
            if (c >= 0x20) {
                v->screen[v->cursor_row][v->cursor_col] = (u8)c;
                v->attrib[v->cursor_row][v->cursor_col] = (v->bg << 4) | v->fg;
                v->cursor_col++;
                if (v->cursor_col >= VTTY_COLS) { v->cursor_col = 0; v->cursor_row++; }
            }
            break;
    }
    /* Handle scroll */
    if (v->cursor_row >= VTTY_ROWS) {
        vtty_scroll_up_internal(v);
        v->cursor_row = VTTY_ROWS - 1;
    }
}

void vtty_write(int id, const char *s, usize len) {
    for (usize i = 0; i < len; i++) vtty_write_char(id, s[i]);
}

void vtty_input_char(int id, u8 c) {
    if (id < 0 || id >= VTTY_COUNT) return;
    vtty_t *v = &g_vttys[id];
    int next = (v->input_head + 1) % VTTY_INPUT_RING;
    if (next != v->input_tail) {
        v->input_buf[v->input_head] = c;
        v->input_head = next;
    }
}

int vtty_read_char(int id) {
    if (id < 0 || id >= VTTY_COUNT) return -1;
    vtty_t *v = &g_vttys[id];
    if (v->input_head == v->input_tail) return -1;
    u8 c = v->input_buf[v->input_tail];
    v->input_tail = (v->input_tail + 1) % VTTY_INPUT_RING;
    return (int)c;
}

void vtty_render(void) {
    u32 *pixels = fb_get_pixels();
    u32  width  = fb_get_width();
    u32  height = fb_get_height();
    if (!pixels || width == 0 || height == 0) return;

    vtty_t *v = &g_vttys[g_active_vtty];

    /* Clear background */
    u32 bg_color = g_ansi_colors[0]; /* Catppuccin dark base */
    for (u32 y = 0; y < height; y++)
        for (u32 x = 0; x < width; x++)
            pixels[y * width + x] = bg_color;

    /* Draw characters */
    for (int row = 0; row < VTTY_ROWS && row * CELL_H < (int)height; row++) {
        for (int col = 0; col < VTTY_COLS && col * CELL_W < (int)width; col++) {
            char ch = (char)v->screen[row][col];
            u8 attr = v->attrib[row][col];
            u32 fg = g_ansi_colors[attr & 0x0F];
            u32 bg_idx = (attr >> 4) & 0x0F;
            u32 bg = g_ansi_colors[bg_idx];
            fb_draw_char_at(pixels, width, col * CELL_W, row * CELL_H, ch, fg, bg);
        }
    }

    /* Draw cursor (blinking block) */
    int cx = v->cursor_col * CELL_W;
    int cy = v->cursor_row * CELL_H;
    for (int y = cy; y < cy + CELL_H && y < (int)height; y++)
        for (int x = cx; x < cx + CELL_W && x < (int)width; x++)
            pixels[y * width + x] ^= 0x00FFFFFF; /* XOR invert */

    /* Status bar: show active tty */
    char status[80];
    ksnprintf(status, 80, " YamOS tty%d — Alt+F1..F6 to switch ", g_active_vtty + 1);
    /* Draw status at bottom */
    for (int col = 0; col < VTTY_COLS && col < (int)(width / CELL_W); col++) {
        char c = (col < (int)strlen(status)) ? status[col] : ' ';
        fb_draw_char_at(pixels, width, col * CELL_W,
                        (int)height - CELL_H, c, 0xFF1E1E2E, 0xFF8BE9FD);
    }
}

void vtty_scroll_up(int id, int n) {
    if (id < 0 || id >= VTTY_COUNT) return;
    for (int i = 0; i < n; i++) vtty_scroll_up_internal(&g_vttys[id]);
}

void vtty_scroll_down(int id, int n) {
    /* Scroll view back toward present */
    vtty_t *v = &g_vttys[id];
    v->scroll_view -= n;
    if (v->scroll_view < 0) v->scroll_view = 0;
}

void vtty_clear(int id) {
    if (id < 0 || id >= VTTY_COUNT) return;
    vtty_t *v = &g_vttys[id];
    for (int r = 0; r < VTTY_ROWS; r++) {
        memset(v->screen[r], ' ', VTTY_COLS);
        memset(v->attrib[r], 0x07, VTTY_COLS);
    }
    v->cursor_row = 0; v->cursor_col = 0;
}

vtty_t *vtty_get(int id) {
    if (id < 0 || id >= VTTY_COUNT) return NULL;
    return &g_vttys[id];
}
