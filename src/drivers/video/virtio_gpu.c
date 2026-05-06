#include "virtio_gpu.h"
#include "../bus/pci.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"
#include "../../mem/pmm.h"
#include "../../mem/vmm.h"
#include "../../mem/heap.h"

#define VIRTIO_PCI_VENDOR 0x1AF4
#define VIRTIO_GPU_DEVICE 0x1050

#define VIRTIO_PCI_HOST_FEATURES 0x00
#define VIRTIO_PCI_GUEST_FEATURES 0x04
#define VIRTIO_PCI_QUEUE_PFN 0x08
#define VIRTIO_PCI_QUEUE_SIZE 0x0C
#define VIRTIO_PCI_QUEUE_SEL 0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY 0x10
#define VIRTIO_PCI_STATUS 0x12
#define VIRTIO_PCI_ISR 0x13

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED 0x80

#define VRING_DESC_F_NEXT 1
#define VRING_DESC_F_WRITE 2

#define VIRTIO_GPU_F_VIRGL 0

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF 0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT 0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106

#define VIRTIO_GPU_RESP_OK_NODATA 0x1200
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1202

typedef struct __attribute__((packed)) {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
} virtq_desc_t;

typedef struct __attribute__((packed)) {
    u16 flags;
    u16 idx;
    u16 ring[];
} virtq_avail_t;

typedef struct __attribute__((packed)) {
    u32 id;
    u32 len;
} virtq_used_elem_t;

typedef struct __attribute__((packed)) {
    u16 flags;
    u16 idx;
    virtq_used_elem_t ring[];
} virtq_used_t;

typedef struct __attribute__((packed)) {
    u32 type;
    u32 flags;
    u64 fence_id;
    u32 ctx_id;
    u32 padding;
} virtio_gpu_ctrl_hdr_t;

typedef struct __attribute__((packed)) {
    u32 x, y, width, height;
} virtio_gpu_rect_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    u32 resource_id;
    u32 format;
    u32 width;
    u32 height;
} virtio_gpu_resource_create_2d_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    u32 resource_id;
    u32 nr_entries;
} virtio_gpu_resource_attach_backing_t;

typedef struct __attribute__((packed)) {
    u64 addr;
    u32 length;
    u32 padding;
} virtio_gpu_mem_entry_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    u32 scanout_id;
    u32 resource_id;
} virtio_gpu_set_scanout_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    u64 offset;
    u32 resource_id;
    u32 padding;
} virtio_gpu_transfer_to_host_2d_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    u32 resource_id;
    u32 padding;
} virtio_gpu_resource_flush_t;

typedef struct {
    pci_device_t *pci;
    u16 io_base;
    u16 queue_size;
    u16 avail_idx;
    u16 used_seen_idx;
    u64 queue_phys;
    virtq_desc_t *desc;
    virtq_avail_t *avail;
    virtq_used_t *used;
    
    // active resource
    u32 res_id;
    u32 width;
    u32 height;
    u64 fb_phys;
    u32 *fb_virt;
} virtio_gpu_dev_t;

static virtio_gpu_dev_t g_vgpu;
static bool g_vgpu_found = false;

static u64 align_up_u64(u64 value, u64 align) {
    return (value + align - 1) & ~(align - 1);
}

static void vgpu_status(virtio_gpu_dev_t *d, u8 status) {
    outb(d->io_base + VIRTIO_PCI_STATUS, status);
}

static u8 vgpu_status_read(virtio_gpu_dev_t *d) {
    return inb(d->io_base + VIRTIO_PCI_STATUS);
}

static bool vgpu_alloc_queue(virtio_gpu_dev_t *d) {
    u64 desc_bytes = sizeof(virtq_desc_t) * d->queue_size;
    u64 avail_bytes = 6 + (u64)sizeof(u16) * d->queue_size;
    u64 used_off = align_up_u64(desc_bytes + avail_bytes, PAGE_SIZE);
    u64 used_bytes = 6 + (u64)sizeof(virtq_used_elem_t) * d->queue_size;
    u64 queue_bytes = align_up_u64(used_off + used_bytes, PAGE_SIZE);
    u64 pages = queue_bytes / PAGE_SIZE;

    d->queue_phys = pmm_alloc_pages(pages);
    if (!d->queue_phys) return false;

    void *queue = vmm_phys_to_virt(d->queue_phys);
    memset(queue, 0, queue_bytes);
    d->desc = (virtq_desc_t *)queue;
    d->avail = (virtq_avail_t *)((u8 *)queue + desc_bytes);
    d->used = (virtq_used_t *)((u8 *)queue + used_off);
    return true;
}

