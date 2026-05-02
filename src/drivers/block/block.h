#ifndef YAM_DRIVERS_BLOCK_BLOCK_H
#define YAM_DRIVERS_BLOCK_BLOCK_H

#include <nexus/types.h>

#define BLOCK_DEVICE_NAME_MAX 32
#define BLOCK_MAX_DEVICES     16

typedef enum {
    BLOCK_DEVICE_UNKNOWN = 0,
    BLOCK_DEVICE_AHCI,
    BLOCK_DEVICE_NVME,
    BLOCK_DEVICE_VIRTIO,
    BLOCK_DEVICE_RAM,
} block_device_kind_t;

typedef struct block_device block_device_t;

typedef int (*block_read_fn)(block_device_t *dev, u64 lba, u32 count, void *buf);
typedef int (*block_write_fn)(block_device_t *dev, u64 lba, u32 count, const void *buf);
typedef int (*block_flush_fn)(block_device_t *dev);

struct block_device {
    char name[BLOCK_DEVICE_NAME_MAX];
    block_device_kind_t kind;
    u64 sector_count;
    u32 sector_size;
    bool read_only;
    void *driver_data;
    block_read_fn read;
    block_write_fn write;
    block_flush_fn flush;
};

void block_init(void);
int block_register(block_device_t dev);
u32 block_device_count(void);
block_device_t *block_device_at(u32 index);
block_device_t *block_find(const char *name);
void block_dump(void);

#endif
