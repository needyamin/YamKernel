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
#include "drivers/ai/ai_accel.h"
#include "nexus/channel.h"
#include "nexus/graph.h"

extern void syscall_entry(void);   /* in syscall.asm */

static u8 syscall_stack[16384] ALIGNED(16);

void syscall_init(void) {
    percpu_set_kernel_rsp((u64)&syscall_stack[sizeof(syscall_stack)]);
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);
    wrmsr(MSR_STAR, ((u64)0x10 << 48) | ((u64)0x08 << 32));
    wrmsr(MSR_LSTAR, (u64)syscall_entry);
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

static i64 sys_getpid(void) { return (i64)this_cpu()->current->id; }
static i64 sys_getppid(void) {
    task_t *t = this_cpu()->current;
    return t->parent ? (i64)t->parent->id : 0;
}
static i64 sys_yield(void) { sched_yield(); return 0; }
static i64 sys_sleepms(u64 ms) { task_sleep_ms(ms); return 0; }

static i64 sys_exit(u64 code) {
    task_t *t = this_cpu()->current;
    t->state = TASK_DEAD;
    t->exit_code = (i32)code;
    kprintf_color(0xFFFF8833, "[SYS] task '%s' exit(%lu)\n", t->name, code);
    sched_yield();
    for (;;) hlt();
    return 0;
}

static i64 sys_clock_gettime(void) {
    return (i64)this_cpu()->ticks * 10; /* Convert ticks to ms (100Hz timer) */
}

/* ---- Wayland / GUI Syscall Handlers ---- */

