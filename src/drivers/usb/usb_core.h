/* ============================================================================
 * YamKernel — USB Core: Device Enumeration + Class Driver Dispatch
 * ============================================================================ */
#pragma once
#include <nexus/types.h>
#include "xhci.h"

#define USB_MAX_DEVICES 32

/* ---- USB Standard Descriptors ---- */
typedef struct __attribute__((packed)) {
    u8  bLength;
    u8  bDescriptorType;    /* 0x01 = Device */
    u16 bcdUSB;
    u8  bDeviceClass;
    u8  bDeviceSubClass;
    u8  bDeviceProtocol;
    u8  bMaxPacketSize0;
    u16 idVendor;
    u16 idProduct;
    u16 bcdDevice;
    u8  iManufacturer;
    u8  iProduct;
    u8  iSerialNumber;
    u8  bNumConfigurations;
} usb_device_desc_t;

typedef struct __attribute__((packed)) {
    u8  bLength;
    u8  bDescriptorType;    /* 0x02 = Configuration */
    u16 wTotalLength;
    u8  bNumInterfaces;
    u8  bConfigurationValue;
    u8  iConfiguration;
    u8  bmAttributes;
    u8  bMaxPower;
} usb_config_desc_t;

typedef struct __attribute__((packed)) {
    u8  bLength;
    u8  bDescriptorType;    /* 0x04 = Interface */
    u8  bInterfaceNumber;
    u8  bAlternateSetting;
    u8  bNumEndpoints;
    u8  bInterfaceClass;
    u8  bInterfaceSubClass;
    u8  bInterfaceProtocol;
    u8  iInterface;
} usb_interface_desc_t;

typedef struct __attribute__((packed)) {
    u8  bLength;
    u8  bDescriptorType;    /* 0x05 = Endpoint */
    u8  bEndpointAddress;
    u8  bmAttributes;
    u16 wMaxPacketSize;
    u8  bInterval;
} usb_endpoint_desc_t;

/* ---- USB Device Registry ---- */
typedef struct {
    bool              active;
    int               slot;
    int               port;
    u16               vendor_id;
    u16               product_id;
    u8                device_class;
    u8                device_subclass;
    u8                protocol;
    usb_device_desc_t desc;
    xhci_ctrl_t      *ctrl;
} usb_device_t;

/* Initialize USB core */
void usb_core_init(void);

/* Enumerate a USB device on given port */
void usb_enumerate_device(xhci_ctrl_t *ctrl, int port);

/* Get device by slot */
usb_device_t *usb_get_device(int slot);

/* Return count of registered devices */
int usb_device_count(void);
