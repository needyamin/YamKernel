/* ============================================================================
 * YamKernel — Interactive Kernel Debug Shell (REPL)
 * Premium Terminal UI with Box-Drawing Characters
 * ============================================================================ */

#include "shell.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../drivers/keyboard.h"
#include "../drivers/framebuffer.h"
#include "../drivers/pci.h"
#include "../mem/pmm.h"
#include "../nexus/graph.h"
#include <nexus/types.h>

#define MAX_CMD_LEN 256

/* ---- Color Palette ---- */
#define C_BORDER  0xFF00FF88   /* Neon green borders */
#define C_TITLE   0xFFFFDD00   /* Gold titles */
#define C_HEADER  0xFF00DDFF   /* Cyan headers */
#define C_TEXT    0xFFCCCCCC   /* Light gray text */
#define C_DIM     0xFF666666   /* Dim gray */
#define C_VALUE   0xFFFFFFFF   /* Bright white values */
#define C_ACCENT  0xFFEE55FF   /* Purple accent */
#define C_BAR_FG  0xFF00FF00   /* Green bar fill */
#define C_BAR_BG  0xFF333333   /* Dark bar background */
#define C_WARN    0xFFFF8833   /* Orange warning */
#define C_ERROR   0xFFFF3333   /* Red error */
#define C_OK      0xFF00FF88   /* Green OK */
#define C_BG      0xFF0A0A14   /* Dark background */

/* ---- Helper: Draw a horizontal separator ---- */
static void draw_line(int width) {
    kprintf_color(C_BORDER, "  ");
    for (int i = 0; i < width; i++) kprintf_color(C_BORDER, "─");
    kprintf("\n");
}

/* ---- Helper: Draw a progress bar ---- */
static void draw_bar(u32 percent, int width, u32 fill_color) {
    kprintf_color(C_DIM, " [");
    int filled = (percent * width) / 100;
    for (int i = 0; i < width; i++) {
        if (i < filled) {
            /* Gradient: green → yellow → red based on position */
            if (percent > 90)       kprintf_color(C_ERROR, "█");
            else if (percent > 70)  kprintf_color(C_WARN,  "█");
            else                    kprintf_color(fill_color, "█");
        } else {
            kprintf_color(C_BAR_BG, "░");
        }
    }
    kprintf_color(C_DIM, "] ");
    if (percent > 90)       kprintf_color(C_ERROR, "%u%%", percent);
    else if (percent > 70)  kprintf_color(C_WARN,  "%u%%", percent);
    else                    kprintf_color(C_OK,    "%u%%", percent);
}

/* ============================================================================
 * COMMAND: help
 * ============================================================================ */
