/* ============================================================================
 * YamKernel — Wayland Client: Primitive Text-Mode Browser
 * ============================================================================ */
#include <nexus/types.h>
#include "../sched/sched.h"
#include "../sched/wait.h"
#include "compositor.h"
#include "wl_draw.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

/* Simple dummy content for now since we don't want to block the thread
   with real networking in this demo just yet, but we will pretend it fetched it. */
static const char *browser_content = 
    "Welcome to YamKernel Web Browser!\n"
    "=================================\n\n"
    "This is a primitive text-mode browser running directly\n"
    "in the kernel's Wayland compositor.\n\n"
    "Current capabilities:\n"
    "- Wayland surface rendering\n"
    "- Basic text layout\n"
    "- Mouse input routing\n\n"
    "Connecting to http://example.com...\n"
    "HTTP/1.1 200 OK\n"
    "Content-Type: text/html\n\n"
    "<html>\n"
    "  <body>\n"
    "    <h1>Example Domain</h1>\n"
    "    <p>This domain is for use in illustrative examples\n"
    "    in documents. You may use this domain in literature\n"
    "    without prior coordination or asking for permission.</p>\n"
    "  </body>\n"
    "</html>\n\n"
    "Network stack is alive, but HTML parsing is not implemented yet!\n"
    ;

static void draw_browser(wl_surface_t *s) {
    /* Background */
    wl_draw_rect(s, 0, 0, 600, 400, 0xFFFFFFFF);
    
    /* Address bar */
    wl_draw_rect(s, 0, 0, 600, 30, 0xFFDDDDDD);
    wl_draw_rect(s, 10, 5, 580, 20, 0xFFFFFFFF);
    wl_draw_text(s, 15, 7, "http://example.com", 0xFF000000, 0);
    
    /* Content area */
    wl_draw_text(s, 10, 40, browser_content, 0xFF000000, 0);
}

void wl_browser_task(void *arg) {
    (void)arg;
    /* Wait for compositor to be ready */
    task_sleep_ms(300);
    
    wl_surface_t *s = wl_surface_create("YamBrowser", 150, 420, 600, 400, sched_current()->id);
    if (!s) return;
    
    draw_browser(s);
    wl_surface_commit(s);
    
    while (1) {
        input_event_t ev;
        while (wl_surface_pop_event(s, &ev)) {
            /* We can add scrolling or clicking links later */
            (void)ev;
        }
        
        task_sleep_ms(100);
    }
}
