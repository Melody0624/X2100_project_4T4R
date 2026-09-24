#include "wwan.h"
#include "option.c"

static struct usb_driver wwan_driver;

static u32 devices_bit;
static wwan_device_callback_t device_callback;
static struct mutex wwan_lock;

static struct usb_serial_port *port_table[WWAN_MINORS];
static struct mutex port_lock[WWAN_MINORS];
static thread_cond_t free_cond[WWAN_MINORS];

#define WWAN_READY(port)    (port && port->dev && port->opened && !port->exiting)

void usb_host_wwan_register_callback(wwan_device_callback_t callback)
{
    device_callback = callback;
}

u32 usb_host_wwan_get_devices_bit(void)
{
    return devices_bit;
}

static void update_devices_bit(void)
{
    int i;
    struct usb_serial_port *port;
    u32 dev_bit = 0;
    wwan_device_callback_t callback = device_callback;

    for (i = 0; i < WWAN_MINORS; i++) {
        port = port_table[i];
        if (port && port->dev)
            dev_bit |= (1 << i);
    }

    devices_bit = dev_bit;
    if (callback)
        callback(dev_bit);
}

static int usb_wwan_bulk_msg(struct usb_device *usb_dev, unsigned int pipe,
        int zlp_flags, void *data, int len, int *actual_length, int timeout)
{
    struct urb *urb;
    struct usb_host_endpoint *ep;

    ep = usb_pipe_endpoint(usb_dev, pipe);
    if (!ep || len < 0)
        return -EINVAL;

    urb = usb_alloc_urb(0);
    if (!urb)
        return -ENOMEM;

    usb_fill_bulk_urb(urb, usb_dev, pipe, data, len,
            usb_api_blocking_completion, NULL);

    if (zlp_flags)
        urb->transfer_flags |= URB_ZERO_PACKET;

    return usb_start_wait_urb(urb, timeout, actual_length);
}

static bool iface_is_reserved(unsigned long device_flags, u8 ifnum)
{
    if (ifnum > FLAG_IFNUM_MAX)
        return false;

    return device_flags & RSVD(ifnum);
}

static void option_instat_callback(struct urb *urb)
{
    int err;
    int status = urb->status;
    struct usb_serial_port *port = urb->context;
    struct usb_wwan_port_private *portdata = &port->portdata;

    if (status == 0) {
        struct usb_ctrlrequest *req_pkt = urb->transfer_buffer;

        if (!req_pkt) {
            printf("%s: NULL req_pkt\n", __func__);
            return;
        }
        if ((req_pkt->bRequestType == 0xA1) &&
                (req_pkt->bRequest == 0x20)) {
            unsigned char signals = *((unsigned char *)
                    urb->transfer_buffer +
                    sizeof(struct usb_ctrlrequest));

            portdata->cts_state = 1;
            portdata->dcd_state = ((signals & 0x01) ? 1 : 0);
            portdata->dsr_state = ((signals & 0x02) ? 1 : 0);
            portdata->ri_state = ((signals & 0x08) ? 1 : 0);

            if (port->notify_callback)
                port->notify_callback(port->minor, portdata);
        } else {
            printf("%s: type %x req %x\n", __func__,
                req_pkt->bRequestType, req_pkt->bRequest);
        }
    } else if (status == -ENOENT || status == -ESHUTDOWN) {
        printf("%s: urb stopped: %d\n", __func__, status);
    } else
        printf("%s: error %d\n", __func__, status);

    /* Resubmit urb so we continue receiving IRQ data */
    if (status != -ESHUTDOWN && status != -ENOENT) {
        err = usb_submit_urb(urb);
        if (err)
            printf("%s: resubmit intr urb failed. (%d)\n", __func__, err);
    }
}

static void store_endpoint(struct usb_serial_endpoints *epds,
                    struct usb_endpoint_descriptor *epd)
{
    u8 addr = epd->bEndpointAddress;

    if (usb_endpoint_is_bulk_in(epd)) {
        if (epds->num_bulk_in == ARRAY_SIZE(epds->bulk_in))
            return;
        printf("found bulk in endpoint %02x\n", addr);
        epds->bulk_in[epds->num_bulk_in++] = epd;
    } else if (usb_endpoint_is_bulk_out(epd)) {
        if (epds->num_bulk_out == ARRAY_SIZE(epds->bulk_out))
            return;
        printf("found bulk out endpoint %02x\n", addr);
        epds->bulk_out[epds->num_bulk_out++] = epd;
    } else if (usb_endpoint_is_int_in(epd)) {
        if (epds->num_interrupt_in == ARRAY_SIZE(epds->interrupt_in))
            return;
        printf("found interrupt in endpoint %02x\n", addr);
        epds->interrupt_in[epds->num_interrupt_in++] = epd;
    } else if (usb_endpoint_is_int_out(epd)) {
        if (epds->num_interrupt_out == ARRAY_SIZE(epds->interrupt_out))
            return;
        printf("found interrupt out endpoint %02x\n", addr);
        epds->interrupt_out[epds->num_interrupt_out++] = epd;
    }
}

