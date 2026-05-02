#include "app_registry.h"
#include "lib/kprintf.h"
#include "lib/spinlock.h"
#include "lib/string.h"

typedef struct {
    bool used;
    yam_app_record_t record;
} app_registry_slot_t;

static app_registry_slot_t g_apps[YAM_APP_REGISTRY_MAX];
static spinlock_t g_apps_lock = SPINLOCK_INIT;
static u32 g_apps_count = 0;

static void copy_text(char *dst, usize cap, const char *src) {
    if (!dst || cap == 0) return;
    dst[0] = 0;
    if (!src) return;
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
}

void app_registry_init(void) {
    spin_init(&g_apps_lock);
    memset(g_apps, 0, sizeof(g_apps));
    g_apps_count = 0;
    kprintf("[APP] registry initialized: slots=%u abi=%u\n",
            YAM_APP_REGISTRY_MAX, YAM_ABI_VERSION);
}

i64 app_registry_register(task_t *task, const yam_app_manifest_t *manifest) {
    if (!task || !manifest) return -1;
    if (manifest->abi_version != YAM_ABI_VERSION) return -2;
    if (!manifest->name[0]) return -3;

    u64 flags = spin_lock_irqsave(&g_apps_lock);
    app_registry_slot_t *slot = NULL;

    for (u32 i = 0; i < YAM_APP_REGISTRY_MAX; i++) {
        if (g_apps[i].used && g_apps[i].record.pid == task->id) {
            slot = &g_apps[i];
            break;
        }
    }

    if (!slot) {
        for (u32 i = 0; i < YAM_APP_REGISTRY_MAX; i++) {
            if (!g_apps[i].used) {
                slot = &g_apps[i];
                slot->used = true;
                g_apps_count++;
                break;
            }
        }
    }

    if (!slot) {
        spin_unlock_irqrestore(&g_apps_lock, flags);
        return -4;
    }

    memset(&slot->record, 0, sizeof(slot->record));
    slot->record.pid = task->id;
    slot->record.graph_node = task->graph_node;
    slot->record.app_type = manifest->app_type;
    slot->record.permissions = manifest->permissions;
    slot->record.flags = manifest->flags;
    copy_text(slot->record.task_name, sizeof(slot->record.task_name), task->name);
    copy_text(slot->record.name, sizeof(slot->record.name), manifest->name);
    copy_text(slot->record.publisher, sizeof(slot->record.publisher), manifest->publisher);
    copy_text(slot->record.version, sizeof(slot->record.version), manifest->version);
    copy_text(slot->record.description, sizeof(slot->record.description), manifest->description);

    kprintf("[APP] register pid=%lu task='%s' app='%s' type=%u perms=0x%x graph=%u\n",
            slot->record.pid, slot->record.task_name, slot->record.name,
            slot->record.app_type, slot->record.permissions, slot->record.graph_node);

    spin_unlock_irqrestore(&g_apps_lock, flags);
    return 0;
}

i64 app_registry_query(u32 index, yam_app_record_t *out) {
    if (!out) return -1;

    u64 flags = spin_lock_irqsave(&g_apps_lock);
    u32 seen = 0;
    for (u32 i = 0; i < YAM_APP_REGISTRY_MAX; i++) {
        if (!g_apps[i].used) continue;
        if (seen == index) {
            memcpy(out, &g_apps[i].record, sizeof(*out));
            spin_unlock_irqrestore(&g_apps_lock, flags);
            return 0;
        }
        seen++;
    }
    spin_unlock_irqrestore(&g_apps_lock, flags);
    return -2;
}

u32 app_registry_count(void) {
    u64 flags = spin_lock_irqsave(&g_apps_lock);
    u32 count = g_apps_count;
    spin_unlock_irqrestore(&g_apps_lock, flags);
    return count;
}
