#ifndef YAMOS_YPKG_H
#define YAMOS_YPKG_H

#include <nexus/types.h>

/*
 * Minimal kernel-side package install:
 *   Line-oriented manifest (see ypkg.c), HTTPS payload fetch, SHA-256 verify,
 *   atomic-ish payload write + append-only transaction log on persistent storage.
 *
 * If manifest_https_host/path are set, GET that manifest over TLS. The response
 * body must parse as a valid YAMOSYPKG1 manifest; otherwise installation falls
 * back to manifest_embedded_text (when non-NULL). TLS errors, empty bodies, or
 * HTML error pages trigger the same fallback.
 */
int ypkg_install(const char *manifest_https_host, const char *manifest_https_path,
                 const char *manifest_embedded_text, const char *payload_install_path,
                 u32 *out_last_error, char *msg_buf, usize msg_cap);

#endif
