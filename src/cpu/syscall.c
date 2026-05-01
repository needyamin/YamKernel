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
#include "cpu/smp.h"

extern void syscall_entry(void);   /* in syscall.asm */

static u8 syscall_stack[16384] ALIGNED(16);

static u64 *syscall_current_pml4(void) {
    task_t *t = this_cpu()->current;
    if (t && t->pml4) return t->pml4;

    u64 cr3 = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    u64 *pml4 = (u64 *)vmm_phys_to_virt(cr3 & 0x000FFFFFFFFFF000ULL);
    if (t && t->id != 0) t->pml4 = pml4;
    return pml4;
}

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

static i64 sys_getrusage(u64 uout) {
    if (!uout) return -1;
    task_t *t = this_cpu()->current;
    yam_rusage_t ru;
    memset(&ru, 0, sizeof(ru));
    ru.pid = t ? t->id : 0;
    ru.ppid = (t && t->parent) ? t->parent->id : 0;
    ru.ticks = t ? t->ticks : 0;
    ru.utime = t ? t->utime : 0;
    ru.stime = t ? t->stime : 0;
    ru.voluntary_switches = t ? t->vol_switches : 0;
    ru.involuntary_switches = t ? t->invol_switches : 0;
    ru.start_tick = t ? t->start_tick : 0;
    ru.rss_pages = t ? t->rss_pages : 0;
    ru.nice = t ? t->nice : 0;
    ru.cpu = t ? t->cpu : 0;
    ru.affinity = t ? t->cpu_affinity : 1;

    bool smap = (read_cr4() & CR4_SMAP) != 0;
    if (smap) __asm__ volatile ("stac");
    memcpy((void *)uout, &ru, sizeof(ru));
    if (smap) __asm__ volatile ("clac");
    return 0;
}

static i64 sys_sched_setaffinity(u64 mask) {
    task_t *t = this_cpu()->current;
    if (!t || mask == 0) return -1;

    u64 allowed = 0;
    u32 sched_cpus = smp_sched_cpu_count();
    if (sched_cpus == 0) sched_cpus = 1;
    if (sched_cpus > MAX_CPUS) sched_cpus = MAX_CPUS;
    for (u32 cpu = 0; cpu < sched_cpus; cpu++) allowed |= (1ULL << cpu);

    u64 effective = mask & allowed;
    if (!effective) effective = 1;
    if (effective != mask) {
        kprintf("[SCHED] affinity clamp task='%s' requested=0x%lx effective=0x%lx sched_cpus=%u detected=%u\n",
                t->name, mask, effective, sched_cpus, smp_cpu_count());
    }
    sched_set_affinity(t, effective);
    return 0;
}

static i64 sys_sched_getaffinity(void) {
    task_t *t = this_cpu()->current;
    return t ? (i64)sched_get_affinity(t) : -1;
}

static i64 sys_sched_info(u64 uout) {
    if (!uout) return -1;
    sched_info_t info;
    sched_get_info(&info);

    yam_sched_info_t out;
    memset(&out, 0, sizeof(out));
    out.detected_cpus = info.detected_cpus;
    out.schedulable_cpus = info.schedulable_cpus;
    out.total_tasks = info.total_tasks;
    out.ready_tasks = info.ready_tasks;
    out.blocked_tasks = info.blocked_tasks;
    out.running_tasks = info.running_tasks;
    out.total_switches = info.total_switches;
    out.ticks = info.ticks;
    for (u32 i = 0; i < 8 && i < MAX_CPUS; i++) {
        out.rq_load[i] = info.rq_load[i];
        out.rq_ready[i] = info.rq_ready[i];
    }

    bool smap = (read_cr4() & CR4_SMAP) != 0;
    if (smap) __asm__ volatile ("stac");
    memcpy((void *)uout, &out, sizeof(out));
    if (smap) __asm__ volatile ("clac");
    return 0;
}

static char g_clipboard[1024];
static u32 g_clipboard_len = 0;

static i64 sys_clipboard_set(u64 utext, u32 len) {
    if (!utext) return -1;
    if (len >= sizeof(g_clipboard)) len = sizeof(g_clipboard) - 1;

    bool smap = (read_cr4() & CR4_SMAP) != 0;
    if (smap) __asm__ volatile ("stac");
    memcpy(g_clipboard, (const void *)utext, len);
    if (smap) __asm__ volatile ("clac");

    g_clipboard[len] = '\0';
    g_clipboard_len = len;
    kprintf("[CLIPBOARD] set len=%u text='%s'\n", g_clipboard_len, g_clipboard);
    return (i64)g_clipboard_len;
}

