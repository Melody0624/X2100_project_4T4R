#include "../usb.h"
#include <common.h>
#include <usb/host_kbd.h>

/*
 * USB HID (Human Interface Device) interface class code
 */

#define USB_INTERFACE_CLASS_HID         3

/*
 * USB HID interface subclass and protocol codes
 */

#define USB_INTERFACE_SUBCLASS_BOOT     1
#define USB_INTERFACE_PROTOCOL_KEYBOARD 1

/*
 * HID class descriptor types
 */

#define HID_DT_HID                  (USB_TYPE_CLASS | 0x01)
#define HID_DT_REPORT               (USB_TYPE_CLASS | 0x02)
#define HID_DT_PHYSICAL             (USB_TYPE_CLASS | 0x03)

#define HID_MAX_DESCRIPTOR_SIZE     4096

/*
 * HID class requests
 */
#define HID_REQ_SET_LED             0x09
#define HID_REQ_SET_IDLE            0x0A

struct hid_class_descriptor {
    u8 bDescriptorType;
    u16 wDescriptorLength;
} __attribute__((packed));

struct hid_descriptor {
    u8 bLength;
    u8 bDescriptorType;
    u16 bcdHID;
    u8 bCountryCode;
    u8 bNumDescriptors;
    struct hid_class_descriptor rpt_desc;

    struct hid_class_descriptor opt_descs[];
} __attribute__((packed));

struct usb_kbd {
    struct usb_device *usbdev;
    struct urb *irq;
    char name[128];

    unsigned char *report;
    unsigned int report_len;

    unsigned char *scancode;
    dma_addr_t scancode_dma;
};

static kbd_notify_callback_t g_notify_callback;

void usb_kbd_register_callback(kbd_notify_callback_t callback)
{
    g_notify_callback = callback;
}

static void usb_kbd_irq(struct urb *urb)
{
    struct usb_kbd *kbd = urb->context;
    int status;
    kbd_notify_callback_t notify_callback = g_notify_callback;

    switch (urb->status) {
        case 0: /* success */
            break;
        case -ECONNRESET: /* unlink */
        case -ENOENT:
        case -ESHUTDOWN:
        case -EPROTO:
        case -EPIPE:
            return;
        default: /* error */
            printf("Error: %s urb->status %d\n", __func__, urb->status);
            goto resubmit;
    }

    if (notify_callback) {
        notify_callback(kbd->report, kbd->report_len, urb->transfer_buffer, urb->actual_length);
    }

resubmit:
    status = usb_submit_urb(urb);
    if (status)
        printf("can't resubmit intr, in %s:%d, status %d\n", __FILE__, __LINE__, status);
}

static int hid_set_idle(struct usb_device *dev, int ifnum, int report, int idle)
{
    return usb_control_msg(dev, usb_sndctrlpipe(dev, 0), HID_REQ_SET_IDLE,
                   USB_TYPE_CLASS | USB_RECIP_INTERFACE, (idle << 8) | report, ifnum,
                   NULL, 0, USB_CTRL_SET_TIMEOUT);
}

static int hid_get_class_descriptor(struct usb_device *dev, int ifnum, unsigned char type,
                    void *buf, int size)
{
    int result, retries = 4;

    memset(buf, 0, size);

    do {
        result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), USB_REQ_GET_DESCRIPTOR,
                     USB_RECIP_INTERFACE | USB_DIR_IN, (type << 8), ifnum, buf,
                     size, USB_CTRL_GET_TIMEOUT);
        retries--;
    } while (result < size && retries);
    return result;
}

static int kbd_set_led(struct usb_device *dev, int ifnum, unsigned char led)
{
    return usb_control_msg_send(dev, usb_sndctrlpipe(dev, 0), HID_REQ_SET_LED,
                    USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0x200, ifnum, &led, 1,
                    USB_CTRL_SET_TIMEOUT);
}

