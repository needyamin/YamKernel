/* ============================================================================
 * YamKernel — Evdev Input Subsystem Implementation
 * Lock-free ring buffer for input events from PS/2 keyboard/mouse ISRs.
 * ============================================================================ */
#include "evdev.h"
#include "../../lib/kprintf.h"
#include "../../lib/spinlock.h"

static input_event_t g_ring[EVDEV_RING_SIZE];
static volatile u32  g_head = 0;
static volatile u32  g_tail = 0;
static spinlock_t    g_lock = SPINLOCK_INIT;

void evdev_init(void) {
    g_head = 0;
    g_tail = 0;
    kprintf_color(0xFF00FF88, "[EVDEV] Input event subsystem initialized (%u slots)\n",
                  EVDEV_RING_SIZE);
}

void evdev_push_event(u16 type, u16 code, i32 value) {
    u64 flags = spin_lock_irqsave(&g_lock);
    u32 next = (g_head + 1) % EVDEV_RING_SIZE;
    if (next == g_tail) {
        spin_unlock_irqrestore(&g_lock, flags);
        return;  /* Ring full — drop oldest */
    }

    g_ring[g_head].type  = type;
    g_ring[g_head].code  = code;
    g_ring[g_head].value = value;
    g_head = next;
    spin_unlock_irqrestore(&g_lock, flags);
}

bool evdev_pop_event(input_event_t *out) {
    u64 flags = spin_lock_irqsave(&g_lock);
    if (g_tail == g_head) {
        spin_unlock_irqrestore(&g_lock, flags);
        return false;
    }
    *out = g_ring[g_tail];
    g_tail = (g_tail + 1) % EVDEV_RING_SIZE;
    spin_unlock_irqrestore(&g_lock, flags);
    return true;
}

bool evdev_has_events(void) {
    return g_head != g_tail;
}