static void cmd_help(void) {
    kprintf("\n");
    kprintf_color(C_BORDER, "  ╔══════════════════════════════════════════════════════╗\n");
    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_TITLE, " ⚡ YamKernel v0.2.0 — Command Reference");
    kprintf_color(C_BORDER,                                                      "           ║\n");
    kprintf_color(C_BORDER, "  ╠══════════════════════════════════════════════════════╣\n");
    kprintf_color(C_BORDER, "  ║                                                      ║\n");
    kprintf_color(C_BORDER, "  ║"); kprintf_color(C_HEADER, "  DIAGNOSTICS");
    kprintf_color(C_BORDER,                                "                                       ║\n");
    kprintf_color(C_BORDER, "  ║"); kprintf_color(C_OK, "    top");
    kprintf_color(C_DIM,             "      "); kprintf_color(C_TEXT, "System dashboard (btop-style)");
    kprintf_color(C_BORDER,                                                        "      ║\n");
    kprintf_color(C_BORDER, "  ║"); kprintf_color(C_OK, "    mem");
    kprintf_color(C_DIM,             "      "); kprintf_color(C_TEXT, "Memory usage & allocation stats");
    kprintf_color(C_BORDER,                                                        "    ║\n");
    kprintf_color(C_BORDER, "  ║"); kprintf_color(C_OK, "    pci");
    kprintf_color(C_DIM,             "      "); kprintf_color(C_TEXT, "Detected PCI hardware devices");
    kprintf_color(C_BORDER,                                                        "     ║\n");
    kprintf_color(C_BORDER, "  ║"); kprintf_color(C_OK, "    graph");
    kprintf_color(C_DIM,             "    "); kprintf_color(C_TEXT, "YamGraph node/edge topology");
    kprintf_color(C_BORDER,                                                        "       ║\n");
    kprintf_color(C_BORDER, "  ║                                                      ║\n");
    kprintf_color(C_BORDER, "  ║"); kprintf_color(C_HEADER, "  SYSTEM CONTROL");
    kprintf_color(C_BORDER,                                "                                    ║\n");
    kprintf_color(C_BORDER, "  ║"); kprintf_color(C_WARN, "    reboot");
    kprintf_color(C_DIM,             "   "); kprintf_color(C_TEXT, "Restart the machine (alias: restart)");
    kprintf_color(C_BORDER,                                                        "║\n");
    kprintf_color(C_BORDER, "  ║"); kprintf_color(C_ERROR, "    shutdown");
    kprintf_color(C_DIM,             " "); kprintf_color(C_TEXT, "Power off (VM environments)");
    kprintf_color(C_BORDER,                                                        "        ║\n");
    kprintf_color(C_BORDER, "  ║                                                      ║\n");
    kprintf_color(C_BORDER, "  ║"); kprintf_color(C_HEADER, "  TERMINAL");
    kprintf_color(C_BORDER,                                "                                          ║\n");
    kprintf_color(C_BORDER, "  ║"); kprintf_color(C_OK, "    clear");
    kprintf_color(C_DIM,             "    "); kprintf_color(C_TEXT, "Clear the screen");
    kprintf_color(C_BORDER,                                                        "                 ║\n");
    kprintf_color(C_BORDER, "  ║"); kprintf_color(C_OK, "    help");
    kprintf_color(C_DIM,             "     "); kprintf_color(C_TEXT, "Show this reference");
    kprintf_color(C_BORDER,                                                        "              ║\n");
    kprintf_color(C_BORDER, "  ║                                                      ║\n");
    kprintf_color(C_BORDER, "  ╚══════════════════════════════════════════════════════╝\n\n");
}

/* ============================================================================
 * COMMAND: mem
 * ============================================================================ */
static void cmd_mem(void) {
    u64 total_b = pmm_total_memory();
    u64 free_b  = pmm_free_memory();
    u64 used_b  = total_b - free_b;

    u64 total_mb = total_b / (1024 * 1024);
    u64 free_mb  = free_b / (1024 * 1024);
    u64 used_mb  = used_b / (1024 * 1024);
    u64 total_kb = total_b / 1024;
    u64 free_kb  = free_b / 1024;
    u64 used_kb  = used_b / 1024;
    u32 pct = (total_b > 0) ? (u32)((used_b * 100) / total_b) : 0;

    kprintf("\n");
    kprintf_color(C_BORDER, "  ╔══════════════════════════════════════════════════════╗\n");
    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_TITLE, " 🧠 Cell Allocator — Memory Dashboard");
    kprintf_color(C_BORDER,                                                      "            ║\n");
    kprintf_color(C_BORDER, "  ╠══════════════════════════════════════════════════════╣\n");

    /* Total */
    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_HEADER, " Total  :");
    kprintf_color(C_VALUE,  " %-6lu MB", total_mb);
    kprintf_color(C_DIM,    " (%lu KB)", total_kb);
    kprintf("\n");

    /* Used */
    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_WARN,   " Used   :");
    kprintf_color(C_VALUE,  " %-6lu MB", used_mb);
    kprintf_color(C_DIM,    " (%lu KB)", used_kb);
    kprintf("\n");

    /* Free */
    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_OK,     " Free   :");
    kprintf_color(C_VALUE,  " %-6lu MB", free_mb);
    kprintf_color(C_DIM,    " (%lu KB)", free_kb);
    kprintf("\n");

    /* Usage bar */
    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_TEXT, " Usage  :");
    draw_bar(pct, 36, C_BAR_FG);
    kprintf("\n");

    /* Page info */
    u64 total_pages = total_b / 4096;
    u64 used_pages  = used_b / 4096;
    u64 free_pages  = free_b / 4096;
    kprintf_color(C_BORDER, "  ║ ");
    kprintf_color(C_DIM, "  Pages: %lu total, ", total_pages);
    kprintf_color(C_WARN, "%lu used", used_pages);
    kprintf_color(C_DIM, ", ");
    kprintf_color(C_OK, "%lu free", free_pages);
    kprintf_color(C_DIM, " (4KB each)");
    kprintf("\n");

    kprintf_color(C_BORDER, "  ╚══════════════════════════════════════════════════════╝\n\n");
}