static i64 sys_clipboard_get(u64 uout, u32 cap) {
    if (!uout || cap == 0) return -1;
    u32 n = g_clipboard_len;
    if (n >= cap) n = cap - 1;

    bool smap = (read_cr4() & CR4_SMAP) != 0;
    if (smap) __asm__ volatile ("stac");
    memcpy((void *)uout, g_clipboard, n);
    ((char *)uout)[n] = '\0';
    if (smap) __asm__ volatile ("clac");
    return (i64)n;
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
    u64 *task_pml4 = syscall_current_pml4();
    if (!task_pml4) return -2;
    wl_compositor_t *comp = wl_get_compositor();
    wl_surface_t *s = NULL;
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (comp->surfaces[i].state != WL_SURFACE_FREE && comp->surfaces[i].id == surface_id) {
            s = &comp->surfaces[i]; break;
        }
    }
    if (!s) {
        kprintf("[WL_MAP] task '%s' id=%lu surface %u not found\n", t->name, t->id, surface_id);
        return -3;
    }
    if (s->owner_task_id != t->id) {
        kprintf("[WL_MAP] task '%s' id=%lu surface %u owner=%lu mismatch\n",
                t->name, t->id, surface_id, s->owner_task_id);
        return -4;
    }
    if (!s->buffer || !s->buffer->pixels) {
        kprintf("[WL_MAP] task '%s' id=%lu surface %u missing buffer\n", t->name, t->id, surface_id);
        return -5;
    }

    u64 phys_base = vmm_virt_hhdm_to_phys(s->buffer->pixels);
    u64 size = s->buffer->size;
    u64 pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (u64 i = 0; i < pages; i++) {
        if (!vmm_map_page(task_pml4, uvaddr + i * PAGE_SIZE, phys_base + i * PAGE_SIZE,
                          VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_WRITE | VMM_FLAG_NX | VMM_FLAG_DONT_FREE)) {
            kprintf("[WL_MAP] task '%s' id=%lu map failed page=%lu/%lu va=0x%lx phys=0x%lx\n",
                    t->name, t->id, i, pages, uvaddr + i * PAGE_SIZE, phys_base + i * PAGE_SIZE);
            return -6;
        }
    }
    kprintf("[WL_DBG] map-ok task='%s' tid=%lu surface=%u va=0x%lx phys=0x%lx pages=%lu bytes=%lu\n",
            t->name, t->id, surface_id, uvaddr, phys_base, pages, size);
    return 0;
}

