/* ============================================================================
 * YamKernel — XHCI USB Controller Driver
 * Handles controller init, port enumeration, and control transfers.
 * ============================================================================ */
#include "xhci.h"
#include "usb_core.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"
#include "../../mem/heap.h"
#include "../../mem/pmm.h"
#include "../../mem/vmm.h"
#include "../../drivers/bus/pci.h"

#define XHCI_PCI_CLASS    0x0C
#define XHCI_PCI_SUBCLASS 0x03
#define XHCI_PCI_PROG_IF  0x30

static xhci_ctrl_t g_xhci;
static bool g_xhci_ready = false;

/* ---- MMIO helpers ---- */
static u32 xhci_readl(xhci_ctrl_t *c, u32 offset) {
    return *(volatile u32 *)(c->op_regs + offset);
}
static void xhci_writel(xhci_ctrl_t *c, u32 offset, u32 val) {
    *(volatile u32 *)(c->op_regs + offset) = val;
}
static u64 __attribute__((unused)) xhci_readq(xhci_ctrl_t *c, u32 offset) {
    u32 lo = *(volatile u32 *)(c->op_regs + offset);
    u32 hi = *(volatile u32 *)(c->op_regs + offset + 4);
    return ((u64)hi << 32) | lo;
}
static void xhci_writeq(xhci_ctrl_t *c, u32 offset, u64 val) {
    *(volatile u32 *)(c->op_regs + offset)     = (u32)(val & 0xFFFFFFFF);
    *(volatile u32 *)(c->op_regs + offset + 4) = (u32)(val >> 32);
}

/* Alloc physically-contiguous aligned memory for DMA */
static void *xhci_dma_alloc(usize size) {
    usize pages = (size + 0xFFF) / 0x1000;
    u64 phys = 0;
    for (usize i = 0; i < pages; i++) {
        u64 p = pmm_alloc_page();
        if (i == 0) phys = p;
    }
    if (!phys) return NULL;
    void *virt = vmm_phys_to_virt(phys);
    memset(virt, 0, size);
    return virt;
}
static u64 xhci_dma_phys(void *virt) {
    return vmm_virt_hhdm_to_phys(virt);
}

bool xhci_hc_reset(xhci_ctrl_t *ctrl) {
    /* Issue host controller reset */
    u32 cmd = xhci_readl(ctrl, XHCI_OP_USBCMD);
    cmd &= ~XHCI_CMD_RUN;
    xhci_writel(ctrl, XHCI_OP_USBCMD, cmd);
    /* Wait for halt */
    for (int i = 0; i < 100000; i++) {
        if (xhci_readl(ctrl, XHCI_OP_USBSTS) & XHCI_STS_HCH) break;
        __asm__ volatile("pause");
    }
    /* Reset */
    xhci_writel(ctrl, XHCI_OP_USBCMD, XHCI_CMD_HCRST);
    for (int i = 0; i < 500000; i++) {
        if (!(xhci_readl(ctrl, XHCI_OP_USBCMD) & XHCI_CMD_HCRST)) break;
        __asm__ volatile("pause");
    }
    /* Wait for controller not-ready to clear */
    for (int i = 0; i < 500000; i++) {
        if (!(xhci_readl(ctrl, XHCI_OP_USBSTS) & XHCI_STS_CNR)) break;
        __asm__ volatile("pause");
    }
    return !(xhci_readl(ctrl, XHCI_OP_USBSTS) & XHCI_STS_CNR);
}