/* ============================================================================
 * COMMAND: graph
 * ============================================================================ */

/* Type name lookup */
static const char* node_type_name(u32 type) {
    switch (type) {
        case 0: return "TASK";
        case 1: return "MEMORY";
        case 2: return "DEVICE";
        case 3: return "FILE";
        case 4: return "CHANNEL";
        case 5: return "IRQ";
        case 6: return "NAMESPACE";
        default: return "UNKNOWN";
    }
}

static const char* edge_type_name(u32 type) {
    switch (type) {
        case 0: return "OWNS";
        case 1: return "CAPABILITY";
        case 2: return "CHANNEL";
        case 3: return "MAPS";
        case 4: return "DEPENDS";
        default: return "LINK";
    }
}

static u32 node_type_color(u32 type) {
    switch (type) {
        case 0: return C_OK;       /* TASK - green */
        case 1: return C_ACCENT;   /* MEMORY - purple */
        case 2: return C_WARN;     /* DEVICE - orange */
        case 3: return C_HEADER;   /* FILE - cyan */
        case 4: return C_TITLE;    /* CHANNEL - gold */
        case 5: return C_ERROR;    /* IRQ - red */
        case 6: return 0xFF88AAFF; /* NAMESPACE - light blue */
        default: return C_DIM;
    }
}

/* Callback for YamGraph edge traversal */
static void print_edge_styled(__attribute__((unused)) yam_node_t *target, yam_edge_t *e, void *ctx) {
    (void)ctx;
    kprintf_color(C_BORDER, "  ║   ");
    kprintf_color(C_DIM, "└── ");
    kprintf_color(C_HEADER, "[%s]", edge_type_name(e->type));
    kprintf_color(C_DIM, " ──► ");
    kprintf_color(C_VALUE, "Node %u", e->target);
    kprintf_color(C_DIM, "  perms:");
    kprintf_color(C_ACCENT, "%04x", e->perms);
    kprintf("\n");
}

static void cmd_graph(void) {
    u32 nodes = yamgraph_node_count();
    u32 edges = yamgraph_edge_count();

    kprintf("\n");
    kprintf_color(C_BORDER, "  ╔══════════════════════════════════════════════════════╗\n");
    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_TITLE, " 🔗 YamGraph — Resource Graph Explorer");
    kprintf_color(C_BORDER,                                                      "            ║\n");
    kprintf_color(C_BORDER, "  ╠══════════════════════════════════════════════════════╣\n");

    /* Stats row */
    kprintf_color(C_BORDER, "  ║ ");
    kprintf_color(C_TEXT, " Active Nodes: ");
    kprintf_color(C_VALUE, "%-8u", nodes);
    kprintf_color(C_TEXT, " Active Edges: ");
    kprintf_color(C_VALUE, "%-8u", edges);
    kprintf("\n");
    draw_line(54);

    /* Node listing with styled types */
    u32 found = 0;
    for (yam_node_id_t i = 0; i < 64; i++) {
        yam_node_t *n = yamgraph_node_get(i);
        if (n) {
            found++;
            u32 color = node_type_color(n->type);
            kprintf_color(C_BORDER, "  ║ ");
            kprintf_color(C_DIM, " [");
            kprintf_color(C_VALUE, "%u", n->id);
            kprintf_color(C_DIM, "] ");
            kprintf_color(color, "%-9s ", node_type_name(n->type));
            kprintf_color(C_VALUE, "%-12s", n->name ? n->name : "(anon)");
            kprintf_color(C_DIM, " refs:");
            kprintf_color(C_TEXT, "%u", n->ref_count);
            kprintf("\n");

            /* Show edges */
            yamgraph_walk_outgoing(i, print_edge_styled, NULL);
        }
    }
    if (found == 0) {
        kprintf_color(C_BORDER, "  ║ ");
        kprintf_color(C_DIM, "  (no active nodes)\n");
    }

    kprintf_color(C_BORDER, "  ╚══════════════════════════════════════════════════════╝\n\n");
}

/* ============================================================================
 * COMMAND: top  (btop-style full dashboard)
 * ============================================================================ */
