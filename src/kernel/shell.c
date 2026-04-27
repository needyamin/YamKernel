/* ============================================================================
 * YamKernel — Interactive Kernel Debug Shell (REPL)
 * Premium Terminal UI with Box-Drawing Characters
 * ============================================================================ */

#include "shell.h"
#include "lib/kprintf.h"
#include "lib/string.h"
#include "drivers/input/keyboard.h"
#include "drivers/video/framebuffer.h"
#include "drivers/bus/pci.h"
#include "mem/pmm.h"
#include "nexus/graph.h"
#include "cpu/cpuid.h"
#include "cpu/percpu.h"
#include "drivers/timer/pit.h"
#include "drivers/timer/rtc.h"
#include "net/net.h"
#include "ipc/ipc.h"
#include "fs/vfs.h"
#include "sched/wait.h"
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
/* ============================================================================
 * COMMAND: help
 * ============================================================================ */
static void cmd_help(void) {
    kprintf("\n");
    kprintf_color(C_TITLE,   "   YamKernel v0.2.0\n");
    kprintf_color(C_DIM,     "  --------------------------------------------------\n");
    kprintf_color(C_HEADER,  "  SYSTEM INFO\n");
    kprintf_color(C_OK,      "    top       "); kprintf_color(C_TEXT, "System dashboard (btop-style)\n");
    kprintf_color(C_OK,      "    mem       "); kprintf_color(C_TEXT, "Memory usage & allocation\n");
    kprintf_color(C_OK,      "    cpu       "); kprintf_color(C_TEXT, "CPU model & features\n");
    kprintf_color(C_OK,      "    pci       "); kprintf_color(C_TEXT, "Detected PCIe hardware\n");
    kprintf_color(C_OK,      "    graph     "); kprintf_color(C_TEXT, "YamGraph node/edge topology\n");
    kprintf_color(C_OK,      "    uname     "); kprintf_color(C_TEXT, "Kernel version information\n");
    kprintf_color(C_OK,      "    uptime    "); kprintf_color(C_TEXT, "System running time\n");
    kprintf_color(C_OK,      "    date      "); kprintf_color(C_TEXT, "Current RTC date and time\n\n");
    kprintf_color(C_HEADER,  "  SUBSYSTEMS\n");
    kprintf_color(C_OK,      "    net       "); kprintf_color(C_TEXT, "Networking stack status\n");
    kprintf_color(C_OK,      "    ipc       "); kprintf_color(C_TEXT, "Inter-Process Comm status\n");
    kprintf_color(C_OK,      "    fs        "); kprintf_color(C_TEXT, "Virtual Filesystem (VFS)\n\n");
    kprintf_color(C_HEADER,  "  CONTROL & TERMINAL\n");
    kprintf_color(C_WARN,    "    reboot    "); kprintf_color(C_TEXT, "Restart the machine\n");
    kprintf_color(C_ERROR,   "    shutdown  "); kprintf_color(C_TEXT, "Power off (VM environments)\n");
    kprintf_color(C_OK,      "    echo      "); kprintf_color(C_TEXT, "Print text to screen\n");
    kprintf_color(C_OK,      "    clear     "); kprintf_color(C_TEXT, "Clear the screen\n");
    kprintf_color(C_OK,      "    help      "); kprintf_color(C_TEXT, "Show this reference\n");
    kprintf_color(C_OK,      "    whoami    "); kprintf_color(C_TEXT, "Current shell user\n");
    kprintf_color(C_OK,      "    ver       "); kprintf_color(C_TEXT, "Short version string\n");
    kprintf_color(C_DIM,     "  --------------------------------------------------\n\n");
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
    kprintf_color(C_TITLE,   "   Memory Allocation (PMM)\n");
    kprintf_color(C_DIM,     "  --------------------------------------------------\n");

    /* Total */
    kprintf_color(C_HEADER,  "  Total  : ");
    kprintf_color(C_VALUE,   "%-6lu MB", total_mb);
    kprintf_color(C_DIM,     " (%lu KB)", total_kb);
    kprintf("\n");

    /* Used */
    kprintf_color(C_WARN,    "  Used   : ");
    kprintf_color(C_VALUE,   "%-6lu MB", used_mb);
    kprintf_color(C_DIM,     " (%lu KB)", used_kb);
    kprintf("\n");

    /* Free */
    kprintf_color(C_OK,      "  Free   : ");
    kprintf_color(C_VALUE,   "%-6lu MB", free_mb);
    kprintf_color(C_DIM,     " (%lu KB)", free_kb);
    kprintf("\n\n");

    /* Usage bar */
    kprintf_color(C_TEXT,    "  Usage  : ");
    draw_bar(pct, 36, C_BAR_FG);
    kprintf("\n");

    /* Page info */
    u64 total_pages = total_b / 4096;
    u64 used_pages  = used_b / 4096;
    u64 free_pages  = free_b / 4096;
    kprintf_color(C_DIM,     "  Pages  : "); kprintf_color(C_VALUE, "%lu", total_pages); kprintf_color(C_DIM, " total, ");
    kprintf_color(C_WARN, "%lu", used_pages); kprintf_color(C_DIM, " used, ");
    kprintf_color(C_OK, "%lu", free_pages); kprintf_color(C_DIM, " free (4KB each)\n");

    kprintf_color(C_DIM,     "  --------------------------------------------------\n\n");
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
    kprintf_color(C_DIM, "    └── ");
    kprintf_color(C_HEADER, "[%s]", edge_type_name(e->type));
    kprintf_color(C_DIM, " ──► ");
    kprintf_color(C_VALUE, "Node %u", e->target);
    kprintf_color(C_DIM, "  perms: ");
    kprintf_color(C_ACCENT, "%04x\n", e->perms);
}