static void find_endpoints(struct usb_serial_endpoints *epds,
                    struct usb_interface *intf)
{
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *epd;
    unsigned int i;

    iface_desc = intf->cur_altsetting;
    for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
        epd = &iface_desc->endpoint[i].desc;
        store_endpoint(epds, epd);
    }
}

static int setup_port_interrupt_in(struct usb_serial_port *port,
                    struct usb_endpoint_descriptor *epd)
{
    struct usb_device *udev = port->dev;
    int buffer_size;

    port->interrupt_in_urb = usb_alloc_urb(0);
    if (!port->interrupt_in_urb)
        return -ENOMEM;

    buffer_size = usb_endpoint_maxp(epd);
    port->interrupt_in_endpointAddress = epd->bEndpointAddress;
    port->interrupt_in_buffer = cache_align_malloc(buffer_size);
    if (!port->interrupt_in_buffer)
        return -ENOMEM;
    memset(port->interrupt_in_buffer, 0, sizeof(buffer_size));

    usb_fill_int_urb(port->interrupt_in_urb, udev,
            usb_rcvintpipe(udev, epd->bEndpointAddress),
            port->interrupt_in_buffer, buffer_size,
            option_instat_callback, port, epd->bInterval);

    return 0;
}

static bool iface_no_modem_control(unsigned long device_flags, u8 ifnum)
{
    if (ifnum > FLAG_IFNUM_MAX)
        return false;

    return device_flags & NCTRL(ifnum);
}

void usb_host_wwan_set_dtr_rts(u8 id, int dtr_state, int rts_state)
{
    struct usb_serial_port *port;
    int ifnum;
    int val = 0;

    if (id >= WWAN_MINORS)
        return;

    mutex_lock(&port_lock[id]);

    port = port_table[id];
    if (!WWAN_READY(port))
        goto err_out;

    ifnum = port->wwan->intf->cur_altsetting->desc.bInterfaceNumber;

    if (!port->wwan->private.use_send_setup)
        goto err_out;

    if (dtr_state)
        val |= WWAN_CTRL_DTR;

    if (rts_state)
        val |= WWAN_CTRL_RTS;

    usb_control_msg(port->dev, usb_sndctrlpipe(port->dev, 0),
            USB_CDC_REQ_SET_CONTROL_LINE_STATE,
            USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
            val, ifnum, NULL, 0, USB_CTRL_SET_TIMEOUT);

    port->portdata.dtr_state = dtr_state;
    port->portdata.rts_state = rts_state;

err_out:
    mutex_unlock(&port_lock[id]);
}

int usb_host_wwan_open(u8 id, wwan_notify_callback_t callback)
{
    struct usb_serial_port *port;
    int ret = 0;

    if (id >= WWAN_MINORS)
        return -ENODEV;

    mutex_lock(&port_lock[id]);

    port = port_table[id];
    if (!port || !port->dev){
        ret = -ENODEV;
        goto err_out;
    }

    if (port->opened) {
        ret = -EBUSY;
        goto err_out;
    }

    port->opened = true;
    port->notify_callback = callback;

    mutex_lock(&wwan_lock);
    port->wwan->opened_port++;
    mutex_unlock(&wwan_lock);

    if (port->interrupt_in_urb && callback) {
        ret = usb_submit_urb(port->interrupt_in_urb);
        if (ret)
            printf("%s: submit int urb failed: %d\n", __func__, ret);
    }
err_out:
    mutex_unlock(&port_lock[id]);

    return ret;
}