static void cmd_top(void) {
    fb_clear(C_BG);

    u64 total_mem = pmm_total_memory();
    u64 free_mem = pmm_free_memory();
    u64 total_mb = total_mem / (1024 * 1024);
    u64 free_mb = free_mem / (1024 * 1024);
    u64 used_mb = total_mb - free_mb;
    u32 pct = (total_mb > 0) ? (u32)((used_mb * 100) / total_mb) : 0;
    u32 nodes = yamgraph_node_count();
    u32 edges = yamgraph_edge_count();

    /* ---- Title Bar ---- */
    kprintf_color(C_BORDER, "\n  ╔══════════════════════════════════════════════════════════╗\n");
    kprintf_color(C_BORDER, "  ║"); kprintf_color(C_TITLE, "  ⚡ YamKernel v0.2.0 — System Monitor");
    kprintf_color(C_BORDER,                                                       "                  ║\n");
    kprintf_color(C_BORDER, "  ╠═══════════════════════════╦════════════════════════════════╣\n");

    /* ---- Left Panel: System Info ---- */
    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_ACCENT, " SYSTEM INFO");
    kprintf_color(C_BORDER, "              ║ "); kprintf_color(C_ACCENT, " YAMGRAPH ENGINE");
    kprintf_color(C_BORDER, "              ║\n");

    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_DIM, " Kernel  :"); kprintf_color(C_VALUE, " YamKernel");
    kprintf_color(C_BORDER, "       ║ "); kprintf_color(C_DIM, " Nodes   :"); kprintf_color(C_OK, " %-5u", nodes);
    kprintf_color(C_BORDER, "              ║\n");

    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_DIM, " Version :"); kprintf_color(C_HEADER, " v0.2.0");
    kprintf_color(C_BORDER, "         ║ "); kprintf_color(C_DIM, " Edges   :"); kprintf_color(C_OK, " %-5u", edges);
    kprintf_color(C_BORDER, "              ║\n");

    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_DIM, " Arch    :"); kprintf_color(C_TEXT, " x86_64");
    kprintf_color(C_BORDER, "         ║ "); kprintf_color(C_DIM, " Status  :"); kprintf_color(C_OK, " ACTIVE");
    kprintf_color(C_BORDER, "             ║\n");

    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_DIM, " Sched   :"); kprintf_color(C_TEXT, " Flow Graph");
    kprintf_color(C_BORDER, "     ║ "); kprintf_color(C_DIM, " Model   :"); kprintf_color(C_TEXT, " Adaptive DAG");
    kprintf_color(C_BORDER, "          ║\n");

    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_DIM, " Boot    :"); kprintf_color(C_TEXT, " Limine v8");
    kprintf_color(C_BORDER, "      ║ "); kprintf_color(C_DIM, " Safety  :"); kprintf_color(C_OK, " Capability");
    kprintf_color(C_BORDER, "           ║\n");

    kprintf_color(C_BORDER, "  ╠═══════════════════════════╩════════════════════════════════╣\n");

    /* ---- Memory Section ---- */
    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_ACCENT, " CELL ALLOCATOR (PHYSICAL MEMORY)");
    kprintf_color(C_BORDER, "                        ║\n");

    kprintf_color(C_BORDER, "  ║ ");
    kprintf_color(C_DIM, " Total: "); kprintf_color(C_VALUE, "%-5lu MB", total_mb);
    kprintf_color(C_DIM, "  Used: "); kprintf_color(C_WARN, "%-5lu MB", used_mb);
    kprintf_color(C_DIM, "  Free: "); kprintf_color(C_OK, "%-5lu MB", free_mb);
    kprintf_color(C_BORDER, "      ║\n");

    /* Memory bar */
    kprintf_color(C_BORDER, "  ║ ");
    draw_bar(pct, 50, C_BAR_FG);
    kprintf_color(C_BORDER, "    ║\n");

    kprintf_color(C_BORDER, "  ╠═══════════════════════════════════════════════════════════╣\n");

    /* ---- Drivers Section ---- */
    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_ACCENT, " ACTIVE DRIVERS");
    kprintf_color(C_BORDER, "                                           ║\n");

    kprintf_color(C_BORDER, "  ║   ");
    kprintf_color(C_OK, "●"); kprintf_color(C_TEXT, " PS/2 Keyboard (IRQ 1)");
    kprintf_color(C_DIM, "       ");
    kprintf_color(C_OK, "●"); kprintf_color(C_TEXT, " Serial UART (COM1)");
    kprintf_color(C_BORDER, "  ║\n");

    kprintf_color(C_BORDER, "  ║   ");
    kprintf_color(C_OK, "●"); kprintf_color(C_TEXT, " PS/2 Mouse (IRQ 12)");
    kprintf_color(C_DIM, "        ");
    kprintf_color(C_OK, "●"); kprintf_color(C_TEXT, " Framebuffer (32bpp)");
    kprintf_color(C_BORDER, " ║\n");

    kprintf_color(C_BORDER, "  ║   ");
    kprintf_color(C_OK, "●"); kprintf_color(C_TEXT, " PCI Bus Scanner");
    kprintf_color(C_BORDER, "                                       ║\n");

    kprintf_color(C_BORDER, "  ╠═══════════════════════════════════════════════════════════╣\n");

    /* ---- Subsystems Section ---- */
    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_ACCENT, " KERNEL SUBSYSTEMS");
    kprintf_color(C_BORDER, "                                        ║\n");

    kprintf_color(C_BORDER, "  ║   ");
    kprintf_color(C_OK, "▸"); kprintf_color(C_TEXT, " GDT/TSS  ");
    kprintf_color(C_OK, "▸"); kprintf_color(C_TEXT, " IDT/PIC  ");
    kprintf_color(C_OK, "▸"); kprintf_color(C_TEXT, " PMM  ");
    kprintf_color(C_OK, "▸"); kprintf_color(C_TEXT, " VMM  ");
    kprintf_color(C_OK, "▸"); kprintf_color(C_TEXT, " Heap  ");
    kprintf_color(C_OK, "▸"); kprintf_color(C_TEXT, " Graph");
    kprintf_color(C_BORDER, "  ║\n");

    kprintf_color(C_BORDER, "  ╚═══════════════════════════════════════════════════════════╝\n\n");
}

