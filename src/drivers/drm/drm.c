/* ============================================================================
 * YamKernel — DRM/KMS Implementation
 * Wraps the Limine framebuffer as a DRM device with dumb buffer support.
 * The compositor allocates dumb buffers, renders into them, then page-flips.
 * ============================================================================ */
#include "drm.h"
#include "../video/framebuffer.h"
#include "../../mem/heap.h"
#include "../../mem/pmm.h"
#include "../../mem/vmm.h"
#include "../../lib/string.h"
#include "../../lib/kprintf.h"

static drm_buffer_t g_primary;          /* The hardware framebuffer */
static u32 *g_shadow_buffer = NULL;     /* Fast RAM shadow for damage tracking */
static u32 g_next_buf_id = 1;

void drm_init(void) {
    g_primary.id     = 0;
    g_primary.width  = fb_get_width();
    g_primary.height = fb_get_height();
    g_primary.pitch  = fb_get_pitch();
    g_primary.bpp    = 32;
    g_primary.pixels = fb_get_pixels();
    g_primary.size   = (usize)g_primary.pitch * g_primary.height;

    /* Allocate shadow buffer to minimize MMIO VM-Exits on KVM */
    g_shadow_buffer = (u32 *)kmalloc(g_primary.size);
    if (g_shadow_buffer) {
        memset(g_shadow_buffer, 0, g_primary.size);
    }

    kprintf_color(0xFF00FF88, "[DRM] Initialized: %ux%u @ %u bpp (primary fb=%p)\n",
                  g_primary.width, g_primary.height, g_primary.bpp,
                  (void *)g_primary.pixels);
}

drm_mode_t drm_get_mode(void) {
    return (drm_mode_t){
        .width      = g_primary.width,
        .height     = g_primary.height,
        .refresh_hz = 60,
    };
}

drm_buffer_t *drm_create_dumb_buffer(u32 width, u32 height) {
    drm_buffer_t *buf = (drm_buffer_t *)kmalloc(sizeof(drm_buffer_t));
    if (!buf) return NULL;

    buf->id     = g_next_buf_id++;
    buf->width  = width;
    buf->height = height;
    buf->pitch  = width * 4;   /* 32-bit ARGB */
    buf->bpp    = 32;
    buf->size   = (usize)buf->pitch * height;

    /* Allocate page-aligned pixel memory */
    u64 pages = (buf->size + PAGE_SIZE - 1) / PAGE_SIZE;
    u64 phys = pmm_alloc_pages(pages);
    if (!phys) {
        kfree(buf);
        return NULL;
    }
    buf->pixels = (u32 *)vmm_phys_to_virt(phys);
    memset(buf->pixels, 0, pages * PAGE_SIZE);

    return buf;
}

void drm_destroy_dumb_buffer(drm_buffer_t *buf) {
    if (!buf) return;
    if (buf->pixels && buf->id != 0) {
        u64 pages = (buf->size + PAGE_SIZE - 1) / PAGE_SIZE;
        u64 phys = vmm_virt_hhdm_to_phys(buf->pixels);
        pmm_free_pages(phys, pages);
    }
    kfree(buf);
}

void drm_page_flip(drm_buffer_t *buf) {
    if (!buf || !buf->pixels || !g_primary.pixels) return;

    /* Blit the dumb buffer to the hardware framebuffer.
     * Only copy the overlapping region. */
    u32 copy_w = buf->width < g_primary.width ? buf->width : g_primary.width;
    u32 copy_h = buf->height < g_primary.height ? buf->height : g_primary.height;
    u32 src_pitch4 = buf->pitch / 4;
    u32 dst_pitch4 = g_primary.pitch / 4;

    for (u32 y = 0; y < copy_h; y++) {
        u32 *src = buf->pixels + y * src_pitch4;
        u32 *dst = g_primary.pixels + y * dst_pitch4;
        
        if (g_shadow_buffer) {
            u32 *shadow = g_shadow_buffer + y * dst_pitch4;
            /* Shadow buffer is MUCH faster in non-KVM QEMU because it avoids 
               expensive MMIO writes for pixels that haven't changed. */
            for (u32 x = 0; x < copy_w; x++) {
                if (src[x] != shadow[x]) {
                    dst[x] = src[x];
                    shadow[x] = src[x];
                }
            }
        } else {
            memcpy(dst, src, copy_w * 4);
        }
    }
}

drm_buffer_t *drm_get_primary(void) {
    return &g_primary;
}
