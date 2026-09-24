#include "../usb.h"
#include <common.h>
#include <usb/host_hid_vendor.h>

/*
 * USB HID (Human Interface Device) interface class code
 */

#define USB_INTERFACE_CLASS_HID         3

/*
 * USB HID interface subclass and protocol codes
 */
#define USB_INTERFACE_SUBCLASS_NONE     0
#define USB_INTERFACE_PROTOCOL_NONE     0

static const struct usb_device_id usb_hid_vendor_id_table[] = {
    {
        .match_flags = USB_DEVICE_ID_MATCH_INT_CLASS | USB_DEVICE_ID_MATCH_INT_SUBCLASS | USB_DEVICE_ID_MATCH_INT_PROTOCOL,
        .bInterfaceClass = USB_INTERFACE_CLASS_HID,
        .bInterfaceSubClass = USB_INTERFACE_PROTOCOL_NONE,
        .bInterfaceProtocol = USB_INTERFACE_SUBCLASS_NONE,
    },
    { }    /* Terminating entry */
};

struct usb_hid_vendor {
    struct usb_device *usbdev;
    /* data interface */
    struct usb_interface *data;

    char name[128];

    /* i/o pipes */
    unsigned int in, out;
    /* usb device endpoint send/receive data size */
    unsigned int in_size;
    unsigned int out_size;

    /* hid_vendor minor number */
    u8 minor;
    /* someone has this hid_vendor's device open */
    bool opened;
    bool exiting;
    unsigned int used;
};

static struct usb_driver usb_hid_vendor_driver;

static u32 devices_bit;
static hid_vendor_device_callback_t device_callback;

static struct usb_hid_vendor *hid_vendor_table[HID_VENDOR_MINORS];
static struct mutex device_lock[HID_VENDOR_MINORS];
static thread_cond_t free_cond[HID_VENDOR_MINORS];

#define HID_VENDOR_READY(hid_vendor)    (hid_vendor && hid_vendor->usbdev && hid_vendor->opened && !hid_vendor->exiting)

void usb_host_hid_vendor_register_callback(hid_vendor_device_callback_t callback)
{
    device_callback = callback;
}

u32 usb_host_hid_vendor_get_state(void)
{
    return devices_bit;
}

static void update_devices_bit(void)
{
    int i;
    struct usb_hid_vendor *hid_vendor;
    u32 dev_bit = 0;
    hid_vendor_device_callback_t callback = device_callback;

    for (i = 0; i < HID_VENDOR_MINORS; i++) {
        hid_vendor = hid_vendor_table[i];
        if (hid_vendor && hid_vendor->usbdev)
            dev_bit |= (1 << i);
    }

    devices_bit = dev_bit;
    if (callback)
        callback(dev_bit);
}

int usb_host_hid_vendor_open(u8 id)
{
    int ret = 0;
    struct usb_hid_vendor *hid_vendor = NULL;

    if (id >= HID_VENDOR_MINORS)
        return -ENODEV;

    mutex_lock(&device_lock[id]);
    hid_vendor = hid_vendor_table[id];
    if (!hid_vendor || !hid_vendor->usbdev) {
        ret = -ENODEV;
        goto err_out;
    }

    if (hid_vendor->opened) {
        ret = -EBUSY;
        goto err_out;
    }

    hid_vendor->opened = true;

err_out:
    mutex_unlock(&device_lock[id]);
    return ret;
}

void usb_host_hid_vendor_close(u8 id)
{
    struct usb_hid_vendor *hid_vendor;

    if (id >= HID_VENDOR_MINORS)
        return;

    mutex_lock(&device_lock[id]);
    hid_vendor = hid_vendor_table[id];
    if (!hid_vendor || !hid_vendor->opened || hid_vendor->exiting)
        goto done;

    hid_vendor->exiting = true;

    while (hid_vendor->used)
        thread_cond_wait(&free_cond[id], &device_lock[id]);

    hid_vendor->exiting = false;
    hid_vendor->opened = false;

    if (!hid_vendor->usbdev) {
        assert(hid_vendor->minor == id);
        hid_vendor_table[hid_vendor->minor] = NULL;
        free(hid_vendor);
    }

done:
    mutex_unlock(&device_lock[id]);
}