static i64 sys_wl_create_surface(u64 utitle, i32 x, i32 y, u32 w, u32 h) {
    task_t *t = this_cpu()->current;
    char ktitle[WL_TITLE_MAX];
    ktitle[0] = '\0';
    ktitle[WL_TITLE_MAX - 1] = '\0';
    if (utitle) {
        bool smap = (read_cr4() & CR4_SMAP) != 0;
        if (smap) __asm__ volatile ("stac");
        strncpy(ktitle, (const char *)utitle, WL_TITLE_MAX - 1);
        if (smap) __asm__ volatile ("clac");
    } else {
        strncpy(ktitle, "(untitled)", WL_TITLE_MAX - 1);
    }
    wl_surface_t *s = wl_surface_create(ktitle, x, y, w, h, t->id);
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
            s = &comp->surfaces[i]; break;
        }
    }
    if (!s) return -3;
    if (s->owner_task_id != t->id) return -4;
    if (!s->buffer || !s->buffer->pixels) return -5;

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
            s = &comp->surfaces[i]; break;
        }
    }
    if (!s || s->owner_task_id != t->id) return -1;

    input_event_t ev;
    if (wl_surface_pop_event(s, &ev)) {
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

/* ---- AI Syscalls ---- */
static i64 sys_ai_device_query(void) { return (i64)ai_device_count(); }
static i64 sys_ai_tensor_alloc_handler(u32 ndim, u64 shape_ptr, u32 dtype) {
    /* Simplified: read shape from user space */
    u32 shape[8] = {0};
    bool smap = (read_cr4() & CR4_SMAP) != 0;
    if (smap) __asm__ volatile ("stac");
    if (ndim > 8) ndim = 8;
    for (u32 i = 0; i < ndim; i++) shape[i] = ((u32 *)shape_ptr)[i];
    if (smap) __asm__ volatile ("clac");
    return (i64)ai_tensor_alloc(ndim, shape, (tensor_dtype_t)dtype);
}

/* ---- YamGraph IPC Syscalls ---- */
static i64 sys_channel_send(u32 node_id, u32 msg_type, u64 udata, u32 length) {
    task_t *t = this_cpu()->current;
    yam_channel_t *chan = channel_get(node_id);
    if (!chan) return -1;
    
    bool smap = (read_cr4() & CR4_SMAP) != 0;
    if (smap) __asm__ volatile ("stac");
    bool ret = channel_send(chan, t->graph_node, msg_type, (const void *)udata, length);
    if (smap) __asm__ volatile ("clac");
    
    return ret ? 0 : -2;
}

static i64 sys_channel_recv(u32 node_id, u64 uout) {
    task_t *t = this_cpu()->current;
    yam_channel_t *chan = channel_get(node_id);
    if (!chan) return -1;
    
    yam_message_t msg;
    if (!channel_recv(chan, t->graph_node, &msg)) return -2;
    
    bool smap = (read_cr4() & CR4_SMAP) != 0;
    if (smap) __asm__ volatile ("stac");
    memcpy((void *)uout, &msg, sizeof(yam_message_t));
    if (smap) __asm__ volatile ("clac");
    
    return 0;
}

static i64 sys_channel_lookup(u64 uname) {
    bool smap = (read_cr4() & CR4_SMAP) != 0;
    if (smap) __asm__ volatile ("stac");
    const char *name = (const char *)uname;
    yam_node_id_t id = yamgraph_find_node_by_name(name);
    if (smap) __asm__ volatile ("clac");
    return (i64)id;
}

/* ---- Dispatch ---- */
i64 syscall_dispatch(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
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

    /* v0.3.0 process management */
    case SYS_FORK:          return sys_fork();
    case SYS_WAITPID:       return sys_waitpid((i64)a1, (i32 *)a2, (u32)a3);
    case SYS_BRK:           return (i64)sys_brk(a1);
    case SYS_MPROTECT:      return (i64)sys_mprotect((void *)a1, (usize)a2, (u32)a3);
    case SYS_CLOCK_GETTIME: return sys_clock_gettime();
    case SYS_GETPPID:       return sys_getppid();
    case SYS_KILL:          return sys_kill(a1, (u32)a2);
    case SYS_FUTEX:         return sys_futex((u32 *)a1, (int)a2, (u32)a3, a4);

    /* Wayland */
    case SYS_WL_CREATE_SURFACE: return sys_wl_create_surface(a1, (i32)a2, (i32)a3, (u32)a4, (u32)a5);
    case SYS_WL_MAP_BUFFER:     return sys_wl_map_buffer((u32)a1, a2);
    case SYS_WL_COMMIT:         return sys_wl_commit((u32)a1);
    case SYS_WL_POLL_EVENT:     return sys_wl_poll_event((u32)a1, a2);

    /* Drivers */
    case SYS_IOPORT_READ:     return sys_ioport_read((u16)a1, (u8)a2);
    case SYS_IOPORT_WRITE:    return sys_ioport_write((u16)a1, (u8)a2, a3);
    case SYS_PCI_CONFIG_READ: return sys_pci_config_read((u8)a1, (u8)a2, (u8)a3, (u8)a4, (u8)a5);
    case SYS_MAP_MMIO:        return sys_map_mmio(a1, a2, a3);

    /* AI */
    case SYS_AI_DEVICE_QUERY: return sys_ai_device_query();
    case SYS_AI_TENSOR_ALLOC: return sys_ai_tensor_alloc_handler((u32)a1, a2, (u32)a3);
    case SYS_AI_TENSOR_FREE:  ai_tensor_free((u32)a1); return 0;
    case SYS_AI_SUBMIT_JOB:   return (i64)ai_job_submit((ai_op_type_t)a1, (const u32 *)a2, (u32)a3, (u32)a4, (u8)a5);
    case SYS_AI_WAIT_JOB:     ai_job_wait((u32)a1); return 0;

    /* YamGraph IPC */
    case SYS_CHANNEL_SEND:    return sys_channel_send((u32)a1, (u32)a2, a3, (u32)a4);
    case SYS_CHANNEL_RECV:    return sys_channel_recv((u32)a1, a2);
    case SYS_CHANNEL_LOOKUP:  return sys_channel_lookup(a1);

    default: return -1;
    }
}
