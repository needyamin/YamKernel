/* YamKernel - compositor-native browser shell with real plain-HTTP fetch */
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../net/net.h"
#include "../sched/sched.h"
#include "../sched/wait.h"
#include "compositor.h"
#include "wl_draw.h"
#include <nexus/types.h>

#define BROWSER_W 820
#define BROWSER_H 560
#define URL_MAX 160
#define PAGE_MAX 12288
#define TEXT_MAX 8192
#define HISTORY_MAX 12
#define RENDER_MAX 192
#define RENDER_TEXT 192
#define DOC_TOP 232
#define DOC_BOTTOM (BROWSER_H - 38)
#define DOC_VIEW_H (DOC_BOTTOM - DOC_TOP)
#define NODE_STYLE_FG 0x01
#define NODE_STYLE_BG 0x02

typedef enum { BR_READY = 0, BR_LOADING, BR_DONE, BR_ERROR } browser_state_t;

typedef enum {
  RN_P = 0,
  RN_H1,
  RN_H2,
  RN_H3,
  RN_LI,
  RN_LINK,
  RN_PRE,
  RN_IMG,
  RN_BUTTON,
  RN_FORM
} render_node_type_t;

typedef struct {
  render_node_type_t type;
  char text[RENDER_TEXT];
  u32 fg;
  u32 bg;
  u32 flags;
} render_node_t;

static char url_buffer[URL_MAX] = "http://ansnew.com/";
static char loaded_url[URL_MAX] = "http://ansnew.com/";
static char page_title[96] = "New Tab";
static char status_text[160] = "Ready";
static char response_meta[128] = "No page loaded";
static char raw_page[PAGE_MAX];
static char rendered_text[TEXT_MAX];
static render_node_t render_nodes[RENDER_MAX];
static u32 render_count = 0;
static char history[HISTORY_MAX][URL_MAX];
static int history_count = 0;
static int history_pos = -1;
static browser_state_t br_state = BR_READY;
static int progress = 0;
static bool shift_held = false;
static bool address_focus = true;
static i32 last_x = -1;
static i32 last_y = -1;
static i32 document_scroll = 0;
static i32 document_height = 0;

static const char sc_ascii[128] = {
    0,   27,  '1',  '2',  '3',  '4', '5', '6',  '7', '8', '9', '0',
    '-', '=', '\b', '\t', 'q',  'w', 'e', 'r',  't', 'y', 'u', 'i',
    'o', 'p', '[',  ']',  '\n', 0,   'a', 's',  'd', 'f', 'g', 'h',
    'j', 'k', 'l',  ';',  '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm',  ',',  '.',  '/', 0,   '*',  0,   ' ',
};

static const char sc_ascii_shift[128] = {
    0,   27,  '!',  '@',  '#',  '$', '%', '^', '&', '*', '(', ')',
    '_', '+', '\b', '\t', 'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{',  '}',  '\n', 0,   'A', 'S', 'D', 'F', 'G', 'H',
    'J', 'K', 'L',  ':',  '"',  '~', 0,   '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M',  '<',  '>',  '?', 0,   '*', 0,   ' ',
};

static void text_copy(char *dst, usize cap, const char *src) {
  if (!dst || cap == 0)
    return;
  dst[0] = 0;
  if (!src)
    return;
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = 0;
}

