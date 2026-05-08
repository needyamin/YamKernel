/* ============================================================================
 * YamKernel — minimal ypkg (manifest + HTTPS payload + SHA-256 + FAT32 log line)
 * ============================================================================ */

#include "ypkg.h"

#include "../../../drivers/timer/pit.h"
#include "../../../fs/vfs.h"
#include "../../../lib/kprintf.h"
#include "../../../lib/string.h"
#include "../../../net/net.h"

#include "mbedtls/sha256.h"

#define YPKG_MAGIC "YAMOSYPKG1"

#define YPKG_TX_LOG "/var/cache/yamos/ypkg/transactions.log"

#define YPKG_MANIFEST_BUF 4096
#define YPKG_RESPONSE_BUF 8192

#define O_CREAT_KERNEL 0x0040u
#define O_TRUNC_KERNEL 0x0200u
#define O_WRONLY_KERNEL 0x0001u

typedef struct {
  char name[64];
  char version[32];
  char payload_host[192];
  char payload_path[256];
  char payload_sha256_hex[68];
} ypkg_manifest_t;

static void copy_msg(char *dst, usize cap, const char *src) {
  if (!dst || cap == 0)
    return;
  if (!src)
    src = "";
  usize i = 0;
  for (; i + 1 < cap && src[i]; i++)
    dst[i] = src[i];
  dst[i] = 0;
}

static void bin_to_hex_lower(const u8 *bin, char out[65]) {
  static const char xd[] = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    out[i * 2] = xd[(bin[i] >> 4) & 0xF];
    out[i * 2 + 1] = xd[bin[i] & 0xF];
  }
  out[64] = 0;
}

static bool hex_is_canonical(const char *hex) {
  if (!hex)
    return false;
  usize n = strlen(hex);
  if (n != 64)
    return false;
  for (usize i = 0; i < n; i++) {
    char c = hex[i];
    bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F');
    if (!ok)
      return false;
  }
  return true;
}

static int hex_eq_ci(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z')
      ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z')
      cb = (char)(cb - 'A' + 'a');
    if (ca != cb)
      return 0;
  }
  return *a == 0 && *b == 0;
}

static bool sha256_matches(const u8 *data, usize len, const char *hex_expect) {
  u8 sum[32];
  mbedtls_sha256(data, len, sum, 0);
  char got[65];
  bin_to_hex_lower(sum, got);
  return hex_eq_ci(got, hex_expect);
}

static int http_body_offset(const char *buf, usize total, usize *body_off,
                            usize *body_len) {
  if (!buf || total == 0 || !body_off || !body_len)
    return -1;
  for (usize i = 0; i + 3 < total; i++) {
    if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' &&
        buf[i + 3] == '\n') {
      usize start = i + 4;
      *body_off = start;
      *body_len = total - start;
      return 0;
    }
  }
  for (usize i = 0; i + 1 < total; i++) {
    if (buf[i] == '\n' && buf[i + 1] == '\n') {
      usize start = i + 2;
      *body_off = start;
      *body_len = total - start;
      return 0;
    }
  }
  return -1;
}

static int split_https_url(const char *url, char *host, usize host_cap,
                           char *path, usize path_cap) {
  if (!url || !host || !path || host_cap == 0 || path_cap == 0)
    return -1;
  host[0] = path[0] = 0;
  if (strncmp(url, "https://", 8) != 0)
    return -1;
  const char *p = url + 8;
  const char *slash = strchr(p, '/');
  if (!slash) {
    usize hn = strlen(p);
    if (hn + 1 >= host_cap)
      return -1;
    memcpy(host, p, hn);
    host[hn] = 0;
    strncpy(path, "/", path_cap - 1);
    path[path_cap - 1] = 0;
    return 0;
  }
  usize hn = (usize)(slash - p);
  if (hn + 1 >= host_cap)
    return -1;
  memcpy(host, p, hn);
  host[hn] = 0;
  strncpy(path, slash, path_cap - 1);
  path[path_cap - 1] = 0;
  if (path[0] == 0) {
    strncpy(path, "/", path_cap - 1);
    path[path_cap - 1] = 0;
  }
  return 0;
}

static void trim_crlf(char *line) {
  usize n = strlen(line);
  while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n')) {
    line[n - 1] = 0;
    n--;
  }
}

