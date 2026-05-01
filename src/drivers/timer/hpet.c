/* YamKernel - HPET discovery and counter access */
#include "hpet.h"
#include "../../cpu/acpi.h"
#include "../../mem/vmm.h"
#include "../../lib/kprintf.h"

#define HPET_REG_CAP_ID   0x000
#define HPET_REG_CONFIG   0x010
#define HPET_REG_COUNTER  0x0F0

static volatile u8 *g_hpet;
static u64 g_hpet_freq_hz;

static inline u64 hpet_read64(u32 reg) {
    return *(volatile u64 *)(g_hpet + reg);
}

static inline void hpet_write64(u32 reg, u64 value) {
    *(volatile u64 *)(g_hpet + reg) = value;
}

void hpet_init(u64 hhdm_offset) {
    const acpi_info_t *a = acpi_get();
    if (!a->hpet_addr) {
        kprintf_color(0xFFFFDD00, "[HPET] not advertised by ACPI\n");
        return;
    }

    u64 phys = a->hpet_addr & ~0xFFFULL;
    vmm_map_page(vmm_get_kernel_pml4(), hhdm_offset + phys, phys,
                 VMM_FLAG_WRITE | VMM_FLAG_NOCACHE | VMM_FLAG_NX);
    g_hpet = (volatile u8 *)(hhdm_offset + a->hpet_addr);

    u64 cap = hpet_read64(HPET_REG_CAP_ID);
    u32 period_fs = (u32)(cap >> 32);
    if (!period_fs) {
        kprintf_color(0xFFFF3333, "[HPET] invalid period\n");
        g_hpet = NULL;
        return;
    }

    g_hpet_freq_hz = 1000000000000000ULL / period_fs;
    hpet_write64(HPET_REG_CONFIG, hpet_read64(HPET_REG_CONFIG) | 1);
    (void)hpet_read64(HPET_REG_COUNTER);

    kprintf_color(0xFF00FF88, "[HPET] MMIO=0x%lx freq=%lu Hz\n",
                  a->hpet_addr, g_hpet_freq_hz);
}

bool hpet_available(void) {
    return g_hpet != NULL;
}

u64 hpet_frequency_hz(void) {
    return g_hpet_freq_hz;
}
