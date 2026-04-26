/* ============================================================================
 * YamKernel — Wayland Demo Client
 * A simple application that creates a window and renders an animation.
 * ============================================================================ */
#include "demo_client.h"
#include "compositor.h"
#include "../sched/sched.h"
#include "../sched/wait.h"
#include "../lib/kprintf.h"

void wl_demo_client_task(void *arg) {
    (void)arg;
    
    /* Wait for compositor to fully initialize */
    task_sleep_ms(100);
    
    kprintf_color(0xFF00DDFF, "[WL-CLIENT] Starting Wayland Demo Application...\n");

    /* Create a window */
    u32 w = 400;
    u32 h = 300;
    wl_surface_t *surf = wl_surface_create("Yam Wayland Demo", 100, 100, w, h, sched_current()->id);
    if (!surf) {
        kprintf_color(0xFFFF3333, "[WL-CLIENT] Failed to create surface!\n");
        return;
    }

    /* Customize decorations */
    surf->title_bg = 0xFF8833FF; /* Purple titlebar */
    
    u32 frame = 0;
    while (1) {
        if (surf->state == WL_SURFACE_FREE) {
            /* Window was closed by compositor (user clicked X) */
            break;
        }

        u32 *pixels = wl_surface_get_pixels(surf);
        if (!pixels) break;

        /* Render an animated checkerboard pattern */
        for (u32 y = 0; y < h; y++) {
            for (u32 x = 0; x < w; x++) {
                u32 cx = (x + frame) / 20;
                u32 cy = (y + frame) / 20;
                
                if ((cx + cy) % 2 == 0) {
                    pixels[y * w + x] = 0xFF222222; /* Dark Gray */
                } else {
                    pixels[y * w + x] = 0xFF4488FF; /* Yam Blue */
                }
            }
        }
        
        /* Render a bouncing square */
        u32 bx = 50 + (frame * 2) % 300;
        u32 by = 50 + (frame * 3) % 200;
        for (u32 y = by; y < by + 40; y++) {
            for (u32 x = bx; x < bx + 40; x++) {
                if (x < w && y < h) {
                    pixels[y * w + x] = 0xFFFFDD00; /* Yellow */
                }
            }
        }

        /* Signal compositor that we updated the buffer */
        wl_surface_commit(surf);

        frame++;
        task_sleep_ms(16); /* ~60 FPS */
    }

    kprintf_color(0xFF00DDFF, "[WL-CLIENT] Application exiting.\n");
}