static int parse_kv(ypkg_manifest_t *m, const char *key, const char *val) {
  if (strcmp(key, "name") == 0) {
    strncpy(m->name, val, sizeof(m->name) - 1);
    m->name[sizeof(m->name) - 1] = 0;
    return 0;
  }
  if (strcmp(key, "version") == 0) {
    strncpy(m->version, val, sizeof(m->version) - 1);
    m->version[sizeof(m->version) - 1] = 0;
    return 0;
  }
  if (strcmp(key, "payload_url") == 0) {
    return split_https_url(val, m->payload_host, sizeof(m->payload_host),
                           m->payload_path, sizeof(m->payload_path));
  }
  if (strcmp(key, "payload_sha256") == 0) {
    strncpy(m->payload_sha256_hex, val, sizeof(m->payload_sha256_hex) - 1);
    m->payload_sha256_hex[sizeof(m->payload_sha256_hex) - 1] = 0;
    return 0;
  }
  return 0;
}

static int parse_manifest_text(char *text, ypkg_manifest_t *out) {
  memset(out, 0, sizeof(*out));
  bool saw_magic = false;
  char *cursor = text;
  while (*cursor) {
    char *line_end = cursor;
    while (*line_end && *line_end != '\n')
      line_end++;
    char saved = *line_end;
    *line_end = 0;
    trim_crlf(cursor);
    if (*cursor == 0) {
      *line_end = saved;
      if (!saved)
        break;
      cursor = line_end + 1;
      continue;
    }
    if (!saw_magic) {
      if (strcmp(cursor, YPKG_MAGIC) != 0)
        return -1;
      saw_magic = true;
    } else {
      char *eq = strchr(cursor, '=');
      if (!eq)
        return -1;
      *eq = 0;
      const char *key = cursor;
      const char *val = eq + 1;
      if (parse_kv(out, key, val) != 0) {
        *eq = '=';
        return -1;
      }
      *eq = '=';
    }
    *line_end = saved;
    if (!saved)
      break;
    cursor = line_end + 1;
  }
  if (!saw_magic)
    return -1;
  if (!out->payload_host[0] || !out->payload_path[0])
    return -1;
  if (!hex_is_canonical(out->payload_sha256_hex))
    return -1;
  return 0;
}

/* Trial parse without mutating `text` (parse_manifest_text edits key=value lines). */
static bool manifest_body_is_valid(const char *text, usize text_len) {
  char trial[YPKG_MANIFEST_BUF];
  if (text_len + 1 > sizeof(trial))
    return false;
  memcpy(trial, text, text_len);
  trial[text_len] = 0;
  ypkg_manifest_t tmp;
  return parse_manifest_text(trial, &tmp) == 0;
}

static int append_transaction_line(const ypkg_manifest_t *m,
                                   const char *install_path) {
  (void)sys_mkdir("/var/cache/yamos/ypkg", 0755);
  int fd = sys_open(YPKG_TX_LOG, O_CREAT_KERNEL | O_WRONLY_KERNEL);
  if (fd < 0) {
    kprintf("[YPKG] transaction log open failed path=%s\n", YPKG_TX_LOG);
    return -1;
  }
  if (sys_lseek(fd, 0, 2) < 0) {
    sys_close(fd);
    return -1;
  }
  char line[384];
  int n = ksnprintf(line, sizeof(line),
                    "ok name=%s version=%s path=%s tick=%llu\n",
                    m->name[0] ? m->name : "unknown",
                    m->version[0] ? m->version : "0",
                    install_path, (unsigned long long)pit_get_ticks());
  if (n <= 0 || (usize)n >= sizeof(line)) {
    sys_close(fd);
    return -1;
  }
  isize w = sys_write(fd, line, (usize)n);
  sys_close(fd);
  return w == (isize)n ? 0 : -1;
}