static int usb_kbd_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    struct usb_device *dev = intf->usb_dev;
    struct usb_host_interface *interface = NULL;
    struct usb_endpoint_descriptor *endpoint = NULL;
    struct usb_kbd *kbd = NULL;
    struct hid_descriptor *hdesc;
    int pipe = 0, maxp = 0;
    unsigned int rsize = 0;
    int error = -ENOMEM;
    int i = 0;

    interface = intf->cur_altsetting;
    if (interface->desc.bNumEndpoints < 1) {
        return -ENODEV;
    }

    for (i = 0; i < interface->desc.bNumEndpoints; i++) {
        endpoint = &interface->endpoint[i].desc;
        if (usb_endpoint_is_int_in(endpoint)) {
            break;
        }
    }

    if (i == interface->desc.bNumEndpoints) {
        return -ENODEV;
    }

    if (usb_get_extra_descriptor((char *)interface->extra, interface->extralen, HID_DT_HID,
                     (void **)&hdesc, sizeof(*hdesc)) &&
        usb_get_extra_descriptor((char *)interface->endpoint[0].extra,
                     interface->endpoint[0].extralen, HID_DT_HID, (void **)&hdesc,
                     sizeof(*hdesc))) {
        return -ENODEV;
    }

    if (!hdesc->bNumDescriptors ||
        hdesc->bLength != sizeof(*hdesc) + (hdesc->bNumDescriptors - 1) *
                               sizeof(struct hid_class_descriptor)) {
        return -EINVAL;
    }

    if (hdesc->rpt_desc.bDescriptorType == HID_DT_REPORT)
        rsize = hdesc->rpt_desc.wDescriptorLength;

    if (!rsize || rsize > HID_MAX_DESCRIPTOR_SIZE) {
        return -EINVAL;
    }

    pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
    maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

    kbd = malloc(sizeof(struct usb_kbd));
    if (!kbd)
        goto fail;

    memset(kbd, 0, sizeof(struct usb_kbd));

    kbd->report_len = rsize;
    kbd->report = cache_align_malloc(kbd->report_len);
    if (!kbd->report) {
        goto fail;
    }

    hid_set_idle(dev, interface->desc.bInterfaceNumber, 0, 0);

    error = hid_get_class_descriptor(dev, interface->desc.bInterfaceNumber, HID_DT_REPORT,
                     kbd->report, kbd->report_len);
    if (error < 0) {
        goto fail;
    }

    kbd_set_led(dev, interface->desc.bInterfaceNumber, 0);

    kbd->irq = usb_alloc_urb(0);
    if (!kbd->irq)
        goto fail;

    kbd->scancode = usb_alloc_coherent(dev, 8, &kbd->scancode_dma);
    if (!kbd->scancode)
        goto fail;

    kbd->usbdev = dev;

    if (dev->manufacturer)
        strlcpy(kbd->name, dev->manufacturer, sizeof(kbd->name));

    if (dev->product) {
        if (dev->manufacturer)
            strlcat(kbd->name, " ", sizeof(kbd->name));
        strlcat(kbd->name, dev->product, sizeof(kbd->name));
    }

    if (!strlen(kbd->name))
        snprintf(kbd->name, sizeof(kbd->name), "USB HIDBP Keyboard %04x:%04x",
             dev->descriptor.idVendor, dev->descriptor.idProduct);

    usb_fill_int_urb(kbd->irq, dev, pipe, kbd->scancode, (maxp > 8 ? 8 : maxp), usb_kbd_irq,
             kbd, endpoint->bInterval);
    kbd->irq->transfer_dma = kbd->scancode_dma;
    kbd->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    if (usb_submit_urb(kbd->irq))
        return -EIO;

    return 0;

fail:
    if (kbd) {
        if (kbd->scancode)
            usb_free_coherent(dev, 8, kbd->scancode);

        if (kbd->irq)
            usb_free_urb(kbd->irq);

        if (kbd->report)
            free(kbd->report);
        free(kbd);
    }
    return error;
}

static void usb_kbd_disconnect(struct usb_interface *intf)
{
    struct usb_kbd *kbd = intf->private_data;

    intf->private_data = NULL;
    if (kbd) {
        usb_kill_urb(kbd->irq);
        usb_free_urb(kbd->irq);
        usb_free_coherent(kbd->usbdev, 8, kbd->scancode);
        free(kbd);
    }
}

static const struct usb_device_id usb_kbd_id_table[] = {
    {USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT,
                USB_INTERFACE_PROTOCOL_KEYBOARD)},
    {} /* Terminating entry */
};

static struct usb_driver usb_kbd_driver = {
    .name = "usbkbd",
    .probe = usb_kbd_probe,
    .disconnect = usb_kbd_disconnect,
    .id_table = usb_kbd_id_table,
};

void usb_hid_kbd_driver_register(void)
{
    usb_register_driver(&usb_kbd_driver);
}

void usb_hid_kbd_driver_deregister(void)
{
    usb_deregister(&usb_kbd_driver);
}
