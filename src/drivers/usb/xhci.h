/* ============================================================================
 * YamKernel — USB XHCI Controller Header
 * eXtensible Host Controller Interface (USB 3.x)
 * ============================================================================ */
#pragma once
#include <nexus/types.h>

/* ---- XHCI Register Offsets ---- */
#define XHCI_CAPLENGTH    0x00   /* Capability Register Length */
#define XHCI_HCIVERSION   0x02   /* Interface Version Number */
#define XHCI_HCSPARAMS1   0x04   /* Structural Parameters 1 */
#define XHCI_HCSPARAMS2   0x08   /* Structural Parameters 2 */
#define XHCI_HCCPARAMS1   0x10   /* Capability Parameters 1 */
#define XHCI_DBOFF        0x14   /* Doorbell Array Offset */
#define XHCI_RTSOFF       0x18   /* Runtime Register Space Offset */

/* Operational Register Offsets (from OpBase) */
#define XHCI_OP_USBCMD    0x00
#define XHCI_OP_USBSTS    0x04
#define XHCI_OP_PAGESIZE  0x08
#define XHCI_OP_DNCTRL    0x14
#define XHCI_OP_CRCR      0x18   /* Command Ring Control Register (64-bit) */
#define XHCI_OP_DCBAAP    0x30   /* Device Context Base Address Array Pointer (64-bit) */
#define XHCI_OP_CONFIG    0x38

/* USBCMD bits */
#define XHCI_CMD_RUN      (1<<0)
#define XHCI_CMD_HCRST    (1<<1)
#define XHCI_CMD_INTE     (1<<2)
#define XHCI_CMD_HSEE     (1<<3)

/* USBSTS bits */
#define XHCI_STS_HCH      (1<<0)  /* Host Controller Halted */
#define XHCI_STS_HSE      (1<<2)  /* Host System Error */
#define XHCI_STS_EINT     (1<<3)  /* Event Interrupt */
#define XHCI_STS_PCD      (1<<4)  /* Port Change Detect */
#define XHCI_STS_CNR      (1<<11) /* Controller Not Ready */

/* Port Status and Control (PORTSC) */
#define XHCI_PORTSC_CCS   (1<<0)  /* Current Connect Status */
#define XHCI_PORTSC_PED   (1<<1)  /* Port Enabled/Disabled */
#define XHCI_PORTSC_PR    (1<<4)  /* Port Reset */
#define XHCI_PORTSC_PP    (1<<9)  /* Port Power */
#define XHCI_PORTSC_PLS   (0xF<<5) /* Port Link State */

/* TRB Types */
#define TRB_NORMAL        1
#define TRB_SETUP         2
#define TRB_DATA          3
#define TRB_STATUS        4
#define TRB_ISOCH         5
#define TRB_LINK          6
#define TRB_EVENT_DATA    7
#define TRB_NO_OP         8
#define TRB_ENABLE_SLOT   9
#define TRB_DISABLE_SLOT  10
#define TRB_ADDRESS_DEV   11
#define TRB_CONFIG_EP     12
#define TRB_CMD_COMPLETE  33
#define TRB_PORT_STATUS   34
#define TRB_TRANSFER      32

/* USB Standard Request Types */
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_REQ_SET_ADDRESS     0x05
#define USB_REQ_SET_CONFIG      0x09
#define USB_REQ_SET_PROTOCOL    0x0B
#define USB_REQ_SET_IDLE        0x0A

/* USB Descriptor Types */
#define USB_DESC_DEVICE     0x01
#define USB_DESC_CONFIG     0x02
#define USB_DESC_STRING     0x03
#define USB_DESC_INTERFACE  0x04
#define USB_DESC_ENDPOINT   0x05
#define USB_DESC_HUB        0x29

/* USB Classes */
#define USB_CLASS_HID       0x03
#define USB_CLASS_MASS      0x08
#define USB_CLASS_HUB       0x09

/* ---- TRB (Transfer Request Block) ---- */
typedef struct __attribute__((packed)) {
    u64 parameter;
    u32 status;
    u32 control;
} xhci_trb_t;

/* ---- XHCI Controller State ---- */
#define XHCI_MAX_PORTS   16
#define XHCI_MAX_SLOTS   32
#define XHCI_MAX_ENDPOINTS 32
#define XHCI_RING_SIZE   256

typedef struct {
    u8    *mmio_base;     /* MMIO region virtual address */
    u8    *cap_regs;      /* Capability registers */
    u8    *op_regs;       /* Operational registers */
    u8    *rt_regs;       /* Runtime registers */
    u32   *db_regs;       /* Doorbell registers */
    u8     cap_length;
    u16    num_ports;
    u8     num_slots;
    bool   initialized;

    /* Command Ring */
    xhci_trb_t *cmd_ring;
    u32         cmd_enqueue;
    int         cmd_cycle;

    /* Event Ring */
    xhci_trb_t *evt_ring;
    u32         evt_dequeue;
    int         evt_cycle;

    /* Device Context Base Address Array */
    u64        *dcbaa;

    /* Per-slot input/output context arrays (simplified) */
    u8         *slot_contexts[XHCI_MAX_SLOTS];
} xhci_ctrl_t;

/* ---- Public API ---- */

/* Probe all PCI devices for XHCI controllers and initialize them */
void xhci_probe_all(void);

/* Initialize a single XHCI controller at given MMIO address */
bool xhci_init(xhci_ctrl_t *ctrl, u64 mmio_phys, usize mmio_size);

/* Reset the host controller */
bool xhci_hc_reset(xhci_ctrl_t *ctrl);

/* Reset a USB port and return true if device detected */
bool xhci_port_reset(xhci_ctrl_t *ctrl, int port);

/* Submit a control transfer (8-byte setup packet) */
bool xhci_control_transfer(xhci_ctrl_t *ctrl, int slot,
                            u8 bmRequestType, u8 bRequest, u16 wValue,
                            u16 wIndex, u16 wLength, void *data);

/* Get global XHCI controller (first one found) */
xhci_ctrl_t *xhci_get_controller(void);
