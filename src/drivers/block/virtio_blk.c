/* ============================================================================
 * YamKernel - VirtIO block driver, legacy/transitional PCI transport
 * ============================================================================
 */
#include "virtio_blk.h"
#include "block.h"
#include "../bus/pci.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"
#include "../../mem/pmm.h"
#include "../../mem/vmm.h"

#define VIRTIO_PCI_VENDOR          0x1AF4
#define VIRTIO_BLK_LEGACY_DEVICE   0x1001
#define VIRTIO_BLK_MODERN_DEVICE   0x1042

#define VIRTIO_PCI_DEVICE_FEATURES 0x00
#define VIRTIO_PCI_GUEST_FEATURES  0x04
#define VIRTIO_PCI_QUEUE_PFN       0x08
#define VIRTIO_PCI_QUEUE_SIZE      0x0C
#define VIRTIO_PCI_QUEUE_SEL       0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY    0x10
#define VIRTIO_PCI_STATUS          0x12
#define VIRTIO_PCI_ISR             0x13
#define VIRTIO_PCI_CONFIG          0x14

#define VIRTIO_STATUS_ACKNOWLEDGE  0x01
#define VIRTIO_STATUS_DRIVER       0x02
#define VIRTIO_STATUS_DRIVER_OK    0x04
#define VIRTIO_STATUS_FEATURES_OK  0x08
#define VIRTIO_STATUS_FAILED       0x80

#define VIRTIO_RING_F_INDIRECT_DESC (1u << 28)
#define VIRTIO_RING_F_EVENT_IDX     (1u << 29)
#define VIRTIO_BLK_F_RO             (1u << 5)
#define VIRTIO_BLK_F_BLK_SIZE       (1u << 6)
#define VIRTIO_BLK_F_FLUSH          (1u << 9)

#define VRING_DESC_F_NEXT           1
#define VRING_DESC_F_WRITE          2

#define VIRTIO_BLK_T_IN             0
#define VIRTIO_BLK_T_OUT            1
#define VIRTIO_BLK_T_FLUSH          4
#define VIRTIO_BLK_S_OK             0

#define VIRTIO_BLK_MAX_DEVICES      4
#define VIRTIO_BLK_BOUNCE_PAGES     16
#define VIRTIO_BLK_BOUNCE_BYTES     (VIRTIO_BLK_BOUNCE_PAGES * PAGE_SIZE)

typedef struct PACKED {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
} virtq_desc_t;

typedef struct PACKED {
    u16 flags;
    u16 idx;
    u16 ring[];
} virtq_avail_t;

typedef struct PACKED {
    u32 id;
    u32 len;
} virtq_used_elem_t;

typedef struct PACKED {
    u16 flags;
    u16 idx;
    virtq_used_elem_t ring[];
} virtq_used_t;

typedef struct PACKED {
    u32 type;
    u32 reserved;
    u64 sector;
} virtio_blk_req_hdr_t;

typedef struct {
    pci_device_t *pci;
    u16 io_base;
    u16 queue_size;
    u16 avail_idx;
    u16 used_seen_idx;
    u32 features;
    bool read_only;
    u32 sector_size;
    u64 sector_count;
    u64 queue_phys;
    usize queue_bytes;
    virtq_desc_t *desc;
    virtq_avail_t *avail;
    virtq_used_t *used;
    u64 req_phys;
    virtio_blk_req_hdr_t *req_hdr;
    u8 *req_status;
    u64 bounce_phys;
    u8 *bounce;
    char name[BLOCK_DEVICE_NAME_MAX];
} virtio_blk_dev_t;

static virtio_blk_dev_t g_vblk[VIRTIO_BLK_MAX_DEVICES];
static u32 g_vblk_count;

static u64 align_up_u64(u64 value, u64 align) {
    return (value + align - 1) & ~(align - 1);
}

static void vblk_status(virtio_blk_dev_t *d, u8 status) {
    outb(d->io_base + VIRTIO_PCI_STATUS, status);
}

static u8 vblk_status_read(virtio_blk_dev_t *d) {
    return inb(d->io_base + VIRTIO_PCI_STATUS);
}