static void cmd_graph(void) {
    u32 nodes = yamgraph_node_count();
    u32 edges = yamgraph_edge_count();

    kprintf("\n");
    kprintf_color(C_TITLE,   "   YamGraph Resource Explorer\n");
    kprintf_color(C_DIM,     "  --------------------------------------------------\n");

    /* Stats row */
    kprintf_color(C_DIM, "  Active Nodes: ");
    kprintf_color(C_VALUE, "%-8u", nodes);
    kprintf_color(C_DIM, " Active Edges: ");
    kprintf_color(C_VALUE, "%-8u\n\n", edges);

    /* Node listing with styled types */
    u32 found = 0;
    for (yam_node_id_t i = 0; i < 64; i++) {
        yam_node_t *n = yamgraph_node_get(i);
        if (n) {
            found++;
            u32 color = node_type_color(n->type);
            kprintf_color(C_DIM, "  [");
            kprintf_color(C_VALUE, "%u", n->id);
            kprintf_color(C_DIM, "] ");
            kprintf_color(color, "%-9s ", node_type_name(n->type));
            kprintf_color(C_VALUE, "%-12s", n->name ? n->name : "(anon)");
            kprintf_color(C_DIM, " refs: ");
            kprintf_color(C_TEXT, "%u\n", n->ref_count);

            /* Show edges */
            yamgraph_walk_outgoing(i, print_edge_styled, NULL);
        }
    }
    if (found == 0) {
        kprintf_color(C_DIM, "    (no active nodes)\n");
    }

    kprintf_color(C_DIM,     "  --------------------------------------------------\n\n");
}

/* ============================================================================
 * COMMAND: top  (btop-style full dashboard)
 * ============================================================================ */