static int vgpu_submit(virtio_gpu_dev_t *d, void *cmd, u32 cmd_len, void *resp, u32 resp_len, void *payload, u32 payload_len) {
    u64 cmd_phys = vmm_virt_hhdm_to_phys(cmd);
    u64 resp_phys = vmm_virt_hhdm_to_phys(resp);

    int desc_idx = 0;
    d->desc[0].addr = cmd_phys;
    d->desc[0].len = cmd_len;
    d->desc[0].flags = VRING_DESC_F_NEXT;
    d->desc[0].next = 1;
    desc_idx = 1;

    if (payload && payload_len > 0) {
        d->desc[desc_idx].addr = vmm_virt_hhdm_to_phys(payload);
        d->desc[desc_idx].len = payload_len;
        d->desc[desc_idx].flags = VRING_DESC_F_NEXT;
        d->desc[desc_idx].next = desc_idx + 1;
        desc_idx++;
    }

    d->desc[desc_idx].addr = resp_phys;
    d->desc[desc_idx].len = resp_len;
    d->desc[desc_idx].flags = VRING_DESC_F_WRITE;
    d->desc[desc_idx].next = 0;

    u16 slot = d->avail_idx % d->queue_size;
    d->avail->ring[slot] = 0; // head of chain
    __asm__ volatile ("" ::: "memory");
    d->avail_idx++;
    d->avail->idx = d->avail_idx;
    __asm__ volatile ("" ::: "memory");
    
    // Notify control queue (queue 0)
    outw(d->io_base + VIRTIO_PCI_QUEUE_NOTIFY, 0);

    for (u32 spin = 0; spin < 10000000; spin++) {
        if (d->used->idx != d->used_seen_idx) {
            d->used_seen_idx = d->used->idx;
            (void)inb(d->io_base + VIRTIO_PCI_ISR);
            virtio_gpu_ctrl_hdr_t *rh = (virtio_gpu_ctrl_hdr_t *)resp;
            if (rh->type >= 0x1200 && rh->type <= 0x1205) return 0; // success type
            return -1;
        }
        __asm__ volatile ("pause");
    }
    kprintf("[VGPU] command timeout!\n");
    return -1;
}