static bool vblk_alloc_queue(virtio_blk_dev_t *d) {
    u64 desc_bytes = sizeof(virtq_desc_t) * d->queue_size;
    u64 avail_bytes = 6 + (u64)sizeof(u16) * d->queue_size;
    u64 used_off = align_up_u64(desc_bytes + avail_bytes, PAGE_SIZE);
    u64 used_bytes = 6 + (u64)sizeof(virtq_used_elem_t) * d->queue_size;
    d->queue_bytes = align_up_u64(used_off + used_bytes, PAGE_SIZE);
    u64 pages = d->queue_bytes / PAGE_SIZE;

    d->queue_phys = pmm_alloc_pages(pages);
    d->req_phys = pmm_alloc_page();
    d->bounce_phys = pmm_alloc_pages(VIRTIO_BLK_BOUNCE_PAGES);
    if (!d->queue_phys || !d->req_phys || !d->bounce_phys) return false;

    void *queue = vmm_phys_to_virt(d->queue_phys);
    memset(queue, 0, d->queue_bytes);
    d->desc = (virtq_desc_t *)queue;
    d->avail = (virtq_avail_t *)((u8 *)queue + desc_bytes);
    d->used = (virtq_used_t *)((u8 *)queue + used_off);

    d->req_hdr = (virtio_blk_req_hdr_t *)vmm_phys_to_virt(d->req_phys);
    d->req_status = (u8 *)d->req_hdr + sizeof(*d->req_hdr);
    memset(d->req_hdr, 0, PAGE_SIZE);

    d->bounce = (u8 *)vmm_phys_to_virt(d->bounce_phys);
    memset(d->bounce, 0, VIRTIO_BLK_BOUNCE_BYTES);
    return true;
}

static int vblk_submit(virtio_blk_dev_t *d, u32 type, u64 lba, u32 count, void *buf) {
    if (!d || !buf || count == 0) return -1;
    u64 byte_count = (u64)count * d->sector_size;
    if (byte_count > VIRTIO_BLK_BOUNCE_BYTES) return -1;

    d->req_hdr->type = type;
    d->req_hdr->reserved = 0;
    d->req_hdr->sector = (lba * d->sector_size) / 512;
    *d->req_status = 0xFF;
    if (type == VIRTIO_BLK_T_OUT) memcpy(d->bounce, buf, byte_count);

    d->desc[0].addr = d->req_phys;
    d->desc[0].len = sizeof(*d->req_hdr);
    d->desc[0].flags = VRING_DESC_F_NEXT;
    d->desc[0].next = 1;

    d->desc[1].addr = d->bounce_phys;
    d->desc[1].len = (u32)byte_count;
    d->desc[1].flags = VRING_DESC_F_NEXT |
                       (type == VIRTIO_BLK_T_IN ? VRING_DESC_F_WRITE : 0);
    d->desc[1].next = 2;

    d->desc[2].addr = d->req_phys + sizeof(*d->req_hdr);
    d->desc[2].len = 1;
    d->desc[2].flags = VRING_DESC_F_WRITE;
    d->desc[2].next = 0;

    u16 slot = d->avail_idx % d->queue_size;
    d->avail->ring[slot] = 0;
    __asm__ volatile ("" ::: "memory");
    d->avail_idx++;
    d->avail->idx = d->avail_idx;
    __asm__ volatile ("" ::: "memory");
    outw(d->io_base + VIRTIO_PCI_QUEUE_NOTIFY, 0);

    for (u32 spin = 0; spin < 10000000; spin++) {
        if (d->used->idx != d->used_seen_idx) {
            d->used_seen_idx = d->used->idx;
            (void)inb(d->io_base + VIRTIO_PCI_ISR);
            if (*d->req_status != VIRTIO_BLK_S_OK) return -1;
            if (type == VIRTIO_BLK_T_IN) memcpy(buf, d->bounce, byte_count);
            return 0;
        }
        __asm__ volatile ("pause");
    }

    kprintf("[VBLK] request timeout name=%s type=%u lba=%lu count=%u\n",
            d->name, type, lba, count);
    return -1;
}

static int vblk_read(block_device_t *dev, u64 lba, u32 count, void *buf) {
    virtio_blk_dev_t *d = (virtio_blk_dev_t *)dev->driver_data;
    if (!d || lba + count > d->sector_count) return -1;
    return vblk_submit(d, VIRTIO_BLK_T_IN, lba, count, buf);
}

static int vblk_write(block_device_t *dev, u64 lba, u32 count, const void *buf) {
    virtio_blk_dev_t *d = (virtio_blk_dev_t *)dev->driver_data;
    if (!d || d->read_only || lba + count > d->sector_count) return -1;
    return vblk_submit(d, VIRTIO_BLK_T_OUT, lba, count, (void *)buf);
}

static int vblk_flush(block_device_t *dev) {
    virtio_blk_dev_t *d = (virtio_blk_dev_t *)dev->driver_data;
    if (!d || !(d->features & VIRTIO_BLK_F_FLUSH)) return 0;
    u8 dummy[512];
    memset(dummy, 0, sizeof(dummy));
    return vblk_submit(d, VIRTIO_BLK_T_FLUSH, 0, 1, dummy);
}

