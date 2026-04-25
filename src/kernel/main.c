/* ============================================================================
 * YamKernel — Main Entry Point
 * 
 * This is where everything begins after the Limine bootloader hands off
 * control to the kernel in 64-bit long mode.
 *
 * YamKernel — A Graph-Based Adaptive Operating System
 * Copyright (C) 2026  
 * ============================================================================ */

#include <nexus/types.h>
#include <nexus/panic.h>
#include <limine.h>

#include "../cpu/gdt.h"
#include "../cpu/idt.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "../mem/heap.h"
#include "../drivers/serial.h"
#include "../drivers/framebuffer.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../nexus/graph.h"
#include "../nexus/capability.h"
#include "../nexus/channel.h"
#include "../drivers/pci.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../kernel/shell.h"

/* ============================================================================
 * Limine Requests — the bootloader fills these in before calling us
 * ============================================================================ */

LIMINE_BASE_REVISION(3)

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request fb_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_kernel_address_request kaddr_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};

/* ============================================================================
 * Boot Banner
 * ============================================================================ */

static void print_banner(void) {
    kprintf_color(0xFF00DDFF,
        "\n"
        "  ██╗   ██╗ █████╗ ███╗   ███╗\n"
        "  ╚██╗ ██╔╝██╔══██╗████╗ ████║\n"
        "   ╚████╔╝ ███████║██╔████╔██║\n"
        "    ╚██╔╝  ██╔══██║██║╚██╔╝██║\n"
        "     ██║   ██║  ██║██║ ╚═╝ ██║\n"
        "     ╚═╝   ╚═╝  ╚═╝╚═╝     ╚═╝\n"
    );
    kprintf_color(0xFFFFDD00,
        "  ╦╔═╔═╗╦═╗╔╗╔╔═╗╦  \n"
        "  ╠╩╗║╣ ╠╦╝║║║║╣ ║  \n"
        "  ╩ ╩╚═╝╩╚═╝╚╝╚═╝╩═╝\n"
    );
    kprintf_color(0xFF00FF88,
        "\n  YamKernel v0.2.0 — Graph-Based Adaptive OS\n");
    kprintf_color(0xFF888888,
        "  Architecture: x86_64 | Model: YamGraph Resource Graph\n"
        "  Built: " __DATE__ " " __TIME__ "\n\n");
}

/* ============================================================================
 * Kernel Main
 * ============================================================================ */

/* Entry point — called by Limine bootloader */
void kernel_main(void) {
    /* ---- Phase 1: Early console (serial only) ---- */
    serial_init();
    serial_write("\n[YAM] Serial console initialized\n");

    /* ---- Phase 2: Validate bootloader response ---- */
    if (limine_base_revision[2] != 0) {
        serial_write("[YAM] FATAL: Limine base revision mismatch!\n");
        for (;;) hlt();
    }

    /* ---- Phase 3: Framebuffer ---- */
    if (fb_request.response && fb_request.response->framebuffer_count > 0) {
        struct limine_framebuffer *fb = fb_request.response->framebuffers[0];
        fb_init(fb);
        kprintf("[FB] Framebuffer: %lux%lu @ %u bpp\n",
                fb->width, fb->height, fb->bpp);
    } else {
        serial_write("[YAM] WARNING: No framebuffer available\n");
    }

    /* ---- Print boot banner ---- */
    print_banner();

    /* ---- Phase 4: CPU Setup ---- */
    kprintf_color(0xFFFFDD00, "=== Phase 1: CPU Setup ===\n");
    gdt_init();
    idt_init();

    /* ---- Phase 5: Memory Management ---- */
    kprintf_color(0xFFFFDD00, "\n=== Phase 2: Memory (Cell Allocator) ===\n");
    
    if (!hhdm_request.response) {
        kpanic("HHDM request not fulfilled by bootloader");
    }
    u64 hhdm_offset = hhdm_request.response->offset;
    vmm_init(hhdm_offset);

    if (!memmap_request.response) {
        kpanic("Memory map request not fulfilled by bootloader");
    }
    pmm_init((void *)memmap_request.response, hhdm_offset);
    heap_init();

    /* ---- Phase 6: YamGraph Core ---- */
    kprintf_color(0xFFFFDD00, "\n=== Phase 3: YamGraph Resource Graph ===\n");
    yamgraph_init();

    /* Register kernel subsystems as graph nodes */
    yam_node_id_t pmm_node = yamgraph_node_create(YAM_NODE_NAMESPACE, "pmm", NULL);
    yam_node_id_t vmm_node = yamgraph_node_create(YAM_NODE_NAMESPACE, "vmm", NULL);
    yam_node_id_t heap_node = yamgraph_node_create(YAM_NODE_NAMESPACE, "heap", NULL);

    /* Link kernel → subsystems */
    yamgraph_edge_link(0, pmm_node,  YAM_EDGE_OWNS, YAM_PERM_ALL);
    yamgraph_edge_link(0, vmm_node,  YAM_EDGE_OWNS, YAM_PERM_ALL);
    yamgraph_edge_link(0, heap_node, YAM_EDGE_OWNS, YAM_PERM_ALL);

    kprintf("[YAMGRAPH] Kernel subsystems registered: nodes=%u, edges=%u\n",
            yamgraph_node_count(), yamgraph_edge_count());

    /* ---- Phase 7: Self-Tests ---- */
    kprintf_color(0xFFFFDD00, "\n=== Phase 4: Self-Tests ===\n");
    pmm_self_test();
    yamgraph_self_test();

    /* ---- Phase 8: System Ready ---- */
    kprintf_color(0xFF00FF88,
        "\n"
        "  ╔══════════════════════════════════════════════════╗\n"
        "  ║        YamKernel v0.2.0 — BOOT COMPLETE         ║\n"
        "  ╠══════════════════════════════════════════════════╣\n");
    kprintf_color(0xFF00DDFF,
        "  ║  Memory : %lu MB total, %lu MB free             \n",
        pmm_total_memory() / (1024 * 1024),
        pmm_free_memory() / (1024 * 1024));
    kprintf_color(0xFF00DDFF,
        "  ║  Graph  : %u nodes, %u edges                    \n",
        yamgraph_node_count(), yamgraph_edge_count());
    kprintf_color(0xFF00FF88,
        "  ║  Status : All subsystems operational             ║\n"
        "  ╚══════════════════════════════════════════════════╝\n"
        "\n");

    /* ---- Phase 8: Shell / Interactive Terminal ---- */
    kprintf_color(0xFF00DDFF, "\n=== Phase 5: Input & Terminal ===\n");
    pci_init();
    keyboard_init();
    mouse_init();

    /* Launch interactive REPL */
    shell_start();
}