static bool vgpu_init_device(pci_device_t *pci) {
    pci_bar_t bar0;
    if (!pci_read_bar(pci, 0, &bar0) || bar0.type != PCI_BAR_IO || !bar0.base) return false;

    virtio_gpu_dev_t *d = &g_vgpu;
    memset(d, 0, sizeof(*d));
    d->pci = pci;
    d->io_base = (u16)bar0.base;

    pci_enable_io(pci);
    pci_enable_bus_master(pci);

    vgpu_status(d, 0);
    vgpu_status(d, VIRTIO_STATUS_ACKNOWLEDGE);
    vgpu_status(d, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    u32 features = inl(d->io_base + VIRTIO_PCI_HOST_FEATURES);
    u32 wanted = features & 0; // we don't need Virgl 3D or EDID for now
    outl(d->io_base + VIRTIO_PCI_GUEST_FEATURES, wanted);

    u8 status = vgpu_status_read(d) | VIRTIO_STATUS_FEATURES_OK;
    vgpu_status(d, status);
    if (!(vgpu_status_read(d) & VIRTIO_STATUS_FEATURES_OK)) return false;

    // setup controlq (0)
    outw(d->io_base + VIRTIO_PCI_QUEUE_SEL, 0);
    d->queue_size = inw(d->io_base + VIRTIO_PCI_QUEUE_SIZE);
    if (d->queue_size < 3) return false;
    if (!vgpu_alloc_queue(d)) return false;
    outl(d->io_base + VIRTIO_PCI_QUEUE_PFN, (u32)(d->queue_phys / PAGE_SIZE));

    // skip cursorq (1)
    
    vgpu_status(d, vgpu_status_read(d) | VIRTIO_STATUS_DRIVER_OK);
    kprintf_color(0xFF00FF88, "[VGPU] virtio-gpu initialized io=0x%x q=%u\n", d->io_base, d->queue_size);
    g_vgpu_found = true;
    return true;
}

void virtio_gpu_init_all(void) {
    for (u32 i = 0; i < pci_device_count(); i++) {
        pci_device_t *dev = pci_device_at(i);
        if (dev && dev->vendor_id == VIRTIO_PCI_VENDOR && dev->device_id == VIRTIO_GPU_DEVICE) {
            vgpu_init_device(dev);
            break; // just support 1
        }
    }
}

bool virtio_gpu_setup_fb(u32 width, u32 height) {
    if (!g_vgpu_found) return false;
    virtio_gpu_dev_t *d = &g_vgpu;

    virtio_gpu_resource_create_2d_t create;
    memset(&create, 0, sizeof(create));
    create.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    create.resource_id = 1;
    create.format = 1; // B8G8R8A8_UNORM
    create.width = width;
    create.height = height;
    
    virtio_gpu_ctrl_hdr_t resp;
    if (vgpu_submit(d, &create, sizeof(create), &resp, sizeof(resp), NULL, 0) < 0) return false;

    d->res_id = 1;
    d->width = width;
    d->height = height;

    u64 bytes = width * height * 4;
    u64 pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    d->fb_phys = pmm_alloc_pages(pages);
    d->fb_virt = (u32 *)vmm_phys_to_virt(d->fb_phys);
    memset(d->fb_virt, 0, pages * PAGE_SIZE);

    virtio_gpu_resource_attach_backing_t attach;
    memset(&attach, 0, sizeof(attach));
    attach.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    attach.resource_id = 1;
    attach.nr_entries = 1;

    virtio_gpu_mem_entry_t ent;
    ent.addr = d->fb_phys;
    ent.length = (u32)bytes;
    ent.padding = 0;

    if (vgpu_submit(d, &attach, sizeof(attach), &resp, sizeof(resp), &ent, sizeof(ent)) < 0) return false;

    virtio_gpu_set_scanout_t scan;
    memset(&scan, 0, sizeof(scan));
    scan.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    scan.scanout_id = 0;
    scan.resource_id = 1;
    scan.r.x = 0; scan.r.y = 0;
    scan.r.width = width; scan.r.height = height;

    if (vgpu_submit(d, &scan, sizeof(scan), &resp, sizeof(resp), NULL, 0) < 0) return false;

    kprintf("[VGPU] Resource #1 attached and set as scanout0 (%ux%u)\n", width, height);
    return true;
}

bool virtio_gpu_flush(u32 x, u32 y, u32 width, u32 height, void *pixels) {
    if (!g_vgpu_found) return false;
    virtio_gpu_dev_t *d = &g_vgpu;
    if (!d->fb_virt) return false;

    u32 *src = (u32 *)pixels;
    u32 *dst = d->fb_virt;
    u32 pitch = d->width;
    
    for (u32 row = y; row < y + height; row++) {
        memcpy(dst + (row * pitch) + x, src + (row * pitch) + x, width * 4);
    }

    virtio_gpu_transfer_to_host_2d_t t2d;
    memset(&t2d, 0, sizeof(t2d));
    t2d.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    t2d.resource_id = d->res_id;
    t2d.r.x = x; t2d.r.y = y;
    t2d.r.width = width; t2d.r.height = height;
    t2d.offset = (y * pitch + x) * 4;

    virtio_gpu_ctrl_hdr_t resp;
    if (vgpu_submit(d, &t2d, sizeof(t2d), &resp, sizeof(resp), NULL, 0) < 0) return false;

    virtio_gpu_resource_flush_t flush;
    memset(&flush, 0, sizeof(flush));
    flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    flush.resource_id = d->res_id;
    flush.r.x = x; flush.r.y = y;
    flush.r.width = width; flush.r.height = height;

    if (vgpu_submit(d, &flush, sizeof(flush), &resp, sizeof(resp), NULL, 0) < 0) return false;
    return true;
}
