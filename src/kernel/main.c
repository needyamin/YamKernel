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
#include "../cpu/cpuid.h"
#include "../cpu/security.h"
#include "../cpu/acpi.h"
#include "../cpu/apic.h"
#include "../cpu/percpu.h"
#include "../cpu/smp.h"
#include "../cpu/syscall.h"
#include "../sched/sched.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "../mem/heap.h"
#include "../drivers/serial/serial.h"
#include "../drivers/video/framebuffer.h"
#include "../drivers/timer/pit.h"
#include "../drivers/timer/rtc.h"
#include "../drivers/bus/api.h"
#include "../net/net.h"
#include "../ipc/ipc.h"
#include "../fs/vfs.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../lib/kdebug.h"
#include "../nexus/graph.h"
#include "../nexus/capability.h"
#include "../nexus/channel.h"
#include "../drivers/bus/pci.h"
#include "../drivers/input/keyboard.h"
#include "../drivers/input/mouse.h"
#include "../drivers/input/evdev.h"
#include "../drivers/drm/drm.h"
#include "../wayland/compositor.h"
#include "../wayland/demo_client.h"
#include "../sched/wait.h"
#include "../kernel/shell.h"
#include "../boot/yamboot.h"

#define YAM_DEMO_TASKS 0
#define YAM_PREEMPTIVE 1
#define YAM_WAYLAND    1

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
static volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0,
    .flags = 0  /* 0 = default (x2APIC enabled if available) */
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

__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};

/* ============================================================================
 * Global module pointers
 * ============================================================================ */
