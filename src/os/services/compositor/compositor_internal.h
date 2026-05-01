#ifndef _WAYLAND_COMPOSITOR_INTERNAL_H
#define _WAYLAND_COMPOSITOR_INTERNAL_H

#include "compositor.h"
#include "drivers/input/evdev.h"
#include "drivers/video/framebuffer.h"
#include "drivers/timer/rtc.h"
#include "sched/sched.h"
#include "sched/wait.h"
#include "mem/heap.h"
#include "lib/kprintf.h"
#include "lib/string.h"
#include "lib/kdebug.h"
#include "fs/elf.h"
#include "../../dev/vtty.h"
#include "wl_draw.h"

extern wl_compositor_t g_compositor;
extern void *g_wallpaper_module;

void wl_compositor_process_input(void);
void wl_compositor_render_frame(void);
void wl_spawn_app_async(void *data, usize size, const char *name);

#endif
