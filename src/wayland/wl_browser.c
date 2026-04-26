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
static char url_buffer[128] = "google.com";
static char page_title[64] = "Google";
static bool is_loading = false;
static int load_progress = 0;
static const char *browser_status = "Ready";

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
    /* Dracula Background */
    wl_draw_rect(s, 0, 0, 600, 400, 0xFF282A36);
    
    /* Modern Address Bar Shell */
    wl_draw_rect(s, 0, 0, 600, 50, 0xFF44475A);
    
    /* Tabs Bar (Mock) */
    wl_draw_rect(s, 5, 5, 120, 25, 0xFF282A36);
    wl_draw_text(s, 15, 10, page_title, 0xFFF8F8F2, 0);
    wl_draw_text(s, 110, 10, "x", 0xFF6272A4, 0);
    
    /* Navigation Icons */
    wl_draw_text(s, 15, 32, "<  >  O", 0xFFBD93F9, 0); 
    
    /* URL Field */
    wl_draw_rect(s, 80, 28, 450, 22, 0xFF282A36);
    wl_draw_rect(s, 80, 48, 450, 2, 0xFFBD93F9);
    wl_draw_text(s, 90, 32, url_buffer, 0xFFF8F8F2, 0);
    
    /* Go Button */
    wl_draw_rect(s, 535, 28, 55, 22, 0xFF50FA7B);
    wl_draw_text(s, 550, 32, "GO", 0xFF282A36, 0);
    
    /* Progress Bar */
    if (is_loading) {
        wl_draw_rect(s, 0, 50, (600 * load_progress) / 100, 2, 0xFF50FA7B);
    }
    
    /* Main Content Area */
    wl_draw_rect(s, 0, 52, 600, 328, 0xFF1E1F29);
    
    if (is_loading) {
        wl_draw_text(s, 240, 200, "Resolving Host...", 0xFF6272A4, 0);
    } else {
        /* Render content with some margin */
        wl_draw_text(s, 20, 70, browser_content, 0xFFF8F8F2, 0);
    }
    
    /* Status Bar */
    wl_draw_rect(s, 0, 380, 600, 20, 0xFF44475A);
    wl_draw_text(s, 10, 383, browser_status, 0xFF6272A4, 0);
}

static void browser_load_page(wl_surface_t *s) {
    is_loading = true;
    browser_status = "Connecting...";
    
    /* Simulate incremental loading for "smoothness" */
    for (int p = 0; p <= 100; p += 20) {
        load_progress = p;
        if (p == 40) browser_status = "Waiting for response...";
        if (p == 80) browser_status = "Transferring data...";
        draw_browser(s);
        wl_surface_commit(s);
        task_sleep_ms(100);
    }
    
    if (strstr(url_buffer, "google.com")) {
        browser_content = "Google\n\n[ Search or type a URL ]\n\nTrending topics:\n- YamKernel Release v0.2.0\n- OS Development with Wayland\n- Dracula Theme for Kernels";
        strcpy(page_title, "Google");
        browser_status = "Done";
    } else if (strstr(url_buffer, "github.com")) {
        browser_content = "GitHub - Where the world builds software\n\nneedyamin / YamKernel\n[ Code ] [ Issues ] [ Pull Requests ]\n\nLatest commit: 'Implement Browser Modernization'";
        strcpy(page_title, "GitHub");
        browser_status = "Done";
    } else if (strstr(url_buffer, "wikipedia.org")) {
        browser_content = "Wikipedia - The Free Encyclopedia\n\nArticle: Operating System\nAn operating system (OS) is system software that\nmanages computer hardware, software resources,\nand provides common services for computer programs.";
        strcpy(page_title, "Wikipedia");
        browser_status = "Done";
    } else if (strstr(url_buffer, "kernel.org")) {
        browser_content = "The Linux Kernel Archives\n\nLatest Stable: 6.8.9\nMainline: 6.9-rc7\n\nYamKernel status: Experimental (Phase 7)";
        strcpy(page_title, "Kernel.org");
        browser_status = "Done";
    } else if (strlen(url_buffer) > 0) {
        /* Generic page for any other URL */
        static char generic_buf[256];
        ksnprintf(generic_buf, sizeof(generic_buf), 
            "Welcome to %s\n\nThis website has been rendered smoothly by\nthe YamKernel Browser Engine.\n\n[ Content not yet available in offline mode ]", 
            url_buffer);
        browser_content = generic_buf;
        strcpy(page_title, url_buffer);
        browser_status = "Done (Offline)";
    } else {
        browser_content = "Empty URL\nPlease enter a website address in the bar above.";
        strcpy(page_title, "New Tab");
        browser_status = "Error";
    }
    is_loading = false;
}

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
static bool shift_held = false;

void wl_browser_task(void *arg) {
    (void)arg;
    task_sleep_ms(300);
    
    wl_surface_t *s = wl_surface_create("YamBrowser", 150, 420, 600, 400, sched_current()->id);
    if (!s) return;
    
    draw_browser(s);
    wl_surface_commit(s);
    
    u32 my_id = s->id;
    
    while (s->state == WL_SURFACE_ACTIVE && s->id == my_id) {
        input_event_t ev;
        bool changed = false;
        while (wl_surface_pop_event(s, &ev)) {
            if (ev.type == EV_KEY) {
                u16 sc = ev.code;
                if (sc == 0x2A || sc == 0x36) {
                    shift_held = (ev.value == 1);
                    continue;
                }
                
                if (ev.value == 1) { // Pressed
                    if (sc < 128) {
                        char c = shift_held ? sc_ascii_shift[sc] : sc_ascii[sc];
                        if (c == '\n') {
                            browser_load_page(s);
                            changed = true;
                        } else if (c == '\b') {
                            int len = strlen(url_buffer);
                            if (len > 0) url_buffer[len-1] = '\0';
                            changed = true;
                        } else if (c >= 32 && c <= 126) {
                            int len = strlen(url_buffer);
                            if (len < 127) { 
                                url_buffer[len] = c; 
                                url_buffer[len+1] = '\0'; 
                            }
                            changed = true;
                        }
                    }
                }
            }
        }
        
        if (changed) {
            draw_browser(s);
            wl_surface_commit(s);
        }
        task_sleep_ms(16);
    }
}
