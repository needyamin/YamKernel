#include "kernel/api/syscall.h"
#include "msr.h"
#include "percpu.h"
#include "sched/sched.h"
#include "sched/wait.h"
#include "lib/kprintf.h"
#include "lib/string.h"
#include "drivers/serial/serial.h"
#include "fs/vfs.h"
#include "fs/poll.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "ipc/pipe.h"
#include "os/services/compositor/compositor.h"
#include "drivers/drm/drm.h"
#include "drivers/bus/pci.h"

extern void syscall_entry(void);   /* in syscall.asm */

static u8 syscall_stack[16384] ALIGNED(16);

void syscall_init(void) {
    /* Per-CPU syscall stack: where SYSCALL entry switches to */
    percpu_set_kernel_rsp((u64)&syscall_stack[sizeof(syscall_stack)]);

    /* EFER.SCE = 1 — enable SYSCALL */
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);

    /* STAR: bits 32-47 = kernel CS for SYSCALL  (kernel CS=0x08, SS=+8=0x10)
     *       bits 48-63 = base for SYSRET selectors. With base=0x10:
     *           SYSRET CS = 0x10+16 | 3 = 0x23 (user code, GDT idx 4)
     *           SYSRET SS = 0x10+8  | 3 = 0x1B (user data, GDT idx 3) */
    wrmsr(MSR_STAR, ((u64)0x10 << 48) | ((u64)0x08 << 32));

    /* LSTAR: 64-bit syscall entry RIP */
    wrmsr(MSR_LSTAR, (u64)syscall_entry);

    /* SFMASK: which RFLAGS bits to clear on SYSCALL.
     * Clear IF (0x200) so syscall runs with interrupts disabled until we want them,
     * also clear DF (0x400) for predictable string ops, TF (0x100) for no debug step. */
    wrmsr(MSR_SFMASK, 0x700);

    kprintf_color(0xFF00FF88, "[SYSCALL] entry=%p  STAR=0x%lx\n",
                  (void *)syscall_entry, rdmsr(MSR_STAR));
}

/* ---- Individual syscall handlers ---- */

static i64 sys_write_serial(u64 fd, u64 ubuf, u64 len) {
    (void)fd;
    bool smap = (read_cr4() & CR4_SMAP) != 0;
    if (smap) __asm__ volatile ("stac");
    const char *p = (const char *)ubuf;
    for (u64 i = 0; i < len; i++) serial_putchar(p[i]);
    if (smap) __asm__ volatile ("clac");
    return (i64)len;
}

static i64 sys_getpid(void) {
    return (i64)this_cpu()->current->id;
}

static i64 sys_yield(void) { sched_yield(); return 0; }
static i64 sys_sleepms(u64 ms) { task_sleep_ms(ms); return 0; }

static i64 sys_exit(u64 code) {
    task_t *t = this_cpu()->current;
    t->state = TASK_DEAD;
    kprintf_color(0xFFFF8833, "[SYS] task '%s' exit(%lu)\n", t->name, code);
    sched_yield();
    for (;;) hlt();   /* should never return */
    return 0;
}

/* ---- Wayland / GUI Syscall Handlers ---- */

static i64 sys_wl_create_surface(u64 utitle, i32 x, i32 y, u32 w, u32 h) {
    task_t *t = this_cpu()->current;
    const char *title = (const char *)utitle;
    
    /* TODO: Validation of title string (SMAP etc) */
    wl_surface_t *s = wl_surface_create(title, x, y, w, h, t->id);
    if (!s) return -1;
    return (i64)s->id;
}

static i64 sys_wl_map_buffer(u32 surface_id, u64 uvaddr) {
    task_t *t = this_cpu()->current;
    if (!t->pml4) return -2;

    wl_compositor_t *comp = wl_get_compositor();
    wl_surface_t *s = NULL;
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (comp->surfaces[i].state != WL_SURFACE_FREE && comp->surfaces[i].id == surface_id) {
            s = &comp->surfaces[i];
            break;
        }
    }
    if (!s) return -3;
    if (s->owner_task_id != t->id) return -4;
    if (!s->buffer || !s->buffer->pixels) return -5;

    /* Map the dumb buffer's physical pages into the user address space */
    u64 phys_base = vmm_virt_hhdm_to_phys(s->buffer->pixels);
    u64 size = s->buffer->size;
    u64 pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (u64 i = 0; i < pages; i++) {
        vmm_map_page(t->pml4, uvaddr + i * PAGE_SIZE, phys_base + i * PAGE_SIZE,
                     VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_WRITE | VMM_FLAG_NX | VMM_FLAG_DONT_FREE);
    }

    return 0;
}