int usb_host_hid_vendor_read(u8 id, void *buf, int len, int timeout)
{
    int ret = 0;
    int actual_len = 0;
    struct usb_hid_vendor *hid_vendor = NULL;

    if (!buf || !len)
      return -EINVAL;

    if (id >= HID_VENDOR_MINORS)
        return -ENODEV;

    mutex_lock(&device_lock[id]);
    hid_vendor = hid_vendor_table[id];
    if (!HID_VENDOR_READY(hid_vendor)) {
        ret = -ENODEV;
        goto err_out;
    }

    if (len != hid_vendor->in_size) {
        printf("read len %d != %d\n", len, hid_vendor->in_size);
        ret = -EINVAL;
        goto err_out;
    }

    hid_vendor->used++;
    mutex_unlock(&device_lock[id]);

    ret = usb_interrupt_msg(hid_vendor->usbdev, hid_vendor->in, buf, len, &actual_len, timeout);

    mutex_lock(&device_lock[id]);
    hid_vendor->used--;
    if (hid_vendor->exiting && !hid_vendor->used)
        thread_cond_signal(&free_cond[id]);

err_out:
    mutex_unlock(&device_lock[id]);
    return ret < 0 ? ret : actual_len;
}

int usb_host_hid_vendor_write(u8 id, void *data, int len, int timeout)
{
    int ret = 0;
    int actual_len = 0;
    struct usb_hid_vendor *hid_vendor = NULL;

    if (!data || !len)
      return -EINVAL;

    if (id >= HID_VENDOR_MINORS)
        return -ENODEV;

    mutex_lock(&device_lock[id]);
    hid_vendor = hid_vendor_table[id];
    if (!HID_VENDOR_READY(hid_vendor)) {
        ret = -ENODEV;
        goto err_out;
    }

    if (len != hid_vendor->out_size) {
        printf("write len %d != %d\n", len, hid_vendor->out_size);
        ret = -EINVAL;
        goto err_out;
    }

    hid_vendor->used++;
    mutex_unlock(&device_lock[id]);

    ret = usb_interrupt_msg(hid_vendor->usbdev, hid_vendor->out, data, len, &actual_len, timeout);

    mutex_lock(&device_lock[id]);
    hid_vendor->used--;
    if (hid_vendor->exiting && !hid_vendor->used)
        thread_cond_signal(&free_cond[id]);

err_out:
    mutex_unlock(&device_lock[id]);
    return ret < 0 ? ret : actual_len;
}

int usb_hid_vendor_get_in_size(u8 id)
{
    int ret = 0;
    struct usb_hid_vendor *hid_vendor = NULL;

    mutex_lock(&device_lock[id]);
    hid_vendor = hid_vendor_table[id];
    if (!HID_VENDOR_READY(hid_vendor)) {
        ret = -ENODEV;
        goto err_out;
    }

    ret = hid_vendor->in_size;

err_out:
    mutex_unlock(&device_lock[id]);
    return ret;
}

int usb_hid_vendor_get_out_size(u8 id)
{
    int ret = 0;
    struct usb_hid_vendor *hid_vendor = NULL;

    mutex_lock(&device_lock[id]);
    hid_vendor = hid_vendor_table[id];
    if (!HID_VENDOR_READY(hid_vendor)) {
        ret = -ENODEV;
        goto err_out;
    }

    ret = hid_vendor->out_size;

err_out:
    mutex_unlock(&device_lock[id]);
    return ret;
}