int ypkg_install(const char *manifest_https_host,
                 const char *manifest_https_path,
                 const char *manifest_embedded_text,
                 const char *payload_install_path, u32 *out_last_error,
                 char *msg_buf, usize msg_cap) {
  if (out_last_error)
    *out_last_error = YAM_INSTALL_ERR_NONE;
  copy_msg(msg_buf, msg_cap, "");

  char manifest_storage[YPKG_MANIFEST_BUF];
  bool use_embedded = false;

  if (manifest_https_host && manifest_https_path && manifest_https_path[0]) {
    usize got = 0;
    int hr = https_get(manifest_https_host, manifest_https_path,
                       manifest_storage, sizeof(manifest_storage), &got);
    if (hr == 0 && got > 0) {
      usize bo = 0, bl = 0;
      if (http_body_offset(manifest_storage, got, &bo, &bl) == 0 &&
          bl + 1 < sizeof(manifest_storage)) {
        memmove(manifest_storage, manifest_storage + bo, bl);
        manifest_storage[bl] = 0;
        if (manifest_body_is_valid(manifest_storage, bl)) {
          kprintf("[YPKG] manifest from https://%s%s (%lu bytes body)\n",
                  manifest_https_host, manifest_https_path,
                  (unsigned long)bl);
        } else {
          kprintf(
              "[YPKG] remote manifest invalid or non-%s (e.g. 404 HTML); "
              "trying embedded\n",
              YPKG_MAGIC);
          use_embedded = true;
        }
      } else {
        kprintf("[YPKG] remote manifest response has no HTTP body; trying "
                "embedded\n");
        use_embedded = true;
      }
    } else {
      kprintf("[YPKG] manifest HTTPS GET failed rc=%d; trying embedded\n", hr);
      use_embedded = true;
    }
  } else {
    use_embedded = true;
  }

  if (use_embedded) {
    if (!manifest_embedded_text || !manifest_embedded_text[0]) {
      copy_msg(msg_buf, msg_cap, "no manifest (fetch unusable and no embed)");
      if (out_last_error)
        *out_last_error = YAM_INSTALL_ERR_MANIFEST;
      return -1;
    }
    usize el = strlen(manifest_embedded_text);
    if (el + 1 > sizeof(manifest_storage)) {
      copy_msg(msg_buf, msg_cap, "embedded manifest too large");
      if (out_last_error)
        *out_last_error = YAM_INSTALL_ERR_MANIFEST;
      return -1;
    }
    memcpy(manifest_storage, manifest_embedded_text, el + 1);
    kprintf("[YPKG] using embedded manifest (%lu bytes)\n", (unsigned long)el);
  }

  ypkg_manifest_t mf;
  if (parse_manifest_text(manifest_storage, &mf) != 0) {
    copy_msg(msg_buf, msg_cap, "invalid manifest");
    if (out_last_error)
      *out_last_error = YAM_INSTALL_ERR_MANIFEST;
    return -1;
  }

  char resp[YPKG_RESPONSE_BUF];
  usize rlen = 0;
  int gr =
      https_get(mf.payload_host, mf.payload_path, resp, sizeof(resp), &rlen);
  if (gr != 0 || rlen == 0) {
    copy_msg(msg_buf, msg_cap, "HTTPS payload fetch failed");
    if (out_last_error)
      *out_last_error = YAM_INSTALL_ERR_DOWNLOAD;
    return -1;
  }

  usize body_off = 0, body_len = 0;
  if (http_body_offset(resp, rlen, &body_off, &body_len) != 0) {
    copy_msg(msg_buf, msg_cap, "payload response has no HTTP body");
    if (out_last_error)
      *out_last_error = YAM_INSTALL_ERR_DOWNLOAD;
    return -1;
  }

  const u8 *body_bytes = (const u8 *)resp + body_off;
  if (!sha256_matches(body_bytes, body_len, mf.payload_sha256_hex)) {
    copy_msg(msg_buf, msg_cap, "SHA-256 mismatch");
    if (out_last_error)
      *out_last_error = YAM_INSTALL_ERR_INTEGRITY;
    kprintf("[YPKG] integrity check failed for https://%s%s\n", mf.payload_host,
            mf.payload_path);
    return -1;
  }

  int fd = sys_open(payload_install_path,
                    O_CREAT_KERNEL | O_TRUNC_KERNEL | O_WRONLY_KERNEL);
  if (fd < 0) {
    copy_msg(msg_buf, msg_cap, "open install path failed");
    if (out_last_error)
      *out_last_error = YAM_INSTALL_ERR_IO;
    return -1;
  }
  isize wr = sys_write(fd, body_bytes, body_len);
  sys_close(fd);
  if (wr != (isize)body_len) {
    copy_msg(msg_buf, msg_cap, "write payload failed");
    if (out_last_error)
      *out_last_error = YAM_INSTALL_ERR_IO;
    return -1;
  }

  if (append_transaction_line(&mf, payload_install_path) != 0) {
    copy_msg(msg_buf, msg_cap, "payload installed but transaction log failed");
    if (out_last_error)
      *out_last_error = YAM_INSTALL_ERR_IO;
    return -1;
  }

  kprintf("[YPKG] installed %s -> %s (%lu bytes)\n",
          mf.name[0] ? mf.name : "package", payload_install_path,
          (unsigned long)body_len);
  copy_msg(msg_buf, msg_cap, "installed");
  return 0;
}
