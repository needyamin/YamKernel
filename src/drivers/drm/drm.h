/* ============================================================================
 * YamKernel — DRM/KMS (Direct Rendering Manager / Kernel Mode Setting)
 * Provides a display abstraction layer between the hardware framebuffer
 * and the Wayland compositor. Supports dumb buffer allocation.
 * ============================================================================ */
#ifndef _DRM_DRM_H
#define _DRM_DRM_H

#include <nexus/types.h>

/* A dumb buffer — unaccelerated software rendering target */
typedef struct drm_buffer {
    u32         id;
    u32         width;
    u32         height;
    u32         pitch;          /* bytes per row */
    u32         bpp;            /* bits per pixel (always 32 = ARGB8888) */
    u32        *pixels;         /* kernel virtual pointer to pixel data */
    usize       size;           /* total buffer size in bytes */
} drm_buffer_t;

/* Display mode information */
typedef struct drm_mode {
    u32  width;
    u32  height;
    u32  refresh_hz;
} drm_mode_t;

/* Initialize the DRM subsystem (wraps the Limine framebuffer) */
void drm_init(void);

/* Get the current display mode */
drm_mode_t drm_get_mode(void);

/* Allocate a dumb buffer for software rendering */
drm_buffer_t *drm_create_dumb_buffer(u32 width, u32 height);

/* Free a dumb buffer */
void drm_destroy_dumb_buffer(drm_buffer_t *buf);

/* Page-flip: blit a dumb buffer to the primary framebuffer (vsync placeholder) */
void drm_page_flip(drm_buffer_t *buf);

/* Get the primary scanout buffer (the actual hardware framebuffer) */
drm_buffer_t *drm_get_primary(void);

#endif /* _DRM_DRM_H */
