/* ============================================================================
 * YamKernel — USB Core Implementation
 * ============================================================================ */
#include "usb_core.h"
#include "hid.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"
#include "../../mem/heap.h"

static usb_device_t g_devices[USB_MAX_DEVICES];
static int g_device_count = 0;

void usb_core_init(void) {
    memset(g_devices, 0, sizeof(g_devices));
    g_device_count = 0;
}

void usb_enumerate_device(xhci_ctrl_t *ctrl, int port) {
    if (g_device_count >= USB_MAX_DEVICES) return;
    usb_device_t *dev = &g_devices[g_device_count];
    memset(dev, 0, sizeof(*dev));
    dev->ctrl = ctrl;
    dev->port = port;
    dev->slot = g_device_count + 1;
    dev->active = true;

    /* Allocate a temp buffer for descriptors */
    u8 *buf = (u8 *)kmalloc(256);
    if (!buf) return;
    memset(buf, 0, 256);

    /* GET_DESCRIPTOR: Device Descriptor */
    bool ok = xhci_control_transfer(ctrl, dev->slot,
        0x80, USB_REQ_GET_DESCRIPTOR,
        (USB_DESC_DEVICE << 8), 0, 18, buf);

    if (!ok) {
        kprintf_color(0xFFFF8800, "[USB] Port %d: GET_DESCRIPTOR failed\n", port);
        kfree(buf); dev->active = false; return;
    }

    usb_device_desc_t *dd = (usb_device_desc_t *)buf;
    dev->desc          = *dd;
    dev->vendor_id     = dd->idVendor;
    dev->product_id    = dd->idProduct;
    dev->device_class  = dd->bDeviceClass;

    kprintf_color(0xFF00FF88,
        "[USB] Port %d: VID=%04x PID=%04x Class=%02x\n",
        port, dd->idVendor, dd->idProduct, dd->bDeviceClass);

    /* SET_ADDRESS */
    xhci_control_transfer(ctrl, dev->slot,
        0x00, USB_REQ_SET_ADDRESS, dev->slot, 0, 0, NULL);

    /* GET_DESCRIPTOR: Configuration */
    memset(buf, 0, 256);
    xhci_control_transfer(ctrl, dev->slot,
        0x80, USB_REQ_GET_DESCRIPTOR,
        (USB_DESC_CONFIG << 8), 0, 255, buf);

    /* SET_CONFIGURATION */
    u8 config_val = buf[5]; /* bConfigurationValue */
    xhci_control_transfer(ctrl, dev->slot,
        0x00, USB_REQ_SET_CONFIG, config_val, 0, 0, NULL);

    /* Scan interface descriptors */
    u16 total_len = *(u16 *)(buf + 2);
    if (total_len > 255) total_len = 255;
    u8 *p = buf;
    u8 *end = buf + total_len;

    while (p < end) {
        u8 len  = p[0];
        u8 type = p[1];
        if (len < 2) break;
        if (type == USB_DESC_INTERFACE) {
            usb_interface_desc_t *iface = (usb_interface_desc_t *)p;
            dev->device_class    = iface->bInterfaceClass;
            dev->device_subclass = iface->bInterfaceSubClass;
            dev->protocol        = iface->bInterfaceProtocol;

            if (iface->bInterfaceClass == USB_CLASS_HID) {
                kprintf_color(0xFF00DDFF, "[USB] HID device: subclass=%d proto=%d\n",
                              iface->bInterfaceSubClass, iface->bInterfaceProtocol);
                hid_probe(ctrl, dev->slot, iface->bInterfaceProtocol);
            }
        }
        p += len;
    }

    kfree(buf);
    g_device_count++;
}

usb_device_t *usb_get_device(int slot) {
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].slot == slot && g_devices[i].active)
            return &g_devices[i];
    }
    return NULL;
}
int usb_device_count(void) { return g_device_count; }