static void *g_elf_module = NULL;
static usize g_elf_module_size = 0;
void *g_wallpaper_module = NULL;

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
    KINFO("BOOT", "=== YamKernel Boot Start ===");
    KINFO("BOOT", "Serial console initialized");

    /* ---- Phase 2: Validate bootloader response ---- */
    if (limine_base_revision[2] != 0) {
        KERR("BOOT", "Limine base revision mismatch! Halting.");
        for (;;) hlt();
    }
    KINFO("BOOT", "Limine base revision OK");

    /* ---- Phase 3: Framebuffer ---- */
    if (fb_request.response && fb_request.response->framebuffer_count > 0) {
        struct limine_framebuffer *fb = fb_request.response->framebuffers[0];
        fb_init(fb);
        KINFO("FB", "Framebuffer: %llux%llu @ %u bpp, addr=%p, pitch=%llu",
              fb->width, fb->height, fb->bpp, fb->address, fb->pitch);
    } else {
        KWARN("FB", "No framebuffer available!");
    }

    /* ---- YamBoot: custom boot menu (polled, no IDT yet) ---- */
    KINFO("BOOT", "Showing YamBoot menu...");
    yamboot_choice_t choice = yamboot_show();
    if (choice == YAMBOOT_REBOOT) {
        KINFO("BOOT", "User chose REBOOT");
        outb(0x64, 0xFE);
        for (;;) __asm__ volatile ("cli; hlt");
    }
    KINFO("BOOT", "User chose boot mode %d (safe=%d)", (int)choice, g_yamboot_safe);

    /* ---- Draw Windows-Style Splash Screen AFTER YamBoot ---- */
    KINFO("SPLASH", "--- Splash Screen Init ---");
    if (fb_request.response && fb_request.response->framebuffer_count > 0) {
        void *wallpaper_data = NULL;
        void *logo_data = NULL;

        if (module_request.response) {
            KINFO("MODULE", "Bootloader provided %llu module(s)",
                  module_request.response->module_count);
            for (u64 i = 0; i < module_request.response->module_count; i++) {
                struct limine_file *mod = module_request.response->modules[i];
                KINFO("MODULE", "  [%llu] path='%s' addr=%p size=%llu",
                      i, mod->path ? mod->path : "(null)",
                      mod->address, mod->size);

                int len = strlen(mod->path);
                if (len >= 13 && strcmp(mod->path + len - 13, "wallpaper.bin") == 0) {
                    wallpaper_data = mod->address;
                    g_wallpaper_module = mod->address;
                    KINFO("MODULE", "    -> matched WALLPAPER");
                } else if (len >= 8 && strcmp(mod->path + len - 8, "logo.bin") == 0) {
                    logo_data = mod->address;
                    KINFO("MODULE", "    -> matched LOGO");
                } else if (len >= 12 && strcmp(mod->path + len - 12, "test_app.elf") == 0) {
                    g_elf_module = mod->address;
                    g_elf_module_size = mod->size;
                    KINFO("MODULE", "    -> matched ELF APP");
                }
            }
        } else {
            KWARN("MODULE", "module_request.response is NULL — bootloader loaded no modules!");
        }

        KINFO("SPLASH", "wallpaper=%p  logo=%p", wallpaper_data, logo_data);

        if (wallpaper_data) {
            u32 *wp = (u32 *)wallpaper_data;
            KINFO("SPLASH", "wallpaper dimensions: %ux%u", wp[0], wp[1]);
        }
        if (logo_data) {
            u32 *lp = (u32 *)logo_data;
            KINFO("SPLASH", "logo dimensions: %ux%u", lp[0], lp[1]);
        }

        fb_enable_text(false);
        KINFO("SPLASH", "Drawing splash...");
        fb_draw_splash(wallpaper_data, logo_data);
        KINFO("SPLASH", "Splash drawn OK");
    } else {
        KWARN("SPLASH", "No framebuffer, skipping splash");
    }

    /* ---- Print boot banner (goes to serial since fb text is off) ---- */
    KINFO("BOOT", "--- Kernel Init Phases ---");
    print_banner();

    /* ---- Phase 4: CPU Setup ---- */
    KINFO("INIT", "Phase 1: CPU Setup");
    kprintf_color(0xFFFFDD00, "=== Phase 1: CPU Setup ===\n");
    gdt_init(0);
    KTRACE("INIT", "GDT OK");
    idt_init();
    KTRACE("INIT", "IDT OK");
    cpuid_init();
    KTRACE("INIT", "CPUID OK");
    security_init();
    KTRACE("INIT", "Security OK");

    /* ---- Phase 5: Memory Management ---- */
    KINFO("INIT", "Phase 2: Memory Management");
    kprintf_color(0xFFFFDD00, "\n=== Phase 2: Memory (Cell Allocator) ===\n");
    
    if (!hhdm_request.response) {
        kpanic("HHDM request not fulfilled by bootloader");
    }
    u64 hhdm_offset = hhdm_request.response->offset;
    vmm_init(hhdm_offset);
    KTRACE("INIT", "VMM OK");

    if (!memmap_request.response) {
        kpanic("Memory map request not fulfilled by bootloader");
    }
    pmm_init((void *)memmap_request.response, hhdm_offset);
    KTRACE("INIT", "PMM OK");
    heap_init();
    KTRACE("INIT", "Heap OK");

    /* ---- Phase 5b: Modern interrupt + SMP topology ---- */
    KINFO("INIT", "Phase 2b: ACPI / APIC / SMP");
    kprintf_color(0xFFFFDD00, "\n=== Phase 2b: ACPI / APIC / SMP ===\n");
    acpi_init(rsdp_request.response ? rsdp_request.response->address : NULL,
              hhdm_offset);
    KTRACE("INIT", "ACPI OK");
    apic_init(hhdm_offset);
    KTRACE("INIT", "APIC OK");
    ioapic_init(hhdm_offset);
    KTRACE("INIT", "IOAPIC OK");
    
    /* Route legacy ISA IRQs to BSP (LAPIC 0) */
    ioapic_set_irq(1, 33, 0);  /* Keyboard -> Vector 33 */
    ioapic_set_irq(12, 44, 0); /* PS/2 Mouse -> Vector 44 */
    
    percpu_init(0, 0);
    KTRACE("INIT", "PerCPU OK");
    smp_init(smp_request.response);
    KTRACE("INIT", "SMP OK");
    syscall_init();
    KTRACE("INIT", "Syscall OK");

    /* ---- Phase 6: YamGraph Core ---- */
    KINFO("INIT", "Phase 3: YamGraph Resource Graph");
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
    KINFO("INIT", "Phase 4: Self-Tests");
    kprintf_color(0xFFFFDD00, "\n=== Phase 4: Self-Tests ===\n");
    pmm_self_test();
    yamgraph_self_test();
    KINFO("INIT", "Self-tests passed");

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
    KINFO("INIT", "Phase 5: Drivers, Subsystems & Input");
    kprintf_color(0xFF00DDFF, "\n=== Phase 5: Drivers, Subsystems & Input ===\n");
    pit_init(100);
    keyboard_init();

    if (!g_yamboot_safe) {
        KTRACE("INIT", "Initializing PCI...");
        pci_init();
        KTRACE("INIT", "PCI OK. Initializing USB...");
        usb_init();
        KTRACE("INIT", "USB OK. Initializing I2C...");
        i2c_init();
        KTRACE("INIT", "I2C OK. Initializing SPI...");
        spi_init();
        KTRACE("INIT", "SPI OK. Initializing VFS...");
        vfs_init();
        KTRACE("INIT", "VFS OK. Initializing IPC...");
        ipc_init();
        KTRACE("INIT", "IPC OK. Initializing NET...");
        net_init();
        KTRACE("INIT", "NET OK. Initializing Evdev...");
        evdev_init();
        KTRACE("INIT", "Evdev OK. Initializing Mouse...");
        mouse_init();
        KTRACE("INIT", "Mouse OK");
    } else {
        kprintf_color(0xFFFF8833, "[YAMBOOT] Safe Mode: skipping PCI/USB/I2C/SPI/VFS/IPC/NET/MOUSE\n");
    }

    /* ---- Phase 9: Preemptive Multitasking ---- */
    KINFO("INIT", "Phase 6: Preemptive Scheduler");
    kprintf_color(0xFFFFDD00, "\n=== Phase 6: Preemptive Scheduler ===\n");
    sched_init();
    sched_install_timer();
    extern void sched_demo_spawn(void);
    extern void user_demo_load(void);
    if (YAM_DEMO_TASKS) {
        sched_demo_spawn();
        user_demo_load();
    }
    if (YAM_PREEMPTIVE) {
        apic_timer_start(100);
        sched_enable();
    } else {
        kprintf_color(0xFFFF8833, "[SCHED] preemption disabled (stable shell mode)\n");
    }

    /* ---- Splash Screen Spinner Animation (hlt-based, near-0% CPU) ---- */
    KINFO("SPLASH", "Starting spinner animation...");
    __asm__ volatile ("sti");  /* Ensure interrupts are enabled for hlt to wake */
    for (int i = 0; i < 45; i++) {
        fb_draw_spinner(i);
        /*
         * Sleep ~80ms per frame using hlt.
         * PIT fires at 100Hz (10ms per tick), so 8 halts ≈ 80ms.
         * CPU drops to ~0% between frames (sleeps until PIT interrupt).
         */
        for (int t = 0; t < 8; t++) {
            __asm__ volatile ("hlt");
        }
    }
    KINFO("SPLASH", "Spinner done, transitioning to shell");

    fb_clear(FB_COLOR_DARK_BG);
    fb_enable_text(true);

    KINFO("BOOT", "=== Boot Complete ===");

    if (YAM_WAYLAND) {
        kprintf_color(0xFF00DDFF, "\n=== Phase 7: Wayland Display Server ===\n");
        drm_init();
        wl_compositor_init();
        
        /* Load ELF user-space app if found */
        if (g_elf_module) {
            extern bool elf_load(const void *, usize, const char *);
            elf_load(g_elf_module, g_elf_module_size, "test_app");
        }
        
        /* Spawn Wayland Compositor (highest priority) */
        sched_spawn("wayland", wl_compositor_task, NULL, 0);
        
        /* Spawn Apps (only when clicked from GUI now) */
        /*
        extern void wl_calc_task(void *);
        extern void wl_browser_task(void *);
        extern void wl_term_task(void *);
        sched_spawn("wl-calc", wl_calc_task, NULL, 2);
        sched_spawn("wl-browser", wl_browser_task, NULL, 2);
        sched_spawn("wl-term", wl_term_task, NULL, 2);
        */
        
        kprintf_color(0xFF00FF88, "[WAYLAND] Handoff to Compositor complete. Terminal suspended.\n");
        
        /* Task 0 (us) acts as the Idle Task */
        for (;;) {
            sched_yield();
            __asm__ volatile("hlt");
        }
    } else {
        /* Launch interactive REPL */
        shell_start();
    }
}