static i64 sys_wl_commit(u32 surface_id) {
    wl_compositor_t *comp = wl_get_compositor();
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (comp->surfaces[i].state != WL_SURFACE_FREE && comp->surfaces[i].id == surface_id) {
            wl_surface_commit(&comp->surfaces[i]);
            return 0;
        }
    }
    return -1;
}

static i64 sys_wl_poll_event(u32 surface_id, u64 uev_ptr) {
    task_t *t = this_cpu()->current;
    wl_compositor_t *comp = wl_get_compositor();
    wl_surface_t *s = NULL;
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (comp->surfaces[i].state != WL_SURFACE_FREE && comp->surfaces[i].id == surface_id) {
            s = &comp->surfaces[i];
            break;
        }
    }
    if (!s || s->owner_task_id != t->id) return -1;

    input_event_t ev;
    if (wl_surface_pop_event(s, &ev)) {
        /* Copy to user-space (SMAP-safe) */
        bool smap = (read_cr4() & CR4_SMAP) != 0;
        if (smap) __asm__ volatile ("stac");
        memcpy((void *)uev_ptr, &ev, sizeof(input_event_t));
        if (smap) __asm__ volatile ("clac");
        return 1;
    }
    return 0;
}

/* ---- Privileged Driver Syscalls ---- */

static i64 sys_ioport_read(u16 port, u8 width) {
    if (width == 1) return (i64)inb(port);
    if (width == 2) return (i64)inw(port);
    if (width == 4) return (i64)inl(port);
    return -1;
}

static i64 sys_ioport_write(u16 port, u8 width, u64 value) {
    if (width == 1) outb(port, (u8)value);
    else if (width == 2) outw(port, (u16)value);
    else if (width == 4) outl(port, (u32)value);
    return 0;
}

static i64 sys_pci_config_read(u8 bus, u8 slot, u8 func, u8 offset, u8 width) {
    if (width == 4) return (i64)pci_read_32(bus, slot, func, offset);
    if (width == 2) return (i64)pci_read_16(bus, slot, func, offset);
    if (width == 1) return (i64)pci_read_8(bus, slot, func, offset);
    return -1;
}

static i64 sys_map_mmio(u64 phys_addr, u64 virt_addr, u64 size) {
    task_t *t = this_cpu()->current;
    if (!t->pml4) return -1;
    u64 pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (u64 i = 0; i < pages; i++) {
        vmm_map_page(t->pml4, virt_addr + i * PAGE_SIZE, phys_addr + i * PAGE_SIZE,
                     VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_WRITE | VMM_FLAG_NX | VMM_FLAG_NOCACHE);
    }
    return 0;
}

i64 syscall_dispatch(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
    (void)a4; (void)a5;
    switch (nr) {
    case SYS_WRITE:    return sys_write_serial(a1, a2, a3);
    case SYS_EXIT:     return sys_exit(a1);
    case SYS_GETPID:   return sys_getpid();
    case SYS_YIELD:    return sys_yield();
    case SYS_SLEEPMS:  return sys_sleepms(a1);
    case SYS_OPEN:     return (i64)sys_open((const char *)a1, (u32)a2);
    case SYS_CLOSE:    return (i64)sys_close((int)a1);
    case SYS_READ:     return (i64)sys_read((int)a1, (void *)a2, (usize)a3);
    case SYS_WRITE_FD: return (i64)sys_write((int)a1, (const void *)a2, (usize)a3);
    case SYS_MMAP:     return (i64)(u64)sys_mmap((void *)a1, (usize)a2, (u32)a3, (u32)a4, (int)a5, 0);
    case SYS_MUNMAP:   return (i64)sys_munmap((void *)a1, (usize)a2);
    case SYS_PIPE:     return (i64)sys_pipe((int *)a1);
    case SYS_POLL:     return (i64)sys_poll((pollfd_t *)a1, (u32)a2, (i64)a3);
    
    case SYS_WL_CREATE_SURFACE: return sys_wl_create_surface(a1, (i32)a2, (i32)a3, (u32)a4, (u32)a5);
    case SYS_WL_MAP_BUFFER:     return sys_wl_map_buffer((u32)a1, a2);
    case SYS_WL_COMMIT:         return sys_wl_commit((u32)a1);
    case SYS_WL_POLL_EVENT:     return sys_wl_poll_event((u32)a1, a2);

    /* Driver Syscalls */
    case SYS_IOPORT_READ:       return sys_ioport_read((u16)a1, (u8)a2);
    case SYS_IOPORT_WRITE:      return sys_ioport_write((u16)a1, (u8)a2, a3);
    case SYS_PCI_CONFIG_READ:   return sys_pci_config_read((u8)a1, (u8)a2, (u8)a3, (u8)a4, (u8)a5);
    case SYS_MAP_MMIO:          return sys_map_mmio(a1, a2, a3);

    default: return -1;
    }
}