void usb_host_wwan_close(u8 id)
{
    struct wwan *wwan;
    struct usb_serial_port *port;

    if (id >= WWAN_MINORS)
        return;

    mutex_lock(&port_lock[id]);
    port = port_table[id];
    if (!port || !port->opened || port->exiting)
        goto done;

    wwan = port->wwan;

    port->exiting = true;

    while (port->used)
        thread_cond_wait(&free_cond[id], &port_lock[id]);

    port->exiting = false;
    port->opened = false;

    if (port->dev) {
        usb_kill_urb(port->interrupt_in_urb);
    } else {
        assert(port->minor == id);
        port_table[port->minor] = NULL;

        usb_free_urb(port->interrupt_in_urb);
        if (port->interrupt_in_buffer)
            free(port->interrupt_in_buffer);

        free(port);
    }

    mutex_lock(&wwan_lock);
    wwan->opened_port--;
    if (wwan->dev && !wwan->opened_port)
        free(wwan);
    mutex_unlock(&wwan_lock);

done:
    mutex_unlock(&port_lock[id]);
}

int usb_host_wwan_read(u8 id, void *data, int len, int timeout)
{
    struct usb_serial_port *port;
    int actual_len = 0;
    int ret = 0;

    if (id >= WWAN_MINORS)
        return -ENODEV;

    mutex_lock(&port_lock[id]);
    port = port_table[id];
    if (!WWAN_READY(port)) {
        ret = -ENODEV;
        goto err_out;
    }

    port->used++;
    mutex_unlock(&port_lock[id]);

    ret = usb_wwan_bulk_msg(port->dev, port->in,
        0, data, len, &actual_len, timeout);

    mutex_lock(&port_lock[id]);
    port->used--;
    if (port->exiting && !port->used)
        thread_cond_signal(&free_cond[id]);

err_out:
    mutex_unlock(&port_lock[id]);
    return ret < 0 ? ret : actual_len;
}

int usb_host_wwan_write(u8 id, void *data, int len, int timeout)
{
    struct usb_serial_port *port;
    int actual_len = 0;
    int ret = 0;

    if (id >= WWAN_MINORS)
        return -ENODEV;

    if (!data || !len)
        return -EINVAL;

    mutex_lock(&port_lock[id]);

    port = port_table[id];
    if (!WWAN_READY(port)) {
        ret = -ENODEV;
        goto err_out;
    }

    port->used++;
    mutex_unlock(&port_lock[id]);

    ret = usb_wwan_bulk_msg(port->dev, port->out,
        port->wwan->private.use_zlp, data, len, &actual_len, timeout);

    mutex_lock(&port_lock[id]);
    port->used--;
    if (port->exiting && !port->used)
        thread_cond_signal(&free_cond[id]);

err_out:
    mutex_unlock(&port_lock[id]);
    return ret < 0 ? ret : actual_len;
}

static int wwan_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    struct wwan *wwan = NULL;
    struct usb_device *usb_dev = interface_to_usbdev(intf);
    struct usb_interface_descriptor *iface_desc = &intf->cur_altsetting->desc;
    unsigned long device_flags = id->driver_info;
    struct usb_serial_endpoints epds = {0};
    struct usb_serial_port *port;
    unsigned char max_endpoints;
    int minor;
    int i, ret;

    /* Never bind to the CD-Rom emulation interface    */
    if (iface_desc->bInterfaceClass == USB_CLASS_MASS_STORAGE)
        return -ENODEV;

    /*
     * Don't bind reserved interfaces (like network ones) which often have
     * the same class/subclass/protocol as the serial interfaces.  Look at
     * the Windows driver .INF files for reserved interface numbers.
     */
    if (iface_is_reserved(device_flags, iface_desc->bInterfaceNumber))
        return -ENODEV;

    /*
     * Allow matching on bNumEndpoints for devices whose interface numbers
     * can change (e.g. Quectel EP06).
     */
    if (device_flags & NUMEP2 && iface_desc->bNumEndpoints != 2)
        return -ENODEV;

    if (!(wwan = malloc(sizeof(*wwan)))) {
        printf("%s: out of memory (wwan malloc)\n", __func__);
        return -ENOMEM;
    }
    memset(wwan, 0, sizeof(*wwan));

    wwan->intf = intf;
    wwan->dev = usb_dev;

    find_endpoints(&epds, intf);

    wwan->num_bulk_in = epds.num_bulk_in;
    wwan->num_bulk_out = epds.num_bulk_out;
    wwan->num_interrupt_in = epds.num_interrupt_in;
    wwan->num_interrupt_out = epds.num_interrupt_out;

    max_endpoints = max(epds.num_bulk_in, epds.num_bulk_out);
    max_endpoints = max(max_endpoints, epds.num_interrupt_in);
    wwan->num_port_pointers = max_endpoints;

    for (i = 0; i < max_endpoints; ++i) {
        port = malloc(sizeof(struct usb_serial_port));
        if (!port) {
            ret = -ENOMEM;
            goto err_free_ports;
        }
        memset(port, 0, sizeof(*port));

        port->wwan = wwan;
        port->dev = usb_dev;
        port->port_number = i;
        port->minor = -1;
        wwan->port[i] = port;
    }

    for (i = 0; i < epds.num_bulk_in; ++i) {
        wwan->port[i]->bulk_in_endpointAddress = epds.bulk_in[i]->bEndpointAddress;
        wwan->port[i]->in = usb_rcvbulkpipe(usb_dev, epds.bulk_in[i]->bEndpointAddress);
    }

    for (i = 0; i < epds.num_bulk_out; ++i) {
        wwan->port[i]->bulk_out_endpointAddress = epds.bulk_out[i]->bEndpointAddress;
        wwan->port[i]->out = usb_sndbulkpipe(usb_dev, epds.bulk_out[i]->bEndpointAddress);
    }

    for (i = 0; i < epds.num_interrupt_in; ++i) {
        ret = setup_port_interrupt_in(wwan->port[i], epds.interrupt_in[i]);
        if (ret)
            goto err_free_intr;
    }

    if (epds.num_interrupt_out)
        printf("USB WWAN device claims unsupport interrupt out transfers\n");

    /* option attach */
    if (!iface_no_modem_control(device_flags, iface_desc->bInterfaceNumber))
        wwan->private.use_send_setup = 1;

    if (device_flags & ZLP)
        wwan->private.use_zlp = 1;

    for (i = 0; i < max_endpoints; ++i) {
        for (minor = 0; minor < WWAN_MINORS && port_table[minor]; minor++);

        if (minor == WWAN_MINORS) {
            printf("%s: no more free wwan devices\n", __func__);
            ret = -ENODEV;
            goto err_clear_table;
        }

        wwan->port[i]->minor = minor;
        port_table[minor] = wwan->port[i];

        printf("NEW USB WWAN device [%d]\n", minor);
    }

    usb_set_intfdata(intf, wwan);

    update_devices_bit();

    return 0;

