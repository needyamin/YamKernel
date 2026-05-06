#ifndef YAM_DRIVERS_VIDEO_VIRTIO_GPU_H
#define YAM_DRIVERS_VIDEO_VIRTIO_GPU_H

#include <nexus/types.h>
#include "../drm/drm.h"

void virtio_gpu_init_all(void);
bool virtio_gpu_setup_fb(u32 width, u32 height);
bool virtio_gpu_flush(u32 x, u32 y, u32 width, u32 height, void *pixels);

#endif
