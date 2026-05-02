/* ============================================================================
 * YamKernel - Block Device Core
 * ============================================================================
 */
#include "block.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"

static block_device_t g_block_devices[BLOCK_MAX_DEVICES];
static u32 g_block_device_count;

static const char *block_kind_name(block_device_kind_t kind) {
    switch (kind) {
        case BLOCK_DEVICE_AHCI:   return "ahci";
        case BLOCK_DEVICE_NVME:   return "nvme";
        case BLOCK_DEVICE_VIRTIO: return "virtio";
        case BLOCK_DEVICE_RAM:    return "ram";
        default:                  return "unknown";
    }
}

void block_init(void) {
    memset(g_block_devices, 0, sizeof(g_block_devices));
    g_block_device_count = 0;
    kprintf("[BLOCK] core initialized: max_devices=%u\n", BLOCK_MAX_DEVICES);
}

int block_register(block_device_t dev) {
    if (g_block_device_count >= BLOCK_MAX_DEVICES) {
        kprintf("[BLOCK] register failed: table full name=%s\n",
                dev.name[0] ? dev.name : "(unnamed)");
        return -1;
    }
    if (!dev.name[0] || dev.sector_size == 0 || dev.sector_count == 0 || !dev.read) {
        kprintf("[BLOCK] register failed: invalid descriptor name=%s sectors=%lu sector_size=%u read=%p\n",
                dev.name[0] ? dev.name : "(unnamed)",
                dev.sector_count, dev.sector_size, dev.read);
        return -1;
    }

    g_block_devices[g_block_device_count] = dev;
    g_block_devices[g_block_device_count].name[BLOCK_DEVICE_NAME_MAX - 1] = 0;
    kprintf("[BLOCK] registered %s kind=%s sectors=%lu sector_size=%u readonly=%d\n",
            g_block_devices[g_block_device_count].name,
            block_kind_name(g_block_devices[g_block_device_count].kind),
            g_block_devices[g_block_device_count].sector_count,
            g_block_devices[g_block_device_count].sector_size,
            g_block_devices[g_block_device_count].read_only ? 1 : 0);
    return (int)g_block_device_count++;
}

u32 block_device_count(void) {
    return g_block_device_count;
}

block_device_t *block_device_at(u32 index) {
    if (index >= g_block_device_count) return NULL;
    return &g_block_devices[index];
}

block_device_t *block_find(const char *name) {
    if (!name) return NULL;
    for (u32 i = 0; i < g_block_device_count; i++) {
        if (strcmp(g_block_devices[i].name, name) == 0) return &g_block_devices[i];
    }
    return NULL;
}

void block_dump(void) {
    kprintf("[BLOCK] inventory: devices=%u\n", g_block_device_count);
    for (u32 i = 0; i < g_block_device_count; i++) {
        block_device_t *dev = &g_block_devices[i];
        kprintf("[BLOCK]  %s kind=%s sectors=%lu sector_size=%u readonly=%d\n",
                dev->name, block_kind_name(dev->kind), dev->sector_count,
                dev->sector_size, dev->read_only ? 1 : 0);
    }
}