err_clear_table:
    for (i = 0; i < max_endpoints; ++i) {
        minor = wwan->port[i]->minor;
        if (minor < 0)
            continue;

        if (port_table[minor])
            port_table[minor] = NULL;
    }
err_free_intr:
    for (i = 0; i < max_endpoints; ++i) {
        port = wwan->port[i];

        usb_free_urb(port->interrupt_in_urb);
        if (port->interrupt_in_buffer)
            free(port->interrupt_in_buffer);
    }
err_free_ports:
    for (i = 0; i < max_endpoints; ++i) {
        if (wwan->port[i])
            free(wwan->port[i]);
    }

    free(wwan);
    return ret;
}

static void wwan_remove(struct usb_interface *intf)
{
    u32 id;
    struct wwan *wwan = usb_get_intfdata(intf);
    struct usb_serial_port *port;
    int i;

    if (!wwan || !wwan->dev)
        return;

    for (i = 0; i < wwan->num_port_pointers; i++) {
        port = wwan->port[i];
        if (!port)
            continue;

        id = port->minor;
        assert(id < WWAN_MINORS);

        mutex_lock(&port_lock[id]);

        port->dev = NULL;
        usb_kill_urb(port->interrupt_in_urb);

        if (!port->opened) {
            usb_free_urb(port->interrupt_in_urb);
            if (port->interrupt_in_buffer)
                free(port->interrupt_in_buffer);

            port_table[id] = NULL;
            wwan->port[i] = NULL;
            free(port);
        }

        mutex_unlock(&port_lock[id]);

        printf("USB WWAN device disconnect [%d]\n", id);
    }

    mutex_lock(&wwan_lock);
    wwan->dev = NULL;
    usb_set_intfdata(intf, NULL);

    usb_driver_release_interface(&wwan_driver, wwan->intf);

    if (!wwan->opened_port)
        free(wwan);
    mutex_unlock(&wwan_lock);

    update_devices_bit();
}

static struct usb_driver wwan_driver = {
    .name = "usb_wwan",
    .probe = wwan_probe,
    .disconnect = wwan_remove,
    .id_table = wwan_ids,
};

void usb_wwan_driver_register(void)
{
    int i;

    mutex_init(&wwan_lock);

    for (i = 0; i < WWAN_MINORS; i++) {
        mutex_init(&port_lock[i]);
        thread_cond_init(&free_cond[i]);
    }

    usb_register_driver(&wwan_driver);
}

void usb_wwan_driver_deregister(void)
{
    usb_deregister(&wwan_driver);
}