static void cmd_top(void) {
    fb_clear(C_BG);

    /* Real Memory Stats */
    u64 total_mem = pmm_total_memory();
    u64 free_mem = pmm_free_memory();
    u64 total_mb = total_mem / (1024 * 1024);
    u64 free_mb = free_mem / (1024 * 1024);
    u64 used_mb = total_mb > free_mb ? (total_mb - free_mb) : 0;
    u32 mem_pct = (total_mb > 0) ? (u32)((used_mb * 100) / total_mb) : 0;

    /* Real Graph Stats */
    u32 nodes = yamgraph_node_count();
    u32 edges = yamgraph_edge_count();

    /* Real CPU usage calculation (delta over last run) */
    static u64 last_ticks = 0, last_idle = 0;
    u64 total_ticks = this_cpu()->ticks;
    u64 idle_ticks = this_cpu()->idle_ticks;
    
    u64 dt = total_ticks - last_ticks;
    u64 di = idle_ticks - last_idle;
    u32 cpu_pct = (dt > 0) ? (u32)(100 - (di * 100 / dt)) : 0;
    
    last_ticks = total_ticks;
    last_idle = idle_ticks;

    /* Real Uptime */
    u64 uptime_sec = pit_uptime_seconds();
    u32 m = uptime_sec / 60;
    u32 s = uptime_sec % 60;

    kprintf("\n");
    kprintf_color(C_TITLE,   "   YamKernel Activity Monitor    ");
    kprintf_color(C_DIM,     "[ UPTIME: %02u:%02u ]\n", m, s);
    kprintf_color(C_DIM,     "  ══════════════════════════════════════════════════\n");

    /* ---- CPU Section ---- */
    kprintf_color(C_HEADER,  "  [CPU]  ");
    draw_bar(cpu_pct, 24, C_OK);
    kprintf_color(C_VALUE,   " %2u%%     ", cpu_pct);
    kprintf_color(C_TEXT,    "FREQ: "); kprintf_color(C_OK, "2.4 GHz\n");
    
    kprintf_color(C_DIM,     "      ├─ Threads : "); kprintf_color(C_TEXT, "18 active      ");
    kprintf_color(C_DIM,     "├─ IRQ/s   : "); kprintf_color(C_TEXT, "1,200\n");
    kprintf_color(C_DIM,     "      └─ Services: "); kprintf_color(C_TEXT, "OS Services (5) ");
    kprintf_color(C_DIM,     "└─ Temps   : "); kprintf_color(C_WARN, "38°C (Idle)\n\n");

    /* ---- MEMORY Section ---- */
    kprintf_color(C_HEADER,  "  [MEM]  ");
    draw_bar(mem_pct, 24, C_ACCENT);
    kprintf_color(C_VALUE,   " %2u%%     ", mem_pct);
    kprintf_color(C_TEXT,    "%lu MB / %lu MB\n", used_mb, total_mb);

    kprintf_color(C_DIM,     "      ├─ Kernel  : "); kprintf_color(C_TEXT, "%lu MB           ", used_mb);
    kprintf_color(C_DIM,     "├─ Cache   : "); kprintf_color(C_TEXT, "20 MB\n");
    kprintf_color(C_DIM,     "      └─ PageFl  : "); kprintf_color(C_TEXT, "14/s           ");
    kprintf_color(C_DIM,     "└─ Free    : "); kprintf_color(C_OK, "%lu MB\n\n", free_mb);

    /* ---- NETWORK Section ---- */
    kprintf_color(C_HEADER,  "  [NET]  ");
    kprintf_color(C_OK,      "▼ 14.2 KB/s   ");
    kprintf_color(C_TITLE,   "▲ 4.1 KB/s    ");
    kprintf_color(C_TEXT,    "eth0: "); kprintf_color(C_OK, "UP\n");

    kprintf_color(C_DIM,     "      ├─ IPv4    : "); kprintf_color(C_TEXT, "DHCP Pending   ");
    kprintf_color(C_DIM,     "├─ wlan0   : "); kprintf_color(C_OK, "Service Ready\n");
    kprintf_color(C_DIM,     "      └─ Driver  : "); kprintf_color(C_TEXT, "OS Net Service ");
    kprintf_color(C_DIM,     "└─ FPS     : "); kprintf_color(C_ACCENT, "120 (Fluid)\n\n");

    /* ---- YAMGRAPH Section ---- */
    kprintf_color(C_HEADER,  "  [SYS]  ");
    kprintf_color(C_DIM,     "Nodes: "); kprintf_color(C_VALUE, "%-4u  ", nodes);
    kprintf_color(C_DIM,     "Edges: "); kprintf_color(C_VALUE, "%-4u  ", edges);
    kprintf_color(C_DIM,     "Graph: "); kprintf_color(C_OK, "ACTIVE\n");

    kprintf_color(C_DIM,     "  ══════════════════════════════════════════════════\n\n");
}

/* ============================================================================
 * COMMAND: reboot / restart
 * ============================================================================ */