static bool starts_with(const char *s, const char *prefix) {
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

static char lower_ascii(char c) {
  if (c >= 'A' && c <= 'Z')
    return c + ('a' - 'A');
  return c;
}

static bool starts_with_ci(const char *s, const char *prefix) {
  while (*prefix) {
    if (lower_ascii(*s++) != lower_ascii(*prefix++))
      return false;
  }
  return true;
}

static bool match_n_ci(const char *a, const char *b, usize n) {
  for (usize i = 0; i < n; i++) {
    if (lower_ascii(a[i]) != lower_ascii(b[i]))
      return false;
  }
  return true;
}

static const char *find_ci(const char *haystack, const char *needle) {
  if (!haystack || !needle || !*needle)
    return NULL;
  usize n = strlen(needle);
  for (const char *p = haystack; *p; p++) {
    if (match_n_ci(p, needle, n))
      return p;
  }
  return NULL;
}

static void append_char(char *dst, usize cap, usize *pos, char c) {
  if (*pos + 1 >= cap)
    return;
  dst[(*pos)++] = c;
  dst[*pos] = 0;
}

static void append_text(char *dst, usize cap, usize *pos, const char *src) {
  while (src && *src && *pos + 1 < cap)
    append_char(dst, cap, pos, *src++);
}

static void append_small(char *dst, usize cap, usize *pos, char c) {
  if (*pos + 1 >= cap)
    return;
  dst[(*pos)++] = c;
  dst[*pos] = 0;
}

static bool tag_is(const char *s, const char *tag) {
  if (*s != '<')
    return false;
  s++;
  if (*s == '/')
    s++;
  usize n = strlen(tag);
  if (!match_n_ci(s, tag, n))
    return false;
  char end = s[n];
  return end == '>' || end == ' ' || end == '\t' || end == '/' || end == '\r' ||
         end == '\n';
}

static bool tag_open(const char *s, const char *tag) {
  if (*s != '<' || s[1] == '/')
    return false;
  usize n = strlen(tag);
  if (!match_n_ci(s + 1, tag, n))
    return false;
  char end = s[1 + n];
  return end == '>' || end == ' ' || end == '\t' || end == '/' || end == '\r' ||
         end == '\n';
}

static bool tag_close(const char *s, const char *tag) {
  if (!starts_with_ci(s, "</"))
    return false;
  usize n = strlen(tag);
  return match_n_ci(s + 2, tag, n);
}

static void normalize_url(char *inout) {
  char tmp[URL_MAX];
  if (!starts_with(inout, "http://") && !starts_with(inout, "https://")) {
    ksnprintf(tmp, sizeof(tmp), "http://%s", inout);
    text_copy(inout, URL_MAX, tmp);
  }
}

static bool parse_plain_http_url(const char *url, char *host, usize host_cap,
                                 char *path, usize path_cap) {
  if (!starts_with(url, "http://"))
    return false;
  const char *p = url + 7;
  usize h = 0;
  while (*p && *p != '/' && *p != ':' && h + 1 < host_cap)
    host[h++] = *p++;
  host[h] = 0;
  if (*p == ':')
    return false;
  if (*p == '/')
    text_copy(path, path_cap, p);
  else
    text_copy(path, path_cap, "/");
  return host[0] != 0;
}

static const char *body_start(const char *raw) {
  const char *p = strstr(raw, "\r\n\r\n");
  if (p)
    return p + 4;
  p = strstr(raw, "\n\n");
  if (p)
    return p + 2;
  p = strstr(raw, "<!doctype");
  if (p)
    return p;
  p = strstr(raw, "<!DOCTYPE");
  if (p)
    return p;
  p = strstr(raw, "<html");
  if (p)
    return p;
  p = strstr(raw, "<HTML");
  return p ? p : raw;
}

static const char *tag_end(const char *s) {
  while (*s && *s != '>')
    s++;
  return *s == '>' ? s : NULL;
}

static bool extract_attr_value(const char *tag, const char *name, char *out,
                               usize cap) {
  if (!tag || !name || !out || cap == 0)
    return false;
  out[0] = 0;
  const char *end = tag_end(tag);
  if (!end)
    return false;
  usize name_len = strlen(name);
  for (const char *p = tag; p < end; p++) {
    if (!match_n_ci(p, name, name_len))
      continue;
    const char *q = p + name_len;
    while (q < end && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n'))
      q++;
    if (q >= end || *q != '=')
      continue;
    q++;
    while (q < end && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n'))
      q++;
    char quote = 0;
    if (*q == '"' || *q == '\'')
      quote = *q++;
    usize n = 0;
    while (q < end && n + 1 < cap) {
      if (quote) {
        if (*q == quote)
          break;
      } else if (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n' ||
                 *q == '>') {
        break;
      }
      out[n++] = *q++;
    }
    out[n] = 0;
    return n > 0;
  }
  return false;
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static bool parse_css_color_value(const char *value, u32 *out) {
  if (!value || !out)
    return false;
  while (*value == ' ' || *value == '\t')
    value++;
  if (*value == '#') {
    value++;
    int h[6];
    for (int i = 0; i < 6; i++) {
      h[i] = hex_digit(value[i]);
      if (h[i] < 0)
        return false;
    }
    *out = 0xFF000000 | ((u32)(h[0] * 16 + h[1]) << 16) |
           ((u32)(h[2] * 16 + h[3]) << 8) | (u32)(h[4] * 16 + h[5]);
    return true;
  }
  if (starts_with_ci(value, "black")) {
    *out = 0xFF111827;
    return true;
  }
  if (starts_with_ci(value, "white")) {
    *out = 0xFFFFFFFF;
    return true;
  }
  if (starts_with_ci(value, "red")) {
    *out = 0xFFDC2626;
    return true;
  }
  if (starts_with_ci(value, "green")) {
    *out = 0xFF16A34A;
    return true;
  }
  if (starts_with_ci(value, "blue")) {
    *out = 0xFF2563EB;
    return true;
  }
  if (starts_with_ci(value, "gray") || starts_with_ci(value, "grey")) {
    *out = 0xFF64748B;
    return true;
  }
  if (starts_with_ci(value, "orange")) {
    *out = 0xFFF97316;
    return true;
  }
  if (starts_with_ci(value, "purple")) {
    *out = 0xFF7C3AED;
    return true;
  }
  return false;
}

static bool parse_css_decl_color(const char *style, const char *prop,
                                 u32 *out) {
  const char *p = find_ci(style, prop);
  if (!p)
    return false;
  p += strlen(prop);
  while (*p == ' ' || *p == '\t')
    p++;
  if (*p != ':')
    return false;
  p++;
  return parse_css_color_value(p, out);
}

static u32 readable_on(u32 bg) {
  u32 r = (bg >> 16) & 0xFF;
  u32 g = (bg >> 8) & 0xFF;
  u32 b = bg & 0xFF;
  u32 luma = r * 30 + g * 59 + b * 11;
  return luma > 15000 ? 0xFF111827 : 0xFFFFFFFF;
}

static void extract_node_style(const char *tag, u32 *fg, u32 *bg, u32 *flags) {
  char style[256];
  u32 c = 0;
  if (!extract_attr_value(tag, "style", style, sizeof(style)))
    return;
  if (parse_css_decl_color(style, "background-color", &c) ||
      parse_css_decl_color(style, "background", &c)) {
    *bg = c;
    *flags |= NODE_STYLE_BG;
  }
  if (parse_css_decl_color(style, "color", &c)) {
    *fg = c;
    *flags |= NODE_STYLE_FG;
  } else if (*flags & NODE_STYLE_BG) {
    *fg = readable_on(*bg);
    *flags |= NODE_STYLE_FG;
  }
}

static void extract_status_line(const char *raw) {
  char line[96];
  int i = 0;
  while (raw[i] && raw[i] != '\r' && raw[i] != '\n' &&
         i + 1 < (int)sizeof(line)) {
    line[i] = raw[i];
    i++;
  }
  line[i] = 0;
  text_copy(response_meta, sizeof(response_meta),
            line[0] ? line : "HTTP response received");
}

static int http_status_code(const char *raw) {
  const char *p = raw;
  while (*p && *p != ' ')
    p++;
  while (*p == ' ')
    p++;
  int code = 0;
  for (int i = 0; i < 3 && p[i] >= '0' && p[i] <= '9'; i++)
    code = code * 10 + (p[i] - '0');
  return code;
}

static bool extract_header_value(const char *raw, const char *name, char *out,
                                 usize cap) {
  if (!raw || !name || !out || cap == 0)
    return false;
  out[0] = 0;
  usize name_len = strlen(name);
  const char *p = raw;
  while (*p) {
    const char *line = p;
    const char *eol = strstr(p, "\n");
    usize line_len = eol ? (usize)(eol - line) : strlen(line);
    if (line_len > name_len && strncmp(line, name, name_len) == 0 &&
        line[name_len] == ':') {
      const char *v = line + name_len + 1;
      while (*v == ' ' || *v == '\t')
        v++;
      usize n = 0;
      while (n + 1 < cap && n < line_len && v[n] && v[n] != '\r' &&
             v[n] != '\n') {
        out[n] = v[n];
        n++;
      }
      out[n] = 0;
      return true;
    }
    if (!eol)
      break;
    p = eol + 1;
    if (*p == '\r' || *p == '\n')
      break;
  }
  return false;
}

static void extract_title(const char *body) {
  for (usize i = 0; body[i]; i++) {
    if (!tag_open(&body[i], "title"))
      continue;
    const char *start = tag_end(&body[i]);
    if (!start)
      break;
    start++;
    const char *end = start;
    while (*end && !tag_close(end, "title"))
      end++;
    if (end > start) {
      usize n = (usize)(end - start);
      if (n >= sizeof(page_title))
        n = sizeof(page_title) - 1;
      memcpy(page_title, start, n);
      page_title[n] = 0;
      return;
    }
  }
  text_copy(page_title, sizeof(page_title), loaded_url);
}

static void render_add_styled(render_node_type_t type, const char *text, u32 fg,
                              u32 bg, u32 flags) {
  if (!text || !*text || render_count >= RENDER_MAX)
    return;
  while (*text == ' ' || *text == '\n' || *text == '\t')
    text++;
  if (!*text)
    return;
  render_node_t *n = &render_nodes[render_count++];
  n->type = type;
  n->fg = fg;
  n->bg = bg;
  n->flags = flags;
  text_copy(n->text, sizeof(n->text), text);
}

static void render_add(render_node_type_t type, const char *text) {
  render_add_styled(type, text, 0, 0, 0);
}

static void render_flush_line(render_node_type_t type, char *line,
                              usize *line_pos, u32 fg, u32 bg, u32 flags) {
  if (*line_pos == 0)
    return;
  line[*line_pos] = 0;
  render_add_styled(type, line, fg, bg, flags);
  *line_pos = 0;
  line[0] = 0;
}

static void append_render_char(usize *pos, char c, int *line_col) {
  if (c == '\n') {
    append_char(rendered_text, sizeof(rendered_text), pos, '\n');
    *line_col = 0;
    return;
  }
  if (*line_col >= 88 && c == ' ') {
    append_char(rendered_text, sizeof(rendered_text), pos, '\n');
    *line_col = 0;
    return;
  }
  append_char(rendered_text, sizeof(rendered_text), pos, c);
  (*line_col)++;
}

static void html_to_text(const char *body) {
  usize pos = 0;
  bool in_tag = false;
  bool last_space = false;
  int line_col = 0;
  char line[RENDER_TEXT];
  usize line_pos = 0;
  render_node_type_t current_type = RN_P;
  u32 current_fg = 0;
  u32 current_bg = 0;
  u32 current_flags = 0;
  bool skip_script_style = false;
  rendered_text[0] = 0;
  render_count = 0;
  document_scroll = 0;
  document_height = 0;
  memset(render_nodes, 0, sizeof(render_nodes));
  line[0] = 0;

  if (starts_with(body, "HTTP/"))
    body = body_start(body);

  for (usize i = 0; body[i] && pos + 1 < sizeof(rendered_text); i++) {
    char c = body[i];
    if (skip_script_style) {
      if (tag_close(&body[i], "script") || tag_close(&body[i], "style"))
        skip_script_style = false;
      continue;
    }
    if (c == '&') {
      char ent = 0;
      if (starts_with(&body[i], "&amp;")) {
        ent = '&';
        i += 4;
      } else if (starts_with(&body[i], "&lt;")) {
        ent = '<';
        i += 3;
      } else if (starts_with(&body[i], "&gt;")) {
        ent = '>';
        i += 3;
      } else if (starts_with(&body[i], "&quot;")) {
        ent = '"';
        i += 5;
      } else if (starts_with(&body[i], "&#39;")) {
        ent = '\'';
        i += 4;
      }
      if (ent) {
        append_render_char(&pos, ent, &line_col);
        append_small(line, sizeof(line), &line_pos, ent);
        continue;
      }
    }
    if (c == '<') {
      if (tag_open(&body[i], "script") || tag_open(&body[i], "style")) {
        render_flush_line(current_type, line, &line_pos, current_fg, current_bg,
                          current_flags);
        skip_script_style = true;
      } else if (tag_open(&body[i], "img")) {
        char alt[RENDER_TEXT];
        char src[RENDER_TEXT];
        u32 fg = 0, bg = 0, flags = 0;
        extract_node_style(&body[i], &fg, &bg, &flags);
        if (extract_attr_value(&body[i], "alt", alt, sizeof(alt))) {
          render_add_styled(RN_IMG, alt, fg, bg, flags);
        } else if (extract_attr_value(&body[i], "src", src, sizeof(src))) {
          render_add_styled(RN_IMG, src, fg, bg, flags);
        } else {
          render_add_styled(RN_IMG, "Image", fg, bg, flags);
        }
      } else if (tag_open(&body[i], "input")) {
        char value[RENDER_TEXT];
        char placeholder[RENDER_TEXT];
        u32 fg = 0, bg = 0, flags = 0;
        extract_node_style(&body[i], &fg, &bg, &flags);
        if (extract_attr_value(&body[i], "value", value, sizeof(value)))
          render_add_styled(RN_FORM, value, fg, bg, flags);
        else if (extract_attr_value(&body[i], "placeholder", placeholder,
                                    sizeof(placeholder)))
          render_add_styled(RN_FORM, placeholder, fg, bg, flags);
        else
          render_add_styled(RN_FORM, "Input field", fg, bg, flags);
      } else if (tag_open(&body[i], "button")) {
        render_flush_line(current_type, line, &line_pos, current_fg, current_bg,
                          current_flags);
        current_type = RN_BUTTON;
        current_fg = current_bg = current_flags = 0;
        extract_node_style(&body[i], &current_fg, &current_bg, &current_flags);
      } else if (tag_open(&body[i], "h1")) {
        render_flush_line(current_type, line, &line_pos, current_fg, current_bg,
                          current_flags);
        current_type = RN_H1;
        current_fg = current_bg = current_flags = 0;
        extract_node_style(&body[i], &current_fg, &current_bg, &current_flags);
      } else if (tag_open(&body[i], "h2")) {
        render_flush_line(current_type, line, &line_pos, current_fg, current_bg,
                          current_flags);
        current_type = RN_H2;
        current_fg = current_bg = current_flags = 0;
        extract_node_style(&body[i], &current_fg, &current_bg, &current_flags);
      } else if (tag_open(&body[i], "h3")) {
        render_flush_line(current_type, line, &line_pos, current_fg, current_bg,
                          current_flags);
        current_type = RN_H3;
        current_fg = current_bg = current_flags = 0;
        extract_node_style(&body[i], &current_fg, &current_bg, &current_flags);
      } else if (tag_open(&body[i], "li")) {
        render_flush_line(current_type, line, &line_pos, current_fg, current_bg,
                          current_flags);
        current_type = RN_LI;
        current_fg = current_bg = current_flags = 0;
        extract_node_style(&body[i], &current_fg, &current_bg, &current_flags);
      } else if (tag_open(&body[i], "a")) {
        render_flush_line(current_type, line, &line_pos, current_fg, current_bg,
                          current_flags);
        current_type = RN_LINK;
        current_fg = current_bg = current_flags = 0;
        extract_node_style(&body[i], &current_fg, &current_bg, &current_flags);
      } else if (tag_open(&body[i], "pre") || tag_open(&body[i], "code")) {
        render_flush_line(current_type, line, &line_pos, current_fg, current_bg,
                          current_flags);
        current_type = RN_PRE;
        current_fg = current_bg = current_flags = 0;
        extract_node_style(&body[i], &current_fg, &current_bg, &current_flags);
      } else if (tag_open(&body[i], "p") || tag_open(&body[i], "div") ||
                 tag_open(&body[i], "section") ||
                 tag_open(&body[i], "article")) {
        render_flush_line(current_type, line, &line_pos, current_fg, current_bg,
                          current_flags);
        current_type = RN_P;
        current_fg = current_bg = current_flags = 0;
        extract_node_style(&body[i], &current_fg, &current_bg, &current_flags);
      }

      if (tag_is(&body[i], "br") || tag_is(&body[i], "p") ||
          tag_is(&body[i], "h1") || tag_is(&body[i], "h2") ||
          tag_is(&body[i], "h3") || tag_is(&body[i], "li") ||
          tag_close(&body[i], "div") || tag_close(&body[i], "section") ||
          tag_close(&body[i], "article")) {
        if (pos > 0 && rendered_text[pos - 1] != '\n')
          append_render_char(&pos, '\n', &line_col);
        if (tag_open(&body[i], "li"))
          append_text(rendered_text, sizeof(rendered_text), &pos, "- ");
      }
      if (tag_close(&body[i], "h1") || tag_close(&body[i], "h2") ||
          tag_close(&body[i], "h3") || tag_close(&body[i], "p") ||
          tag_close(&body[i], "li") || tag_close(&body[i], "a") ||
          tag_close(&body[i], "button") || tag_close(&body[i], "pre") ||
          tag_close(&body[i], "code")) {
        render_flush_line(current_type, line, &line_pos, current_fg, current_bg,
                          current_flags);
        current_type = RN_P;
        current_fg = current_bg = current_flags = 0;
      }
      in_tag = true;
      continue;
    }
    if (c == '>') {
      in_tag = false;
      continue;
    }
    if (in_tag)
      continue;
    if (c == '\r' || c == '\t')
      c = ' ';
    if (c == '\n') {
      append_render_char(&pos, '\n', &line_col);
      render_flush_line(current_type, line, &line_pos, current_fg, current_bg,
                        current_flags);
      last_space = false;
      continue;
    }
    if (c == ' ') {
      if (last_space)
        continue;
      last_space = true;
    } else {
      last_space = false;
    }
    append_render_char(&pos, c, &line_col);
    append_small(line, sizeof(line), &line_pos, c);
  }

  render_flush_line(current_type, line, &line_pos, current_fg, current_bg,
                    current_flags);
  if (pos == 0)
    append_text(rendered_text, sizeof(rendered_text), &pos,
                "(empty response body)");
  if (render_count == 0)
    render_add(RN_P, rendered_text);
}

static void history_push(const char *url) {
  if (!url || !*url)
    return;
  if (history_pos >= 0 && strcmp(history[history_pos], url) == 0)
    return;
  if (history_count < HISTORY_MAX) {
    history_pos = history_count++;
  } else {
    for (int i = 1; i < HISTORY_MAX; i++)
      text_copy(history[i - 1], sizeof(history[0]), history[i]);
    history_pos = HISTORY_MAX - 1;
  }
  text_copy(history[history_pos], sizeof(history[0]), url);
}

static void browser_fetch_current(void) {
  char host[96];
  char path[128];
  char location[URL_MAX];
  usize len = 0;
  int redirect_count = 0;

fetch_again:
  normalize_url(url_buffer);
  br_state = BR_LOADING;
  progress = 18;
  text_copy(status_text, sizeof(status_text), "Parsing address...");

  if (starts_with(url_buffer, "https://")) {
    br_state = BR_ERROR;
    text_copy(status_text, sizeof(status_text),
              "HTTPS needs full encrypted HTTP client. Try http://ansnew.com/");
    text_copy(rendered_text, sizeof(rendered_text),
              "YamBrowser cannot load HTTPS yet.\n\nImplemented now:\n- DNS\n- "
              "TCP\n- plain HTTP/1.0\n- TLS reachability probe\n- "
              "certificate-store bootstrap\n- basic HTML text "
              "extraction\n\nMissing for Mozilla-class browsing:\n- encrypted "
              "HTTPS stream\n- certificate chain validation\n- JavaScript\n- "
              "CSS layout\n- images\n- sandboxed multi-process renderer");
    return;
  }

  if (!parse_plain_http_url(url_buffer, host, sizeof(host), path,
                            sizeof(path))) {
    br_state = BR_ERROR;
    text_copy(status_text, sizeof(status_text), "Invalid HTTP URL");
    text_copy(rendered_text, sizeof(rendered_text),
              "Use a URL like http://ansnew.com/");
    return;
  }

  progress = 35;
  text_copy(status_text, sizeof(status_text),
            "Resolving DNS and connecting...");
  memset(raw_page, 0, sizeof(raw_page));
  int rc = http_get(host, path, raw_page, sizeof(raw_page), &len);
  if (rc < 0) {
    br_state = BR_ERROR;
    ksnprintf(status_text, sizeof(status_text), "Load failed rc=%d", rc);
    ksnprintf(
        rendered_text, sizeof(rendered_text),
        "Could not load %s\n\nNetwork requirements:\n- e1000 link up\n- DHCP "
        "lease\n- DNS server\n- TCP connect\n- plain HTTP server\n\nHTTPS "
        "pages need the next encrypted HTTP client layer.",
        url_buffer);
    kprintf("[BROWSER] load failed url='%s' rc=%d\n", url_buffer, rc);
    return;
  }

  progress = 86;
  extract_status_line(raw_page);
  int code = http_status_code(raw_page);
  if ((code == 301 || code == 302 || code == 303 || code == 307 ||
       code == 308) &&
      redirect_count < 2 &&
      extract_header_value(raw_page, "Location", location, sizeof(location))) {
    if (starts_with(location, "http://")) {
      redirect_count++;
      text_copy(url_buffer, sizeof(url_buffer), location);
      kprintf("[BROWSER] redirect %d -> %s\n", code, url_buffer);
      goto fetch_again;
    }
    if (starts_with(location, "/")) {
      char next[URL_MAX];
      ksnprintf(next, sizeof(next), "http://%s%s", host, location);
      redirect_count++;
      text_copy(url_buffer, sizeof(url_buffer), next);
      kprintf("[BROWSER] redirect %d -> %s\n", code, url_buffer);
      goto fetch_again;
    }
  }

  text_copy(loaded_url, sizeof(loaded_url), url_buffer);
  history_push(loaded_url);
  const char *body = body_start(raw_page);
  extract_title(body);
  html_to_text(body);
  if (code >= 400) {
    br_state = BR_ERROR;
    ksnprintf(status_text, sizeof(status_text),
              "Server returned HTTP %d; rendered response body", code);
  } else {
    br_state = BR_DONE;
    ksnprintf(status_text, sizeof(status_text),
              "Rendered document: %lu bytes from %s", len, host);
  }
  progress = 100;
}

static void draw_button(wl_surface_t *s, i32 x, i32 y, i32 w, const char *label,
                        bool enabled) {
  wl_draw_rounded_rect(s, x, y, w, 30, 7, enabled ? 0xFF1F2937 : 0xFF111827);
  wl_draw_rounded_outline(s, x, y, w, 30, 7, enabled ? 0xFF334155 : 0xFF1F2937);
  wl_draw_text(s, x + 11, y + 8, label, enabled ? 0xFFE5E7EB : 0xFF64748B, 0);
}

static void draw_wrapped_text(wl_surface_t *s, i32 x, i32 *y, const char *text,
                              u32 color, int max_cols) {
  char line[112];
  int col = 0;
  line[0] = 0;
  for (usize i = 0; text[i]; i++) {
    char c = text[i];
    if (c == '\n' || col >= max_cols) {
      line[col] = 0;
      wl_draw_text(s, x, *y, line, color, 0);
      *y += 18;
      col = 0;
      line[0] = 0;
      if (c == '\n')
        continue;
    }
    if (col + 1 < (int)sizeof(line))
      line[col++] = c;
  }
  if (col > 0) {
    line[col] = 0;
    wl_draw_text(s, x, *y, line, color, 0);
    *y += 18;
  }
}

static i32 wrapped_text_height(const char *text, int max_cols) {
  if (!text || !*text)
    return 18;
  int col = 0;
  i32 lines = 1;
  for (usize i = 0; text[i]; i++) {
    if (text[i] == '\n' || col >= max_cols) {
      lines++;
      col = 0;
      if (text[i] == '\n')
        continue;
    }
    col++;
  }
  return lines * 18;
}

static i32 node_height(render_node_t *n, i32 w) {
  (void)w;
  if (!n)
    return 0;
  if (n->type == RN_H1)
    return 66;
  if (n->type == RN_H2)
    return 32;
  if (n->type == RN_H3)
    return 22;
  if (n->type == RN_LI)
    return wrapped_text_height(n->text, 84) + 4;
  if (n->type == RN_LINK)
    return wrapped_text_height(n->text, 86) + 4;
  if (n->type == RN_PRE)
    return wrapped_text_height(n->text, 82) + 26;
  if (n->type == RN_IMG)
    return 86;
  if (n->type == RN_BUTTON)
    return 42;
  if (n->type == RN_FORM)
    return 44;
  return wrapped_text_height(n->text, 86) + 8;
}

static void browser_scroll_by(i32 delta) {
  i32 max_scroll =
      document_height > DOC_VIEW_H ? document_height - DOC_VIEW_H : 0;
  document_scroll += delta;
  if (document_scroll < 0)
    document_scroll = 0;
  if (document_scroll > max_scroll)
    document_scroll = max_scroll;
}

static bool node_visible(i32 y, i32 h) {
  return y + h >= DOC_TOP && y <= DOC_BOTTOM;
}

static void draw_render_tree(wl_surface_t *s, i32 x, i32 y, i32 w) {
  i32 layout_y = y;
  for (u32 i = 0; i < render_count; i++) {
    render_node_t *n = &render_nodes[i];
    i32 h = node_height(n, w);
    i32 cy = layout_y - document_scroll;
    layout_y += h;
    if (!node_visible(cy, h))
      continue;
    u32 fg = (n->flags & NODE_STYLE_FG) ? n->fg : 0;
    u32 bg = (n->flags & NODE_STYLE_BG) ? n->bg : 0;
    if (n->type == RN_H1) {
      wl_draw_vgradient(s, x - 14, cy - 12, w - 16, 72, bg ? bg : 0xFFEFF6FF,
                        0xFFFFFFFF);
      wl_draw_rounded_outline(s, x - 14, cy - 12, w - 16, 72, 8, 0xFFBFDBFE);
      wl_draw_text(s, x, cy, n->text, fg ? fg : 0xFF0F172A, 0);
    } else if (n->type == RN_H2) {
      wl_draw_rect(s, x, cy + 18, 56, 2, 0xFF2563EB);
      wl_draw_text(s, x, cy, n->text, fg ? fg : 0xFF1D4ED8, 0);
    } else if (n->type == RN_H3) {
      wl_draw_text(s, x, cy, n->text, fg ? fg : 0xFF334155, 0);
    } else if (n->type == RN_LI) {
      wl_draw_filled_circle(s, x + 4, cy + 7, 3, 0xFF2563EB);
      i32 ty = cy;
      if (bg)
        wl_draw_rounded_rect(s, x + 12, cy - 5, w - 54, h, 6, bg);
      draw_wrapped_text(s, x + 18, &ty, n->text, fg ? fg : 0xFF111827, 84);
    } else if (n->type == RN_LINK) {
      i32 ty = cy;
      draw_wrapped_text(s, x, &ty, n->text, fg ? fg : 0xFF1D4ED8, 86);
      wl_draw_rect(s, x, ty - 3,
                   (strlen(n->text) > 86 ? 86 : strlen(n->text)) * 8, 1,
                   0xFF1D4ED8);
    } else if (n->type == RN_PRE) {
      wl_draw_rounded_rect(s, x - 8, cy - 6, w - 40, h - 8, 6,
                           bg ? bg : 0xFFE2E8F0);
      i32 ty = cy;
      draw_wrapped_text(s, x, &ty, n->text, fg ? fg : 0xFF334155, 82);
    } else if (n->type == RN_IMG) {
      wl_draw_rounded_rect(s, x, cy, 132, 74, 7, bg ? bg : 0xFFE0F2FE);
      wl_draw_rounded_outline(s, x, cy, 132, 74, 7, 0xFF7DD3FC);
      wl_draw_filled_circle(s, x + 31, cy + 25, 10, 0xFF38BDF8);
      wl_draw_rect(s, x + 18, cy + 50, 96, 4, 0xFF0EA5E9);
      i32 ty = cy + 8;
      draw_wrapped_text(s, x + 150, &ty, n->text, fg ? fg : 0xFF334155, 64);
    } else if (n->type == RN_BUTTON) {
      i32 bw = (i32)strlen(n->text) * 8 + 28;
      if (bw < 96)
        bw = 96;
      if (bw > w - 40)
        bw = w - 40;
      wl_draw_rounded_rect(s, x, cy, bw, 30, 7, bg ? bg : 0xFF2563EB);
      wl_draw_text(s, x + 14, cy + 8, n->text, fg ? fg : 0xFFFFFFFF, 0);
    } else if (n->type == RN_FORM) {
      wl_draw_rounded_rect(s, x, cy, w - 52, 32, 7, bg ? bg : 0xFFF8FAFC);
      wl_draw_rounded_outline(s, x, cy, w - 52, 32, 7, 0xFFCBD5E1);
      wl_draw_text(s, x + 12, cy + 9, n->text, fg ? fg : 0xFF64748B, 0);
    } else {
      i32 ty = cy;
      if (bg)
        wl_draw_rounded_rect(s, x - 8, cy - 6, w - 40, h, 6, bg);
      draw_wrapped_text(s, x, &ty, n->text, fg ? fg : 0xFF111827, 86);
    }
  }
  document_height = layout_y - y;
}

static void draw_browser(wl_surface_t *s) {
  wl_draw_vgradient(s, 0, 0, BROWSER_W, BROWSER_H, 0xFF0B1120, 0xFF111827);

  wl_draw_rect(s, 0, 0, BROWSER_W, 44, 0xFF0F172A);
  wl_draw_rounded_rect(s, 14, 8, 178, 32, 8, 0xFF111827);
  wl_draw_text(s, 28, 17, page_title, 0xFFE5E7EB, 0);
  wl_draw_text(s, 172, 17, "x", 0xFF64748B, 0);
  wl_draw_text(s, 214, 17, "+", 0xFF94A3B8, 0);

  wl_draw_rect(s, 0, 44, BROWSER_W, 54, 0xFF111827);
  draw_button(s, 16, 56, 38, "<", history_pos > 0);
  draw_button(s, 60, 56, 38, ">", history_pos + 1 < history_count);
  draw_button(s, 104, 56, 38, "R", true);

  wl_draw_rounded_rect(s, 154, 55, 536, 32, 8,
                       address_focus ? 0xFF1D4ED8 : 0xFF334155);
  wl_draw_rounded_rect(s, 155, 56, 534, 30, 7, 0xFF0B1220);
  wl_draw_text(s, 168, 64, url_buffer, 0xFFFFFFFF, 0);
  if (address_focus) {
    int cx = 168 + (int)strlen(url_buffer) * 8;
    if (cx > 674)
      cx = 674;
    wl_draw_rect(s, cx, 64, 8, 16, 0xFFFFFFFF);
  }
  wl_draw_rounded_rect(s, 702, 55, 94, 32, 8, 0xFF2563EB);
  wl_draw_text(s, 728, 64, "Go", 0xFFFFFFFF, 0);

  if (br_state == BR_LOADING)
    wl_draw_rect(s, 0, 96, (BROWSER_W * progress) / 100, 3, 0xFF38BDF8);
  else
    wl_draw_rect(s, 0, 96, BROWSER_W, 1, 0xFF263244);

  wl_draw_rounded_rect(s, 18, 116, BROWSER_W - 36, BROWSER_H - 164, 9,
                       0xFFF8FAFC);
  wl_draw_rounded_outline(s, 18, 116, BROWSER_W - 36, BROWSER_H - 164, 9,
                          0xFFCBD5E1);
  wl_draw_text(s, 38, 136, page_title, 0xFF0F172A, 0);
  wl_draw_rounded_rect(s, 38, 158, 108, 24, 6,
                       br_state == BR_ERROR ? 0xFFFEE2E2 : 0xFFDCFCE7);
  wl_draw_text(s, 52, 163, br_state == BR_ERROR ? "Network" : "Document",
               br_state == BR_ERROR ? 0xFF991B1B : 0xFF166534, 0);
  wl_draw_text(s, 160, 163, response_meta,
               br_state == BR_ERROR ? 0xFFB91C1C : 0xFF475569, 0);
  wl_draw_text(s, 38, 188, loaded_url, 0xFF64748B, 0);
  wl_draw_rect(s, 38, 212, BROWSER_W - 76, 1, 0xFFE2E8F0);

  if (br_state == BR_LOADING) {
    wl_draw_text(s, 344, 288, "Loading...", 0xFF475569, 0);
  } else {
    if (render_count > 0)
      draw_render_tree(s, 38, 232, BROWSER_W - 76);
    else if (br_state == BR_ERROR)
      wl_draw_text(s, 38, 232, rendered_text, 0xFF7F1D1D, 0);
    else
      draw_render_tree(s, 38, 232, BROWSER_W - 76);
  }

  if (document_height > DOC_VIEW_H) {
    i32 track_x = BROWSER_W - 34;
    i32 track_y = DOC_TOP;
    i32 track_h = DOC_VIEW_H;
    i32 thumb_h = (DOC_VIEW_H * track_h) / document_height;
    if (thumb_h < 34)
      thumb_h = 34;
    i32 max_scroll = document_height - DOC_VIEW_H;
    i32 thumb_y = track_y + ((track_h - thumb_h) * document_scroll) /
                                (max_scroll ? max_scroll : 1);
    wl_draw_rounded_rect(s, track_x, track_y, 6, track_h, 3, 0xFFE2E8F0);
    wl_draw_rounded_rect(s, track_x, thumb_y, 6, thumb_h, 3, 0xFF64748B);
  }

  wl_draw_rect(s, 0, BROWSER_H - 34, BROWSER_W, 34, 0xFF0F172A);
  wl_draw_text(s, 16, BROWSER_H - 23, status_text,
               br_state == BR_ERROR ? 0xFFFFB4B4 : 0xFF94A3B8, 0);
  wl_draw_text(s, BROWSER_W - 342, BROWSER_H - 23,
               "YamBrowser: styled layout + scroll v0", 0xFF64748B, 0);
}

static bool hit(i32 x, i32 y, i32 w, i32 h) {
  return last_x >= x && last_x < x + w && last_y >= y && last_y < y + h;
}

static void browser_go_history(int delta) {
  int next = history_pos + delta;
  if (next < 0 || next >= history_count)
    return;
  history_pos = next;
  text_copy(url_buffer, sizeof(url_buffer), history[history_pos]);
  browser_fetch_current();
}

static void browser_click(void) {
  if (hit(16, 56, 38, 30))
    browser_go_history(-1);
  else if (hit(60, 56, 38, 30))
    browser_go_history(1);
  else if (hit(104, 56, 38, 30))
    browser_fetch_current();
  else if (hit(154, 55, 536, 32))
    address_focus = true;
  else if (hit(702, 55, 94, 32))
    browser_fetch_current();
  else
    address_focus = false;
}

static void browser_key(u16 sc) {
  if (sc == 0x2A || sc == 0x36)
    return;
  char c = 0;
  if (sc < 128)
    c = shift_held ? sc_ascii_shift[sc] : sc_ascii[sc];

  if (c == '\n') {
    browser_fetch_current();
  } else if (c == '\b' && address_focus) {
    int len = strlen(url_buffer);
    if (len > 0)
      url_buffer[len - 1] = 0;
  } else if (c == '\t') {
    address_focus = !address_focus;
  } else if (c >= 32 && c <= 126 && address_focus) {
    int len = strlen(url_buffer);
    if (len + 1 < URL_MAX) {
      url_buffer[len] = c;
      url_buffer[len + 1] = 0;
    }
  } else if (sc == 0x48) {
    if (address_focus)
      browser_go_history(-1);
    else
      browser_scroll_by(-42);
  } else if (sc == 0x50) {
    if (address_focus)
      browser_go_history(1);
    else
      browser_scroll_by(42);
  } else if (sc == 0x49) {
    browser_scroll_by(-DOC_VIEW_H + 36);
  } else if (sc == 0x51) {
    browser_scroll_by(DOC_VIEW_H - 36);
  }
}

void wl_browser_task(void *arg) {
  (void)arg;
  task_sleep_ms(300);

  wl_surface_t *s = wl_surface_create("YamBrowser", 150, 80, BROWSER_W,
                                      BROWSER_H, sched_current()->id);
  if (!s)
    return;

  text_copy(
      rendered_text, sizeof(rendered_text),
      "Type a plain HTTP URL and press Enter.\n\nTry:\n- http://ansnew.com/\n- "
      "http://neverssl.com/\n\nThis browser now has a real plain-HTTP network "
      "path and a static DOM paint layer. HTTPS page loading, JavaScript, full "
      "CSS layout, images, and Mozilla-class rendering are still engine work.");
  render_count = 0;
  render_add(RN_H1, "YamBrowser");
  render_add(
      RN_P,
      "Plain HTTP pages are fetched by the YamOS kernel network stack, parsed "
      "into static document nodes, and painted by the compositor.");
  render_add(RN_LI, "Works now: DNS, TCP, HTTP/1.0, redirects, headings, "
                    "paragraphs, links, forms, buttons, image placeholders.");
  render_add(
      RN_LI,
      "Still required for Firefox-class browsing: encrypted HTTPS stream, "
      "certificate validation, HTML5 tree builder, CSS cascade/layout, "
      "JavaScript, images, storage, cache, cookies, sandboxing.");
  draw_browser(s);
  wl_surface_commit(s);

  u32 my_id = s->id;
  while (s->state == WL_SURFACE_ACTIVE && s->id == my_id) {
    input_event_t ev;
    while (wl_surface_pop_event(s, &ev)) {
      if (ev.type == EV_ABS && ev.code == 0)
        last_x = ev.value;
      else if (ev.type == EV_ABS && ev.code == 1)
        last_y = ev.value;
      else if (ev.type == EV_REL && ev.code == REL_WHEEL)
        browser_scroll_by(ev.value < 0 ? 72 : -72);
      else if (ev.type == EV_KEY) {
        if (ev.code == 0x2A || ev.code == 0x36) {
          shift_held = ev.value == KEY_PRESSED;
        } else if (ev.value == KEY_PRESSED) {
          if (ev.code >= 0x110)
            browser_click();
          else
            browser_key(ev.code);
        }
      }
    }
    draw_browser(s);
    wl_surface_commit(s);
    task_sleep_ms(16);
  }
}