/* ============================================================================
 * COMMAND: reboot / restart
 * ============================================================================ */
static void cmd_reboot(void) {
    kprintf_color(C_BORDER, "\n  ╔═════════════════════════════════╗\n");
    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_WARN, " ↻  Rebooting YamKernel...");
    kprintf_color(C_BORDER, "       ║\n");
    kprintf_color(C_BORDER, "  ╚═════════════════════════════════╝\n");
    cli();

    /* Method 1: PS/2 keyboard controller reset */
    int timeout = 100000;
    while ((inb(0x64) & 0x02) && --timeout > 0);
    outb(0x64, 0xFE);
    for (volatile int i = 0; i < 1000000; i++);

    /* Method 2: ACPI reset via port 0xCF9 (VMware, QEMU, real HW) */
    outb(0xCF9, 0x02);
    for (volatile int i = 0; i < 100000; i++);
    outb(0xCF9, 0x06);
    for (volatile int i = 0; i < 100000; i++);

    /* Method 3: Triple fault (guaranteed) */
    struct { u16 limit; u64 base; } PACKED null_idt = {0, 0};
    __asm__ volatile ("lidt %0" :: "m"(null_idt));
    __asm__ volatile ("int $0x03");
    for (;;) __asm__ volatile ("cli; hlt");
}

/* ============================================================================
 * COMMAND: shutdown
 * ============================================================================ */