static int usb_hid_vendor_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    int i = 0;
    int minor = 0;
    int error = -ENOMEM;
    int in_pipe = 0, in_size = 0;
    int out_pipe = 0, out_size = 0;
    struct usb_device *dev = intf->usb_dev;
    struct usb_hid_vendor *hid_vendor = NULL;
    struct usb_endpoint_descriptor *endpoint = NULL;
    struct usb_endpoint_descriptor *in_endpoint = NULL;
    struct usb_endpoint_descriptor *out_endpoint = NULL;
    struct usb_host_interface *interface = intf->cur_altsetting;

    /* check the number of communication endpoint */
    if (interface->desc.bNumEndpoints < 2)
        return -EINVAL;

    for (i = 0; i < interface->desc.bNumEndpoints; i++) {
        endpoint = &interface->endpoint[i].desc;

        if (!out_endpoint && usb_endpoint_is_int_out(endpoint)) {
            out_endpoint = endpoint;
            out_pipe = usb_sndintpipe(dev, out_endpoint->bEndpointAddress);
            out_size = usb_maxpacket(dev, out_pipe, usb_pipeout(out_pipe));
        } else if (!in_endpoint && usb_endpoint_is_int_in(endpoint)) {
            in_endpoint = endpoint;
            in_pipe = usb_rcvintpipe(dev, in_endpoint->bEndpointAddress);
            in_size = usb_maxpacket(dev, in_pipe, usb_pipeout(in_pipe));
        }
    }

    if (!in_endpoint || !out_endpoint) {
        printf("check endpoint is null! in[0x%x] out[0x%x]\n", (unsigned int)in_endpoint, (unsigned int)out_endpoint);
        return -ENODEV;
    }

    for (minor = 0; minor < HID_VENDOR_MINORS && hid_vendor_table[minor]; minor++);

    if (minor == HID_VENDOR_MINORS) {
        printf("%s: no more free hid_vendor devices\n", __func__);
        return -ENODEV;
    }

    hid_vendor = malloc(sizeof(struct usb_hid_vendor));
    if (!hid_vendor)
        goto fail1;

    memset(hid_vendor, 0, sizeof(struct usb_hid_vendor));

    hid_vendor->data = intf;
    hid_vendor->minor = minor;
    hid_vendor->in = in_pipe;
    hid_vendor->out = out_pipe;
    hid_vendor->usbdev = dev;
    hid_vendor->in_size = in_size;
    hid_vendor->out_size = out_size;

    if (dev->manufacturer)
        strlcpy(hid_vendor->name, dev->manufacturer, sizeof(hid_vendor->name));

    if (dev->product) {
        if (dev->manufacturer)
            strlcat(hid_vendor->name, " ", sizeof(hid_vendor->name));
        strlcat(hid_vendor->name, dev->product, sizeof(hid_vendor->name));
    }

    if (!strlen(hid_vendor->name))
        snprintf(hid_vendor->name, sizeof(hid_vendor->name),
             "USB HID VENDOR %04x:%04x",
             dev->descriptor.idVendor,
             dev->descriptor.idProduct);

    intf->private_data = hid_vendor;
    usb_driver_claim_interface(&usb_hid_vendor_driver, intf, hid_vendor);

    hid_vendor_table[minor] = hid_vendor;

    printf("NEW USB HID VENDOR device[%d]\n", minor);

    update_devices_bit();

    return 0;

fail1:
    if (hid_vendor)
        free(hid_vendor);
    return error;
}

static void usb_hid_vendor_disconnect(struct usb_interface *intf)
{
    u32 id = 0;
    struct usb_hid_vendor *hid_vendor = intf->private_data;

    if (!hid_vendor || !hid_vendor->usbdev)
        return;

    id = hid_vendor->minor;
    assert(id < HID_VENDOR_MINORS);

    mutex_lock(&device_lock[id]);
    hid_vendor->usbdev = NULL;
    usb_set_intfdata(hid_vendor->data, NULL);

    usb_driver_release_interface(&usb_hid_vendor_driver, hid_vendor->data);

    if (!hid_vendor->opened) {
        hid_vendor_table[hid_vendor->minor] = NULL;
        free(hid_vendor);
    }

    mutex_unlock(&device_lock[id]);

    printf("USB HID VENDOR device[%d] disconnect\n", id);

    update_devices_bit();
}

static struct usb_driver usb_hid_vendor_driver = {
    .name        = "hid_vendor",
    .probe       = usb_hid_vendor_probe,
    .disconnect  = usb_hid_vendor_disconnect,
    .id_table    = usb_hid_vendor_id_table,
};

void usb_hid_vendor_driver_register(void)
{
    usb_register_driver(&usb_hid_vendor_driver);
}

void usb_hid_vendor_driver_deregister(void)
{
    usb_deregister(&usb_hid_vendor_driver);
}