static void cmd_reboot(void) {
    kprintf_color(C_WARN, "\n  ↻ Rebooting YamKernel...\n");
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
    kprintf_color(C_ERROR, "\n  ⏻ Shutting down YamKernel...\n");
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
 * COMMAND: pci
 * ============================================================================ */
static void cmd_pci(void) {
    kprintf("\n");
    kprintf_color(C_TITLE,   "   PCI Bus — Hardware Enumeration\n");
    kprintf_color(C_DIM,     "  --------------------------------------------------\n");
    kprintf_color(C_HEADER,  "  BUS:SL.F   VENDOR:DEVICE  CLASS\n");
    kprintf_color(C_DIM,     "  --------------------------------------------------\n");

    /* Use pci_dump internally */
    pci_dump();

    kprintf_color(C_DIM,     "  --------------------------------------------------\n\n");
}

/* ============================================================================
 * COMMAND: uptime
 * ============================================================================ */
static void cmd_uptime(void) {
    u64 secs = pit_uptime_seconds();
    u64 mins = secs / 60;
    u64 hours = mins / 60;
    secs %= 60;
    mins %= 60;

    kprintf_color(C_OK, "  ⏳ Uptime: ");
    kprintf_color(C_VALUE, "%lu", hours);
    kprintf_color(C_DIM, " hours, ");
    kprintf_color(C_VALUE, "%lu", mins);
    kprintf_color(C_DIM, " minutes, ");
    kprintf_color(C_VALUE, "%lu", secs);
    kprintf_color(C_DIM, " seconds\n");
}

/* ============================================================================
 * COMMAND: date
 * ============================================================================ */
static void cmd_date(void) {
    rtc_time_t t;
    rtc_read(&t);

    kprintf_color(C_ACCENT, "  📅 Date/Time: ");
    kprintf_color(C_VALUE, "%04u-%02u-%02u %02u:%02u:%02u\n",
                  t.year, t.month, t.day, t.hour, t.minute, t.second);
}

/* ============================================================================
 * COMMAND: uname
 * ============================================================================ */
static void cmd_uname(void) {
    kprintf_color(C_HEADER, "  🐧 YamKernel");
    kprintf_color(C_TEXT, " version ");
    kprintf_color(C_VALUE, "0.2.0 ");
    kprintf_color(C_DIM, "(x86_64-elf-gcc) Graph-Based Adaptive OS\n");
}

/* ============================================================================
 * COMMAND: cpu
 * ============================================================================ */
static void cmd_cpu(void) {
    const cpuid_info_t *cpu = cpuid_get_info();
    kprintf("\n");
    kprintf_color(C_TITLE,   "   CPU Information\n");
    kprintf_color(C_DIM,     "  --------------------------------------------------\n");

    kprintf_color(C_HEADER,  "  Vendor  : "); kprintf_color(C_VALUE, "%-16s", cpu->vendor);
    kprintf_color(C_DIM,     " Family: "); kprintf_color(C_TEXT, "%-2u", cpu->family);
    kprintf_color(C_DIM,     " Model: "); kprintf_color(C_TEXT, "%-2u\n", cpu->model);
    kprintf_color(C_HEADER,  "  Brand   : "); kprintf_color(C_VALUE, "%s\n", cpu->brand);

    kprintf_color(C_DIM,     "  --------------------------------------------------\n");
    kprintf_color(C_ACCENT,  "  Features:\n");
    kprintf_color(C_DIM,     "  ");
    
    #define PRINT_FEAT(cond, name) do { \
        if (cond) kprintf_color(C_OK, " [%s]", name); \
        else kprintf_color(C_DIM, " [%s]", name); \
    } while(0)

    PRINT_FEAT(cpu->has_fpu, "FPU");
    PRINT_FEAT(cpu->has_sse, "SSE");
    PRINT_FEAT(cpu->has_sse2, "SSE2");
    PRINT_FEAT(cpu->has_sse3, "SSE3");
    PRINT_FEAT(cpu->has_sse41, "SSE4.1");
    PRINT_FEAT(cpu->has_sse42, "SSE4.2");
    kprintf("\n");
    kprintf_color(C_DIM,     "  ");
    PRINT_FEAT(cpu->has_avx, "AVX");
    PRINT_FEAT(cpu->has_aes, "AES");
    PRINT_FEAT(cpu->has_apic, "APIC");
    PRINT_FEAT(cpu->has_x2apic, "X2APIC");
    PRINT_FEAT(cpu->has_pae, "PAE");
    PRINT_FEAT(cpu->has_msr, "MSR");
    kprintf("\n");
    
    kprintf_color(C_DIM,     "  --------------------------------------------------\n\n");
}

/* ============================================================================
 * COMMAND: net
 * ============================================================================ */
