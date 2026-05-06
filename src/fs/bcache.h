#ifndef YAM_FS_BCACHE_H
#define YAM_FS_BCACHE_H

#include <nexus/types.h>
#include "../drivers/block/block.h"

void bcache_init(void);

// Read/write through the LRU cache
int bcache_read(block_device_t *dev, u64 lba, u32 count, void *buf);
int bcache_write(block_device_t *dev, u64 lba, u32 count, const void *buf);

// Flush all dirty blocks for this device (or all devices if dev is NULL)
void bcache_flush(block_device_t *dev);

#endif