static i64 sys_wl_commit(u32 surface_id) {
    task_t *t = this_cpu()->current;
    static u32 last_missing_surface = 0;
    static u32 missing_repeat = 0;
    wl_compositor_t *comp = wl_get_compositor();
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (comp->surfaces[i].state != WL_SURFACE_FREE && comp->surfaces[i].id == surface_id) {
            if (comp->surfaces[i].owner_task_id != t->id) {
                kprintf("[WL_DBG] commit-deny task='%s' tid=%lu surface=%u owner=%lu\n",
                        t->name, t->id, surface_id, comp->surfaces[i].owner_task_id);
                return -2;
            }
            wl_surface_commit(&comp->surfaces[i]);
            return 0;
        }
    }
    if (last_missing_surface != surface_id) {
        last_missing_surface = surface_id;
        missing_repeat = 0;
    }
    missing_repeat++;
    if (missing_repeat <= 3 || (missing_repeat % 128) == 0) {
        kprintf("[WL_DBG] commit-missing task='%s' tid=%lu surface=%u repeat=%u\n",
                t->name, t->id, surface_id, missing_repeat);
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

static void syscall_restore_current_address_space(void) {
    u64 *pml4 = syscall_current_pml4();
    u64 phys = vmm_virt_hhdm_to_phys(pml4);
    __asm__ volatile("mov %0, %%cr3" :: "r"(phys) : "memory");
}

/* ---- Dispatch ---- */
i64 syscall_dispatch(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
#define SYSCALL_RETURN(expr) do { \
        i64 __ret = (i64)(expr); \
        syscall_restore_current_address_space(); \
        return __ret; \
    } while (0)

    switch (nr) {
    case SYS_WRITE:    SYSCALL_RETURN(sys_write_serial(a1, a2, a3));
    case SYS_EXIT:     SYSCALL_RETURN(sys_exit(a1));
    case SYS_GETPID:   SYSCALL_RETURN(sys_getpid());
    case SYS_YIELD:    SYSCALL_RETURN(sys_yield());
    case SYS_SLEEPMS:  SYSCALL_RETURN(sys_sleepms(a1));
    case SYS_OPEN:     SYSCALL_RETURN(sys_open((const char *)a1, (u32)a2));
    case SYS_CLOSE:    SYSCALL_RETURN(sys_close((int)a1));
    case SYS_READ:     SYSCALL_RETURN(sys_read((int)a1, (void *)a2, (usize)a3));
    case SYS_WRITE_FD: SYSCALL_RETURN(sys_write((int)a1, (const void *)a2, (usize)a3));
    case SYS_MMAP:     SYSCALL_RETURN((u64)sys_mmap((void *)a1, (usize)a2, (u32)a3, (u32)a4, (int)a5, 0));
    case SYS_MUNMAP:   SYSCALL_RETURN(sys_munmap((void *)a1, (usize)a2));
    case SYS_PIPE:     SYSCALL_RETURN(sys_pipe((int *)a1));
    case SYS_POLL:     SYSCALL_RETURN(sys_poll((pollfd_t *)a1, (u32)a2, (i64)a3));

    /* v0.3.0 process management */
    case SYS_FORK:          SYSCALL_RETURN(sys_fork());
    case SYS_WAITPID:       SYSCALL_RETURN(sys_waitpid((i64)a1, (i32 *)a2, (u32)a3));
    case SYS_BRK:           SYSCALL_RETURN(sys_brk(a1));
    case SYS_MPROTECT:      SYSCALL_RETURN(sys_mprotect((void *)a1, (usize)a2, (u32)a3));
    case SYS_CLOCK_GETTIME: SYSCALL_RETURN(sys_clock_gettime());
    case SYS_GETRUSAGE:     SYSCALL_RETURN(sys_getrusage(a1));
    case SYS_GETPPID:       SYSCALL_RETURN(sys_getppid());
    case SYS_KILL:          SYSCALL_RETURN(sys_kill(a1, (u32)a2));
    case SYS_SCHED_SETAFFINITY: SYSCALL_RETURN(sys_sched_setaffinity(a1));
    case SYS_SCHED_GETAFFINITY: SYSCALL_RETURN(sys_sched_getaffinity());
    case SYS_SCHED_INFO:        SYSCALL_RETURN(sys_sched_info(a1));
    case SYS_FUTEX:         SYSCALL_RETURN(sys_futex((u32 *)a1, (int)a2, (u32)a3, a4));
    case SYS_CLIPBOARD_SET: SYSCALL_RETURN(sys_clipboard_set(a1, (u32)a2));
    case SYS_CLIPBOARD_GET: SYSCALL_RETURN(sys_clipboard_get(a1, (u32)a2));

    /* Wayland */
    case SYS_WL_CREATE_SURFACE: SYSCALL_RETURN(sys_wl_create_surface(a1, (i32)a2, (i32)a3, (u32)a4, (u32)a5));
    case SYS_WL_MAP_BUFFER:     SYSCALL_RETURN(sys_wl_map_buffer((u32)a1, a2));
    case SYS_WL_COMMIT:         SYSCALL_RETURN(sys_wl_commit((u32)a1));
    case SYS_WL_POLL_EVENT:     SYSCALL_RETURN(sys_wl_poll_event((u32)a1, a2));

    /* Drivers */
    case SYS_IOPORT_READ:     SYSCALL_RETURN(sys_ioport_read((u16)a1, (u8)a2));
    case SYS_IOPORT_WRITE:    SYSCALL_RETURN(sys_ioport_write((u16)a1, (u8)a2, a3));
    case SYS_PCI_CONFIG_READ: SYSCALL_RETURN(sys_pci_config_read((u8)a1, (u8)a2, (u8)a3, (u8)a4, (u8)a5));
    case SYS_MAP_MMIO:        SYSCALL_RETURN(sys_map_mmio(a1, a2, a3));

    /* AI */
    case SYS_AI_DEVICE_QUERY: SYSCALL_RETURN(sys_ai_device_query());
    case SYS_AI_TENSOR_ALLOC: SYSCALL_RETURN(sys_ai_tensor_alloc_handler((u32)a1, a2, (u32)a3));
    case SYS_AI_TENSOR_FREE:  ai_tensor_free((u32)a1); SYSCALL_RETURN(0);
    case SYS_AI_SUBMIT_JOB:   SYSCALL_RETURN(ai_job_submit((ai_op_type_t)a1, (const u32 *)a2, (u32)a3, (u32)a4, (u8)a5));
    case SYS_AI_WAIT_JOB:     ai_job_wait((u32)a1); SYSCALL_RETURN(0);

    /* YamGraph IPC */
    case SYS_CHANNEL_SEND:    SYSCALL_RETURN(sys_channel_send((u32)a1, (u32)a2, a3, (u32)a4));
    case SYS_CHANNEL_RECV:    SYSCALL_RETURN(sys_channel_recv((u32)a1, a2));
    case SYS_CHANNEL_LOOKUP:  SYSCALL_RETURN(sys_channel_lookup(a1));

    default: SYSCALL_RETURN(-1);
    }
#undef SYSCALL_RETURN
}
