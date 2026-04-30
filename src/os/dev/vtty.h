/* ============================================================================
 * YamKernel — Virtual TTY Engine (C7: Alt+F1..F6 Virtual Consoles)
 * Each VTY has its own screen buffer, scrollback, and input ring.
 * ============================================================================ */
#pragma once
#include <nexus/types.h>

#define VTTY_COUNT       6
#define VTTY_COLS        80
#define VTTY_ROWS        25
#define VTTY_SCROLLBACK  200   /* lines of scrollback per VTY */
#define VTTY_INPUT_RING  512

typedef struct {
    /* Screen buffer: char + attribute per cell */
    u8   screen[VTTY_ROWS][VTTY_COLS];     /* character */
    u8   attrib[VTTY_ROWS][VTTY_COLS];     /* foreground/background color attribute */
    int  cursor_col;
    int  cursor_row;

    /* Scrollback */
    u8   scrollback[VTTY_SCROLLBACK][VTTY_COLS];
    int  scroll_lines;  /* total lines in scrollback */
    int  scroll_view;   /* lines scrolled back from bottom (0 = current) */

    /* Input ring buffer */
    u8   input_buf[VTTY_INPUT_RING];
    int  input_head;
    int  input_tail;

    /* State */
    bool active;
    bool escape_seq;    /* inside ANSI escape */
    char esc_buf[16];
    int  esc_len;

    /* Colors */
    u8   fg;  /* current foreground color index */
    u8   bg;  /* current background color index */
} vtty_t;

/* Initialize the VTY system */
void vtty_init(void);

/* Switch the active VTY (0–5) */
void vtty_switch(int id);

/* Get active VTY ID */
int  vtty_active(void);

/* Write a character to a VTY's buffer */
void vtty_write_char(int id, char c);

/* Write a string (UTF-8 subset, ANSI escapes) to a VTY */
void vtty_write(int id, const char *s, usize len);

/* Push a raw keystroke into a VTY's input ring */
void vtty_input_char(int id, u8 c);

/* Read one character from a VTY's input ring (-1 if empty) */
int  vtty_read_char(int id);

/* Render the active VTY to the framebuffer */
void vtty_render(void);

/* Scroll the view of a VTY up by n lines */
void vtty_scroll_up(int id, int n);
void vtty_scroll_down(int id, int n);

/* Clear a VTY screen */
void vtty_clear(int id);

/* Return pointer to VTY struct (for direct access) */
vtty_t *vtty_get(int id);