bool xhci_init(xhci_ctrl_t *ctrl, u64 mmio_phys, usize mmio_size) {
    (void)mmio_size;
    memset(ctrl, 0, sizeof(*ctrl));

    /* Map MMIO */
    ctrl->mmio_base = (u8 *)vmm_phys_to_virt(mmio_phys);
    ctrl->cap_regs  = ctrl->mmio_base;

    ctrl->cap_length = *(volatile u8 *)(ctrl->cap_regs + XHCI_CAPLENGTH);
    ctrl->op_regs    = ctrl->cap_regs + ctrl->cap_length;

    u32 rts_off = *(volatile u32 *)(ctrl->cap_regs + XHCI_RTSOFF) & ~0x1F;
    u32 db_off  = *(volatile u32 *)(ctrl->cap_regs + XHCI_DBOFF)  & ~0x03;
    ctrl->rt_regs = ctrl->mmio_base + rts_off;
    ctrl->db_regs = (u32 *)(ctrl->mmio_base + db_off);

    u32 hcsparams1 = *(volatile u32 *)(ctrl->cap_regs + XHCI_HCSPARAMS1);
    ctrl->num_ports = (u16)((hcsparams1 >> 24) & 0xFF);
    ctrl->num_slots = (u8)(hcsparams1 & 0xFF);

    kprintf_color(0xFF00DDFF, "[XHCI] Controller: %u ports, %u slots\n",
                  ctrl->num_ports, ctrl->num_slots);

    if (!xhci_hc_reset(ctrl)) {
        kprintf_color(0xFFFF3333, "[XHCI] Reset failed\n");
        return false;
    }

    /* Set max slots */
    xhci_writel(ctrl, XHCI_OP_CONFIG, ctrl->num_slots);

    /* Allocate DCBAA (Device Context Base Address Array) */
    ctrl->dcbaa = (u64 *)xhci_dma_alloc((ctrl->num_slots + 1) * sizeof(u64));
    if (!ctrl->dcbaa) return false;
    xhci_writeq(ctrl, XHCI_OP_DCBAAP, xhci_dma_phys(ctrl->dcbaa));

    /* Allocate command ring */
    ctrl->cmd_ring = (xhci_trb_t *)xhci_dma_alloc(XHCI_RING_SIZE * sizeof(xhci_trb_t));
    if (!ctrl->cmd_ring) return false;
    ctrl->cmd_enqueue = 0;
    ctrl->cmd_cycle   = 1;
    /* Set link TRB at end */
    xhci_trb_t *link = &ctrl->cmd_ring[XHCI_RING_SIZE - 1];
    link->parameter = xhci_dma_phys(ctrl->cmd_ring);
    link->control   = (TRB_LINK << 10) | (1 << 1) | 1; /* TC=1, C=1 */

    u64 crcr = xhci_dma_phys(ctrl->cmd_ring) | ctrl->cmd_cycle;
    xhci_writeq(ctrl, XHCI_OP_CRCR, crcr);

    /* Allocate event ring (simplified: single segment) */
    ctrl->evt_ring   = (xhci_trb_t *)xhci_dma_alloc(XHCI_RING_SIZE * sizeof(xhci_trb_t));
    ctrl->evt_dequeue = 0;
    ctrl->evt_cycle   = 1;

    /* Set ERST (Event Ring Segment Table) — simplified single-segment */
    u64 *erst = (u64 *)xhci_dma_alloc(64);
    erst[0] = xhci_dma_phys(ctrl->evt_ring);
    erst[1] = XHCI_RING_SIZE;
    /* Interrupter 0 ERSTSZ, ERDP, ERSTBA */
    u8 *ir0 = ctrl->rt_regs + 0x20;
    *(volatile u32 *)(ir0 + 0x00) = 1; /* ERSTSZ = 1 segment */
    *(volatile u64 *)(ir0 + 0x08) = xhci_dma_phys(erst);
    *(volatile u64 *)(ir0 + 0x10) = xhci_dma_phys(ctrl->evt_ring);

    /* Start the controller */
    xhci_writel(ctrl, XHCI_OP_USBCMD, XHCI_CMD_RUN | XHCI_CMD_INTE);

    /* Wait for start */
    for (int i = 0; i < 100000; i++) {
        if (!(xhci_readl(ctrl, XHCI_OP_USBSTS) & XHCI_STS_HCH)) break;
        __asm__ volatile("pause");
    }

    ctrl->initialized = true;
    kprintf_color(0xFF00FF88, "[XHCI] Controller running\n");
    return true;
}

bool xhci_port_reset(xhci_ctrl_t *ctrl, int port) {
    u32 portsc_off = ctrl->cap_length + 0x400 + port * 0x10;
    volatile u32 *portsc = (volatile u32 *)(ctrl->mmio_base + portsc_off);

    if (!(*portsc & XHCI_PORTSC_CCS)) return false; /* No device */

    /* Power on and reset */
    *portsc = (*portsc | XHCI_PORTSC_PP | XHCI_PORTSC_PR) & ~0xFE002; /* clear status change bits */

    /* Wait for reset to complete */
    for (int i = 0; i < 100000; i++) {
        if (!(*portsc & XHCI_PORTSC_PR)) break;
        __asm__ volatile("pause");
    }

    bool connected = (*portsc & XHCI_PORTSC_CCS) != 0;
    bool enabled   = (*portsc & XHCI_PORTSC_PED) != 0;
    kprintf_color(0xFF888888, "[XHCI] Port %d: CCS=%d PED=%d\n", port, connected, enabled);
    return connected && enabled;
}