static bool vblk_probe_legacy(pci_device_t *pci) {
    if (g_vblk_count >= VIRTIO_BLK_MAX_DEVICES) return false;

    pci_bar_t bar0;
    if (!pci_read_bar(pci, 0, &bar0) || bar0.type != PCI_BAR_IO || !bar0.base) {
        kprintf("[VBLK] legacy device %02x:%02x.%u has no IO BAR0\n",
                pci->bus, pci->slot, pci->func);
        return false;
    }

    virtio_blk_dev_t *d = &g_vblk[g_vblk_count];
    memset(d, 0, sizeof(*d));
    d->pci = pci;
    d->io_base = (u16)bar0.base;

    pci_enable_io(pci);
    pci_enable_bus_master(pci);

    vblk_status(d, 0);
    vblk_status(d, VIRTIO_STATUS_ACKNOWLEDGE);
    vblk_status(d, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    d->features = inl(d->io_base + VIRTIO_PCI_DEVICE_FEATURES);
    u32 wanted = d->features & (VIRTIO_BLK_F_RO | VIRTIO_BLK_F_BLK_SIZE | VIRTIO_BLK_F_FLUSH);
    wanted &= ~(VIRTIO_RING_F_INDIRECT_DESC | VIRTIO_RING_F_EVENT_IDX);
    outl(d->io_base + VIRTIO_PCI_GUEST_FEATURES, wanted);

    u8 status = vblk_status_read(d) | VIRTIO_STATUS_FEATURES_OK;
    vblk_status(d, status);
    if (!(vblk_status_read(d) & VIRTIO_STATUS_FEATURES_OK)) {
        vblk_status(d, VIRTIO_STATUS_FAILED);
        return false;
    }

    outw(d->io_base + VIRTIO_PCI_QUEUE_SEL, 0);
    d->queue_size = inw(d->io_base + VIRTIO_PCI_QUEUE_SIZE);
    if (d->queue_size < 3) {
        vblk_status(d, VIRTIO_STATUS_FAILED);
        return false;
    }
    if (!vblk_alloc_queue(d)) {
        vblk_status(d, VIRTIO_STATUS_FAILED);
        return false;
    }

    outl(d->io_base + VIRTIO_PCI_QUEUE_PFN, (u32)(d->queue_phys / PAGE_SIZE));

    u32 cap_lo = inl(d->io_base + VIRTIO_PCI_CONFIG + 0);
    u32 cap_hi = inl(d->io_base + VIRTIO_PCI_CONFIG + 4);
    u32 blk_size = (wanted & VIRTIO_BLK_F_BLK_SIZE) ?
                   inl(d->io_base + VIRTIO_PCI_CONFIG + 20) : 512;
    d->sector_count = ((u64)cap_hi << 32) | cap_lo;
    d->sector_size = blk_size ? blk_size : 512;
    d->read_only = (wanted & VIRTIO_BLK_F_RO) != 0;
    d->features = wanted;
    ksnprintf(d->name, sizeof(d->name), "vd%u", g_vblk_count);

    block_device_t bdev;
    memset(&bdev, 0, sizeof(bdev));
    strncpy(bdev.name, d->name, sizeof(bdev.name) - 1);
    bdev.kind = BLOCK_DEVICE_VIRTIO;
    bdev.sector_count = d->sector_count;
    bdev.sector_size = d->sector_size;
    bdev.read_only = d->read_only;
    bdev.driver_data = d;
    bdev.read = vblk_read;
    bdev.write = d->read_only ? NULL : vblk_write;
    bdev.flush = vblk_flush;

    int id = block_register(bdev);
    if (id < 0) {
        vblk_status(d, VIRTIO_STATUS_FAILED);
        return false;
    }

    vblk_status(d, vblk_status_read(d) | VIRTIO_STATUS_DRIVER_OK);
    kprintf("[VBLK] %s legacy pci=%02x:%02x.%u io=0x%x q=%u sectors=%lu sector_size=%u readonly=%d features=0x%x\n",
            d->name, pci->bus, pci->slot, pci->func, d->io_base, d->queue_size,
            d->sector_count, d->sector_size, d->read_only ? 1 : 0, d->features);
    u8 probe[512];
    if (d->sector_size <= sizeof(probe) && vblk_read(&bdev, 0, 1, probe) == 0) {
        kprintf("[VBLK] %s probe read lba=0 ok sig=%02x%02x\n",
                d->name, probe[510], probe[511]);
    } else {
        kprintf("[VBLK] %s probe read lba=0 failed\n", d->name);
    }
    g_vblk_count++;
    return true;
}

void virtio_blk_init_all(void) {
    g_vblk_count = 0;
    for (u32 i = 0; i < pci_device_count(); i++) {
        pci_device_t *dev = pci_device_at(i);
        if (!dev || dev->vendor_id != VIRTIO_PCI_VENDOR) continue;
        if (dev->device_id == VIRTIO_BLK_LEGACY_DEVICE) {
            (void)vblk_probe_legacy(dev);
        } else if (dev->device_id == VIRTIO_BLK_MODERN_DEVICE) {
            kprintf("[VBLK] modern virtio-blk at %02x:%02x.%u detected; modern PCI transport not enabled yet\n",
                    dev->bus, dev->slot, dev->func);
        }
    }
    if (g_vblk_count == 0) {
        kprintf("[VBLK] no legacy virtio-blk disks found\n");
    }
}