static void cmd_shutdown(void) {
    kprintf_color(C_BORDER, "\n  ╔═════════════════════════════════╗\n");
    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_ERROR, " ⏻  Shutting down YamKernel...");
    kprintf_color(C_BORDER, "   ║\n");
    kprintf_color(C_BORDER, "  ╚═════════════════════════════════╝\n");
    cli();

    outw(0x604, 0x2000);   /* QEMU (new) */
    for (volatile int i = 0; i < 100000; i++);
    outw(0xB004, 0x2000);  /* Bochs / older QEMU */
    for (volatile int i = 0; i < 100000; i++);
    outw(0x1004, 0x2000);  /* VMware (PM1a) */
    for (volatile int i = 0; i < 100000; i++);
    outw(0x600, 0x2000);   /* VMware (alt) */
    for (volatile int i = 0; i < 100000; i++);
    outw(0x4004, 0x3400);  /* VirtualBox */
    for (volatile int i = 0; i < 100000; i++);

    sti();
    kprintf_color(C_ERROR, "  ACPI shutdown failed (bare metal?). System halted.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

/* ============================================================================
 * COMMAND: pci (styled version — replaces pci_dump in shell context)
 * ============================================================================ */
static void cmd_pci(void) {
    kprintf("\n");
    kprintf_color(C_BORDER, "  ╔══════════════════════════════════════════════════════╗\n");
    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_TITLE, " 🔌 PCI Bus — Hardware Enumeration");
    kprintf_color(C_BORDER,                                                      "               ║\n");
    kprintf_color(C_BORDER, "  ╠══════════════════════════════════════════════════════╣\n");

    kprintf_color(C_BORDER, "  ║ ");
    kprintf_color(C_DIM, " BUS:SL.F  VENDOR:DEVICE  CLASS");
    kprintf("\n");
    draw_line(54);

    /* Use pci_dump internally but with our styled output */
    pci_dump();

    kprintf_color(C_BORDER, "  ╚══════════════════════════════════════════════════════╝\n\n");
}

/* ============================================================================
 * Command Parser
 * ============================================================================ */
static void parse_command(char *cmd) {
    /* Strip trailing spaces and \n */
    int len = 0;
    while (cmd[len]) len++;
    while (len > 0 && (cmd[len-1] == ' ' || cmd[len-1] == '\n')) {
        cmd[--len] = 0;
    }

    if (len == 0) return;

    if (strcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "clear") == 0) {
        fb_clear(C_BG);
    } else if (strcmp(cmd, "top") == 0) {
        cmd_top();
    } else if (strcmp(cmd, "pci") == 0) {
        cmd_pci();
    } else if (strcmp(cmd, "graph") == 0) {
        cmd_graph();
    } else if (strcmp(cmd, "mem") == 0) {
        cmd_mem();
    } else if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "restart") == 0) {
        cmd_reboot();
    } else if (strcmp(cmd, "shutdown") == 0) {
        cmd_shutdown();
    } else {
        kprintf_color(C_ERROR, "  ✗ Unknown command: ");
        kprintf_color(C_VALUE, "'%s'", cmd);
        kprintf_color(C_DIM, " — type ");
        kprintf_color(C_OK, "help");
        kprintf_color(C_DIM, " for commands\n");
    }
}

/* ============================================================================
 * Shell Entry Point
 * ============================================================================ */
void shell_start(void) {
    char cmd_buf[MAX_CMD_LEN];
    u32 cmd_len = 0;

    /* Welcome banner */
    kprintf_color(C_BORDER, "\n  ╔══════════════════════════════════════════════════════╗\n");
    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_TITLE, " ⚡ YamKernel Interactive Terminal");
    kprintf_color(C_BORDER,                                                      "                ║\n");
    kprintf_color(C_BORDER, "  ║ "); kprintf_color(C_TEXT, " Type "); kprintf_color(C_OK, "help");
    kprintf_color(C_TEXT, " for commands. "); kprintf_color(C_DIM, "YamGraph-Powered OS");
    kprintf_color(C_BORDER,                                                      "      ║\n");
    kprintf_color(C_BORDER, "  ╚══════════════════════════════════════════════════════╝\n\n");

    for (;;) {
        /* Styled prompt */
        kprintf_color(C_OK, "  yam");
        kprintf_color(C_ACCENT, "@");
        kprintf_color(C_HEADER, "kernel");
        kprintf_color(C_DIM, " ► ");
        cmd_len = 0;
        cmd_buf[0] = 0;

        for (;;) {
            char c = keyboard_get_char();

            if (c == '\n') {
                kprintf("\n");
                cmd_buf[cmd_len] = 0;
                break;
            } else if (c == '\b') {
                if (cmd_len > 0) {
                    cmd_len--;
                    cmd_buf[cmd_len] = 0;
                    kprintf("\b \b");
                }
            } else if (c >= ' ' && c <= '~' && cmd_len < MAX_CMD_LEN - 1) {
                cmd_buf[cmd_len++] = c;
                cmd_buf[cmd_len] = 0;
                /* Echo FAST */
                fb_putchar(c, C_VALUE, C_BG);
            }
        }

        parse_command(cmd_buf);
    }
}