bool xhci_control_transfer(xhci_ctrl_t *ctrl, int slot,
                            u8 bmRequestType, u8 bRequest, u16 wValue,
                            u16 wIndex, u16 wLength, void *data) {
    /* Simplified control transfer using TRBs */
    if (!ctrl->initialized) return false;
    (void)slot;

    /* Setup packet */
    u64 setup_data = (u64)bmRequestType         |
                     ((u64)bRequest       <<  8) |
                     ((u64)wValue         << 16) |
                     ((u64)wIndex         << 32) |
                     ((u64)wLength        << 48);

    xhci_trb_t *setup = &ctrl->cmd_ring[ctrl->cmd_enqueue];
    setup->parameter = setup_data;
    setup->status    = 8; /* TRB Transfer Length = 8 */
    setup->control   = (TRB_SETUP << 10) | (3 << 16) | ctrl->cmd_cycle; /* TRT=3 (IN) */
    ctrl->cmd_enqueue = (ctrl->cmd_enqueue + 1) % (XHCI_RING_SIZE - 1);

    /* Data TRB (if length > 0) */
    if (wLength > 0 && data) {
        xhci_trb_t *dtd = &ctrl->cmd_ring[ctrl->cmd_enqueue];
        dtd->parameter = xhci_dma_phys(data);
        dtd->status    = wLength;
        dtd->control   = (TRB_DATA << 10) | (1 << 16) | ctrl->cmd_cycle; /* DIR=1 (IN) */
        ctrl->cmd_enqueue = (ctrl->cmd_enqueue + 1) % (XHCI_RING_SIZE - 1);
    }

    /* Status TRB */
    xhci_trb_t *status = &ctrl->cmd_ring[ctrl->cmd_enqueue];
    status->parameter = 0;
    status->status    = 0;
    status->control   = (TRB_STATUS << 10) | ctrl->cmd_cycle;
    ctrl->cmd_enqueue = (ctrl->cmd_enqueue + 1) % (XHCI_RING_SIZE - 1);

    /* Ring doorbell for EP0 of slot */
    ctrl->db_regs[slot] = 1; /* Target = 1 (EP0 IN) */

    /* Wait for completion event (simplified polling) */
    for (int i = 0; i < 100000; i++) {
        xhci_trb_t *evt = &ctrl->evt_ring[ctrl->evt_dequeue];
        if ((evt->control & 1) == (u32)ctrl->evt_cycle) {
            ctrl->evt_dequeue = (ctrl->evt_dequeue + 1) % (XHCI_RING_SIZE - 1);
            /* Update ERDP */
            *(volatile u64 *)(ctrl->rt_regs + 0x20 + 0x10) =
                xhci_dma_phys(&ctrl->evt_ring[ctrl->evt_dequeue]) | (1 << 3);
            return true;
        }
        __asm__ volatile("pause");
    }
    return false; /* Timeout */
}

void xhci_probe_all(void) {
    kprintf_color(0xFF00DDFF, "[XHCI] Scanning PCI for XHCI controllers...\n");

    /* Scan PCI bus for XHCI (Class 0x0C, SubClass 0x03, ProgIF 0x30) */
    for (u16 bus = 0; bus < 256; bus++) {
        for (u8 dev = 0; dev < 32; dev++) {
            for (u8 func = 0; func < 8; func++) {
                u32 class_info = pci_read_32((u8)bus, dev, func, 0x08);
                u8 base_class = (class_info >> 24) & 0xFF;
                u8 sub_class  = (class_info >> 16) & 0xFF;
                u8 prog_if    = (class_info >>  8) & 0xFF;

                if (base_class == XHCI_PCI_CLASS &&
                    sub_class  == XHCI_PCI_SUBCLASS &&
                    prog_if    == XHCI_PCI_PROG_IF) {

                    /* Read BAR0 for MMIO base */
                    u32 bar0 = pci_read_32((u8)bus, dev, func, 0x10);
                    u64 mmio_phys = bar0 & ~0xF;
                    if (!mmio_phys) continue;

                    kprintf_color(0xFF00DDFF, "[XHCI] Found at %02x:%02x.%x MMIO=%llx\n",
                                  bus, dev, func, mmio_phys);

                    /* Enable bus mastering and memory space */
                    u32 cmd_sts = pci_read_32((u8)bus, dev, func, 0x04);
                    pci_write_32((u8)bus, dev, func, 0x04, cmd_sts | 0x06);

                    if (xhci_init(&g_xhci, mmio_phys, 0x10000)) {
                        g_xhci_ready = true;
                        /* Enumerate ports */
                        for (int p = 0; p < g_xhci.num_ports && p < XHCI_MAX_PORTS; p++) {
                            if (xhci_port_reset(&g_xhci, p)) {
                                kprintf_color(0xFF00FF88,
                                    "[XHCI] Device on port %d — enumerating...\n", p);
                                usb_enumerate_device(&g_xhci, p);
                            }
                        }
                    }
                    return; /* Use first controller found */
                }
            }
        }
    }
    kprintf_color(0xFF888888, "[XHCI] No XHCI controller found\n");
}

xhci_ctrl_t *xhci_get_controller(void) {
    return g_xhci_ready ? &g_xhci : NULL;
}