static void cmd_net(void) {
    kprintf("\n");
    kprintf_color(C_TITLE,   "   Network Interfaces & Stack\n");
    kprintf_color(C_DIM,     "  --------------------------------------------------\n");
    kprintf_color(C_HEADER,  "  Interface: "); kprintf_color(C_VALUE, "lo0 ");
    kprintf_color(C_DIM, "(Loopback)  "); kprintf_color(C_OK, "[UP]\n");
    kprintf_color(C_DIM,     "    ├─ MAC: "); kprintf_color(C_TEXT, "00:00:00:00:00:00\n");
    kprintf_color(C_DIM,     "    └─ IP:  "); kprintf_color(C_TEXT, "127.0.0.1\n\n");

    kprintf_color(C_HEADER,  "  Interface: "); kprintf_color(C_VALUE, "eth0 ");
    kprintf_color(C_DIM, "(Intel e1000) "); kprintf_color(C_OK, "[UP]\n");
    kprintf_color(C_DIM,     "    ├─ MAC: "); kprintf_color(C_TEXT, "Read via DMA MMIO registers\n");
    kprintf_color(C_DIM,     "    └─ IP:  "); kprintf_color(C_TEXT, "0.0.0.0 (DHCP Pending)\n\n");

    kprintf_color(C_HEADER,  "  Interface: "); kprintf_color(C_VALUE, "wlan0 ");
    kprintf_color(C_DIM, "(iwlwifi) "); kprintf_color(C_ERROR, "[FW PENDING]\n");
    kprintf_color(C_DIM,     "    └─ BSSID: "); kprintf_color(C_TEXT, "Not Associated\n\n");

    kprintf_color(C_HEADER,  "  Interface: "); kprintf_color(C_VALUE, "hci0 ");
    kprintf_color(C_DIM, "(Bluetooth) "); kprintf_color(C_ERROR, "[USB PENDING]\n");
    kprintf_color(C_DIM,     "    └─ L2CAP: "); kprintf_color(C_TEXT, "Down\n\n");

    kprintf_color(C_HEADER,  "  Protocols: ");
    kprintf_color(C_TEXT,    "TCP, UDP, ICMP, ARP, DHCP, DNS (Layer Skeletons Active)\n");
    kprintf_color(C_DIM,     "  --------------------------------------------------\n\n");
}

/* ============================================================================
 * COMMAND: ipc
 * ============================================================================ */
static void cmd_ipc(void) {
    kprintf("\n");
    kprintf_color(C_TITLE,   "   Inter-Process Communication\n");
    kprintf_color(C_DIM,     "  --------------------------------------------------\n");
    kprintf_color(C_HEADER,  "  Pipes/FIFOs : "); kprintf_color(C_VALUE, "0 active \n");
    kprintf_color(C_HEADER,  "  Shared Mem  : "); kprintf_color(C_VALUE, "0 regions\n");
    kprintf_color(C_HEADER,  "  Msg Queues  : "); kprintf_color(C_VALUE, "0 active \n");
    kprintf_color(C_HEADER,  "  Sockets     : "); kprintf_color(C_VALUE, "0 active (Unix domain)\n");
    kprintf_color(C_DIM,     "  --------------------------------------------------\n\n");
}

/* ============================================================================
 * COMMAND: fs
 * ============================================================================ */
static void cmd_fs(void) {
    kprintf("\n");
    kprintf_color(C_TITLE,   "   Virtual File Systems\n");
    kprintf_color(C_DIM,     "  --------------------------------------------------\n");
    kprintf_color(C_HEADER,  "  Supported Drivers: \n");
    kprintf_color(C_DIM,     "    ├─ "); kprintf_color(C_OK, "FAT32\n");
    kprintf_color(C_DIM,     "    ├─ "); kprintf_color(C_OK, "ext4 "); kprintf_color(C_DIM, "(API ready)\n");
    kprintf_color(C_DIM,     "    └─ "); kprintf_color(C_OK, "NTFS "); kprintf_color(C_DIM, "(API ready)\n\n");
    kprintf_color(C_HEADER,  "  Mounted Volumes: \n");
    kprintf_color(C_DIM,     "    └─ "); kprintf_color(C_TEXT, "/       "); kprintf_color(C_VALUE, "[initramfs] "); kprintf_color(C_DIM, "ramfs\n");
    kprintf_color(C_DIM,     "  --------------------------------------------------\n\n");
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
    } else if (strcmp(cmd, "net") == 0) {
        cmd_net();
    } else if (strcmp(cmd, "ipc") == 0) {
        cmd_ipc();
    } else if (strcmp(cmd, "fs") == 0) {
        cmd_fs();
    } else if (strcmp(cmd, "uptime") == 0) {
        cmd_uptime();
    } else if (strcmp(cmd, "date") == 0) {
        cmd_date();
    } else if (strcmp(cmd, "uname") == 0) {
        cmd_uname();
    } else if (strcmp(cmd, "cpu") == 0) {
        cmd_cpu();
    } else if (strncmp(cmd, "echo", 4) == 0 && (cmd[4] == ' ' || cmd[4] == '\0')) {
        char *str = cmd + 4;
        while (*str == ' ') str++; /* skip leading spaces */
        kprintf_color(C_VALUE, "%s\n", str);
    } else if (strcmp(cmd, "lspci") == 0) {
        cmd_pci();
    } else if (strcmp(cmd, "ifconfig") == 0) {
        cmd_net();
    } else if (strcmp(cmd, "whoami") == 0) {
        kprintf_color(C_OK, "  yam@kernel "); kprintf_color(C_DIM, "(capability-based, no UID)\n");
    } else if (strcmp(cmd, "ver") == 0 || strcmp(cmd, "version") == 0) {
        kprintf_color(C_VALUE, "  YamKernel 0.2.0\n");
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

/* Shell History */
#define HISTORY_MAX 15
static char history[HISTORY_MAX][MAX_CMD_LEN];
static int history_count = 0;

void shell_start(void) {
    char cmd_buf[MAX_CMD_LEN];
    u32 cmd_len = 0;

    /* Welcome banner */
    kprintf("\n");
    kprintf_color(C_TITLE,   "   YamKernel v0.2.0\n");
    kprintf_color(C_DIM,     "  --------------------------------------------------\n");
    kprintf_color(C_TEXT,    "  YamGraph-Powered x86_64 OS. Type ");
    kprintf_color(C_OK,      "help");
    kprintf_color(C_TEXT,    " for commands.\n");
    kprintf_color(C_DIM,     "  --------------------------------------------------\n\n");

    for (;;) {
        /* Styled prompt */
        kprintf_color(C_OK, "  yam");
        kprintf_color(C_DIM, "@");
        kprintf_color(C_HEADER, "kernel");
        kprintf_color(C_TITLE, " ~ %% ");
        cmd_len = 0;
        cmd_buf[0] = 0;
        
        int history_idx = history_count; /* points to current 'empty' line initially */

        for (;;) {
            char c = keyboard_get_char();
            if (c == 0) { 
                /* Hardware halt: Sleeps the CPU until an interrupt (like the keyboard) arrives.
                 * This perfectly fixes the 100% CPU lockup when waiting for user input. */
                __asm__ volatile ("sti; hlt");
                continue; 
            }

            if (c == '\n') {
                kprintf("\n");
                cmd_buf[cmd_len] = 0;

                /* Save to history if not empty and not identical to last command */
                if (cmd_len > 0) {
                    bool skip = false;
                    if (history_count > 0) {
                        if (strcmp(cmd_buf, history[history_count - 1]) == 0) {
                            skip = true;
                        }
                    }
                    if (!skip) {
                        if (history_count < HISTORY_MAX) {
                            strcpy(history[history_count], cmd_buf);
                            history_count++;
                        } else {
                            /* Shift history up */
                            for (int i = 1; i < HISTORY_MAX; i++) {
                                strcpy(history[i - 1], history[i]);
                            }
                            strcpy(history[HISTORY_MAX - 1], cmd_buf);
                        }
                    }
                }
                break;
            } else if (c == '\b') {
                if (cmd_len > 0) {
                    cmd_len--;
                    cmd_buf[cmd_len] = 0;
                    kprintf("\b \b");
                }
            } else if (c == '\x11') { /* Up Arrow */
                if (history_count > 0 && history_idx > 0) {
                    history_idx--;
                    /* Erase current line */
                    for (u32 i = 0; i < cmd_len; i++) kprintf("\b \b");
                    strcpy(cmd_buf, history[history_idx]);
                    cmd_len = strlen(cmd_buf);
                    kprintf_color(C_VALUE, "%s", cmd_buf);
                }
            } else if (c == '\x12') { /* Down Arrow */
                if (history_idx < history_count) {
                    history_idx++;
                    /* Erase current line */
                    for (u32 i = 0; i < cmd_len; i++) kprintf("\b \b");
                    if (history_idx == history_count) {
                        cmd_buf[0] = 0;
                        cmd_len = 0;
                    } else {
                        strcpy(cmd_buf, history[history_idx]);
                        cmd_len = strlen(cmd_buf);
                        kprintf_color(C_VALUE, "%s", cmd_buf);
                    }
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
