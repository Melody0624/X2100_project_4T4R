#include <common.h>
#include <malloc.h>
#include <os.h>
#include <driver/cache.h>
#include <usb/gadget_display.h>

#include "../composite.h"
#include "../../usb_lock.h"

struct display_dev {
    spinlock_t      lock;		/* lock this structure */
    struct usb_gadget    *gadget;
    s8            interface;
    struct usb_ep       *out_ep;

    struct list_head    rx_reqs;    /* List of free RX structs */
    struct list_head    rx_reqs_active;    /* List of Active RX xfers */
    struct list_head    rx_buffers;    /* List of completed xfers */
    /* wait until there is data to be read. */
    thread_waiter_t    rx_wait;

    struct usb_function    function;

    u8            display_status;
    u8            backlight;

    uint32_t        ops_busy;
    uint8_t        exit_flag;
    thread_waiter_t    exit_wait;

    struct display_edid *edid;
    struct display_specific_des *specific_des;

    u8 interface_id;

    uint8_t connect_flag;
    connect_callback_t connect_cb;
    thread_waiter_t connect_wait;
};

static struct display_dev *my_display;

static inline struct display_dev *func_to_display(struct usb_function *f)
{
    return container_of(f, struct display_dev, function);
}

/*-------------------------------------------------------------------------*/
#define DISPLAY_SPECIFIC_DES  (USB_TYPE_VENDOR | 0x01F)
#define DISPLAY_REQ_GET_STATUS      0x01
#define DISPLAY_REQ_GET_EDID        0x02
#define DISPLAY_REQ_SOFT_RESET      0x03
#define DISPLAY_REQ_GET_BACKLIGHT   0x04

#define Q_LEN                   3
#define USB_BUFSIZE             (128*1024)

static struct usb_interface_descriptor intf_desc = {
    .bLength =        sizeof(intf_desc),
    .bDescriptorType =    USB_DT_INTERFACE,
    .bNumEndpoints =    1,
    .bInterfaceClass =    USB_CLASS_VENDOR_SPEC,
    .bInterfaceSubClass =    USB_SUBCLASS_VENDOR_SPEC,
    .bInterfaceProtocol =    0,
    .iInterface =        0
};

static struct usb_endpoint_descriptor fs_ep_out_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,
    .bEndpointAddress =    USB_DIR_OUT,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK
};

static struct usb_descriptor_header *fs_bulk_function[] = {
    (struct usb_descriptor_header *) &intf_desc,
    NULL, // (struct usb_descriptor_header *) &specific_descriptor,
    (struct usb_descriptor_header *) &fs_ep_out_desc,
    NULL
};

/*
 * usb 2.0 devices need to expose both high speed and full speed
 * descriptors, unless they only run at full speed.
 */

static struct usb_endpoint_descriptor hs_ep_out_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,
    .bEndpointAddress =    USB_DIR_OUT,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =    512
};

static struct usb_descriptor_header *hs_bulk_function[] = {
    (struct usb_descriptor_header *) &intf_desc,
    NULL, // (struct usb_descriptor_header *) &specific_descriptor,
    (struct usb_descriptor_header *) &hs_ep_out_desc,
    NULL
};


/* maxpacket and other transfer characteristics vary by speed. */
static inline struct usb_endpoint_descriptor *ep_desc(struct usb_gadget *gadget,
                    struct usb_endpoint_descriptor *fs,
                    struct usb_endpoint_descriptor *hs)
{
    switch (gadget->speed) {
    case USB_SPEED_HIGH:
        return hs;
    default:
        return fs;
    }
}

/*-------------------------------------------------------------------------*/

static struct usb_request *display_req_alloc(struct usb_ep *ep, unsigned len)
{
    struct usb_request    *req;

    req = usb_ep_alloc_request(ep);

    if (req != NULL) {
        req->length = len;
        req->buf = cache_align_malloc(len);
        if (req->buf == NULL) {
            usb_ep_free_request(ep, req);
            return NULL;
        }
    }

    return req;
}

static void display_req_free(struct usb_ep *ep, struct usb_request *req)
{
    if (ep != NULL && req != NULL) {
        free(req->buf);
        usb_ep_free_request(ep, req);
    }
}

/*-------------------------------------------------------------------------*/

static void rx_complete(struct usb_ep *ep, struct usb_request *req)
{
    struct display_dev    *dev = ep->driver_data;
    int            status = req->status;
    unsigned long  flags;

    usb_spin_lock_irqsave(&dev->lock, flags);

    list_del_init(&req->list);    /* Remode from Active List */

    switch (status) {

    /* normal completion */
    case 0:
        list_add_tail(&req->list, &dev->rx_buffers);
        thread_waiter_wakeup(&dev->rx_wait);
        break;

    /* software-driven interface shutdown */
    case -ECONNRESET:        /* unlink */
    case -ESHUTDOWN:        /* disconnect etc */
        printf("gadget display rx shutdown, code %d\n", status);
        list_add(&req->list, &dev->rx_reqs);
        break;

    /* for hardware automagic (such as pxa) */
    case -ECONNABORTED:        /* endpoint reset */
        printf("gadget display rx %s reset\n", ep->name);
        list_add(&req->list, &dev->rx_reqs);
        break;

    /* data overrun */
    case -EOVERFLOW:

    default:
        printf("gadget display rx status %d\n", status);
        list_add(&req->list, &dev->rx_reqs);
        break;
    }

    usb_spin_unlock_irqrestore(&dev->lock, flags);
}


/*-------------------------------------------------------------------------*/

/* This function must be called with interrupts turned off. */
static void setup_rx_reqs(struct display_dev *dev)
{
    struct usb_request              *req;

    while (!list_empty(&dev->rx_reqs)) {
        int error;

        req = list_entry(dev->rx_reqs.next, struct usb_request, list);
        list_del_init(&req->list);

        /* The USB Host sends us whatever amount of data it wants to
         * so we always set the length field to the full USB_BUFSIZE.
         * If the amount of data is more than the read() caller asked
         * for it will be stored in the request buffer until it is
         * asked for by read().
         */
        req->length = USB_BUFSIZE;
        req->complete = rx_complete;

        /* here, we unlock, and only unlock, to avoid deadlock. */
        error = usb_ep_queue(dev->out_ep, req);
        if (error) {
            printf("%s: rx submit err --> %d\n", __func__, error);
            list_add(&req->list, &dev->rx_reqs);
            break;
        }
        /* if the req is empty, then add it into dev->rx_reqs_active. */
        else if (list_empty(&req->list))
            list_add(&req->list, &dev->rx_reqs_active);
    }
}

static int
set_display_interface(struct display_dev *dev)
{
    int            result = 0;

    dev->out_ep->desc = ep_desc(dev->gadget, &fs_ep_out_desc, &hs_ep_out_desc);
    dev->out_ep->driver_data = dev;

    result = usb_ep_enable(dev->out_ep);
    if (result != 0) {
        printf("gadget generic bulk  enable %s --> %d\n", dev->out_ep->name, result);
        goto done;
    }

    setup_rx_reqs(dev);

done:
    /* on error, disable any endpoints  */
    if (result != 0) {
        usb_ep_disable(dev->out_ep);
        dev->out_ep->desc = NULL;
    } else {
        dev->connect_flag = 1;
        if (dev->connect_cb)
            dev->connect_cb(1);

        thread_waiter_wakeup(&dev->connect_wait);
    }

    /* caller is responsible for cleanup on error */
    return result;
}

static void display_reset_interface(struct display_dev *dev)
{
    unsigned long   flags;

    if (dev->interface < 0)
        return;

    if (dev->out_ep->desc)
        usb_ep_disable(dev->out_ep);

    usb_spin_lock_irqsave(&dev->lock, flags);
    dev->out_ep->desc = NULL;
    dev->interface = -1;
    dev->connect_flag = 0;
    usb_spin_unlock_irqrestore(&dev->lock, flags);

    if (dev->connect_cb)
        dev->connect_cb(0);

    thread_waiter_wakeup(&dev->rx_wait);

}

/* Change our operational Interface. */
static int set_interface(struct display_dev *dev, unsigned number)
{
    int            result = 0;

    /* Free the current interface */
    display_reset_interface(dev);

    result = set_display_interface(dev);
    if (result)
        display_reset_interface(dev);
    else
        dev->interface = number;

    return result;
}

static void display_soft_reset(struct display_dev *dev)
{
    struct usb_request	*req;

    usb_ep_disable(dev->out_ep);

    while (likely(!(list_empty(&dev->rx_buffers)))) {
        req = container_of(dev->rx_buffers.next, struct usb_request,
                list);
        list_del_init(&req->list);
        list_add(&req->list, &dev->rx_reqs);
    }

    while (likely(!(list_empty(&dev->rx_reqs_active)))) {
        req = container_of(dev->rx_reqs_active.next, struct usb_request,
                list);
        list_del_init(&req->list);
        list_add(&req->list, &dev->rx_reqs);
    }

    usb_ep_enable(dev->out_ep);

    setup_rx_reqs(my_display);
}

static bool display_req_match(struct usb_function *f,
                   const struct usb_ctrlrequest *ctrl,
                   bool config0)
{
    struct display_dev *dev = func_to_display(f);

    if (config0 || (ctrl->wIndex != dev->interface_id))
        return false;

    switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
        case ((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) << 8 | USB_REQ_GET_DESCRIPTOR):
            switch (ctrl->wValue >> 8) {
                case DISPLAY_SPECIFIC_DES:
                    return true;
            }
            break;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_GET_STATUS):
            return true;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_GET_EDID):
            return true;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_SOFT_RESET):
            return true;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_GET_BACKLIGHT):
            return true;

        default:
            return false;
    }

    return false;
}

/*
 * The setup() callback implements all the ep0 functionality that's not
 * handled lower down.
 */
static int display_func_setup(struct usb_function *f,
        const struct usb_ctrlrequest *ctrl)
{
    struct display_dev *dev = func_to_display(f);
    struct usb_composite_dev *cdev = f->config->cdev;
    struct usb_request    *req = cdev->req;
    u8  *buf = req->buf;
    int offset;
    int len = -EOPNOTSUPP;

    printf("%s: ctrl req%02x.%02x v%04x i%04x l%d\n", __func__,
        ctrl->bRequestType, ctrl->bRequest, ctrl->wValue, ctrl->wIndex, ctrl->wLength);

    switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
        case ((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) << 8 | USB_REQ_GET_DESCRIPTOR):
            switch (ctrl->wValue >> 8) {
                case DISPLAY_SPECIFIC_DES:
                    len = min_t(unsigned short, ctrl->wLength, dev->specific_des->size);
                    memcpy(buf, dev->specific_des->buf, len);
                    break;

                default:
                    printf("%s %d: Unknown descriptor request\n", __func__, __LINE__);
                    break;
            }
            break;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_GET_STATUS):
            buf[0] = dev->display_status;
            len = min_t(unsigned short, ctrl->wLength, 1);
            break;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_GET_EDID):
            offset = ctrl->wValue;
            if (offset < dev->edid->size)
                len = min_t(unsigned short, ctrl->wLength, dev->edid->size - offset);
            else
                len = 0;

            memcpy(buf, &dev->edid->buf[offset], len);
            if (len < ctrl->wLength) {
                memset(buf + len, 0, ctrl->wLength - len);
                len = ctrl->wLength;
            }
            break;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_SOFT_RESET):
            display_soft_reset(dev);
            len = 0;
            break;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_GET_BACKLIGHT):
            buf[0] = dev->backlight;
            len = min_t(unsigned short, ctrl->wLength, 1);
            break;

        default:
            printf("%s %d: Unknown descriptor request\n", __func__, __LINE__);
            break;
    }

    if (len >= 0) {
        req->zero = 0;
        req->length = len;
        len = usb_ep_queue(cdev->gadget->ep0, req);
        if (len < 0) {
            printf("%s:%d Error!\n", __func__, __LINE__);
            req->status = 0;
        }
    }
    return len;
}

static int display_func_bind(struct usb_configuration *c,
        struct usb_function *f)
{
    struct usb_gadget *gadget = c->cdev->gadget;
    struct display_dev *dev = func_to_display(f);
    struct usb_composite_dev *cdev = c->cdev;
    struct usb_ep *out_ep = NULL;
    struct usb_request *req;
    int id;
    int ret;
    u32 i;

    id = usb_interface_id(c, f);
    if (id < 0)
        return id;
    intf_desc.bInterfaceNumber = id;
    dev->interface_id = id;

    /* finish hookup to lower layer ... */
    dev->gadget = gadget;

    /* all we really need is bulk IN/OUT */
    out_ep = usb_ep_autoconfig(cdev->gadget, &fs_ep_out_desc);
    if (!out_ep)
        panic("%s %d: can't autoconfigure on %s\n", __func__, __LINE__, cdev->gadget->name);


    /* assumes that all endpoints are dual-speed */
    hs_ep_out_desc.bEndpointAddress = fs_ep_out_desc.bEndpointAddress;

    fs_bulk_function[1] = (struct usb_descriptor_header *)dev->specific_des->buf;
    hs_bulk_function[1] = (struct usb_descriptor_header *)dev->specific_des->buf;
    ret = usb_assign_descriptors(f, fs_bulk_function, hs_bulk_function);
    if (ret)
        return ret;

    dev->out_ep = out_ep;

    ret = -ENOMEM;
    for (i = 0; i < Q_LEN; i++) {
        req = display_req_alloc(dev->out_ep, USB_BUFSIZE);
        if (!req)
            goto fail_rx_reqs;
        list_add(&req->list, &dev->rx_reqs);
    }

    return 0;

fail_rx_reqs:
    while (!list_empty(&dev->rx_reqs)) {
        req = list_entry(dev->rx_reqs.next, struct usb_request, list);
        list_del(&req->list);
        display_req_free(dev->out_ep, req);
    }

    usb_free_all_descriptors(f);
    return ret;
}

static int display_func_set_alt(struct usb_function *f,
        unsigned intf, unsigned alt)
{
    struct display_dev *dev = func_to_display(f);
    int ret = -EINVAL;

    printf("%s: alt %d\n", __func__, alt);

    if (!alt)
        ret = set_interface(dev, intf);

    return ret;
}

static void display_func_disable(struct usb_function *f)
{
    struct display_dev *dev = func_to_display(f);

    display_reset_interface(dev);
}


static void display_func_unbind(struct usb_configuration *c,
        struct usb_function *f)
{
    struct display_dev    *dev;
    struct usb_request    *req;

    dev = func_to_display(f);

    /* we must already have been disconnected ... no i/o may be active */
    if (!list_empty(&dev->rx_reqs_active))
        printf("%s: error !!!\n", __func__);

    while (!list_empty(&dev->rx_reqs)) {
        req = list_entry(dev->rx_reqs.next,
                struct usb_request, list);
        list_del(&req->list);
        display_req_free(dev->out_ep, req);
    }

    while (!list_empty(&dev->rx_buffers)) {
        req = list_entry(dev->rx_buffers.next,
                struct usb_request, list);
        list_del(&req->list);
        display_req_free(dev->out_ep, req);
    }

    usb_free_all_descriptors(f);
}

void display_device_free(void)
{
    os_enter_critical();
    assert(my_display);
    assert(!my_display->exit_flag);
    my_display->exit_flag = 1;
    while (my_display->ops_busy) {
        thread_waiter_wakeup(&my_display->rx_wait);
        thread_waiter_wakeup(&my_display->connect_wait);
        os_exit_critical();
        thread_waiter_wait(&my_display->exit_wait);
        os_enter_critical();
    }
    free(my_display);
    my_display = NULL;
    os_exit_critical();
}


struct usb_function *display_device_alloc(struct display_edid *edid, struct display_specific_des *des, connect_callback_t connect_cb)
{
    os_enter_critical();

    assert(my_display == NULL);

    my_display = malloc(sizeof(*my_display));
    if (my_display == NULL) {
        os_exit_critical();
        printf("%s: out of memory\n", __func__);
        return ERR_PTR(-ENOMEM);
    }
    memset(my_display, 0, sizeof(*my_display));

    my_display->edid = edid;
    my_display->specific_des = des;
    my_display->connect_cb = connect_cb;

    my_display->function.name = "display";
    my_display->function.bind = display_func_bind;
    my_display->function.setup = display_func_setup;
    my_display->function.unbind = display_func_unbind;
    my_display->function.set_alt = display_func_set_alt;
    my_display->function.disable = display_func_disable;
    my_display->function.req_match = display_req_match;

    INIT_LIST_HEAD(&my_display->rx_reqs);
    INIT_LIST_HEAD(&my_display->rx_buffers);
    INIT_LIST_HEAD(&my_display->rx_reqs_active);

    spin_lock_init_recursive(&my_display->lock);
    thread_waiter_init(&my_display->rx_wait);
    thread_waiter_init(&my_display->connect_wait);
    thread_waiter_init(&my_display->exit_wait);

    my_display->interface = -1;
    os_exit_critical();

    return &my_display->function;
}

/*-------------------------------------------------------------------------*/

static int check_set_busy(void)
{
    os_enter_critical();

    if (my_display == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    my_display->ops_busy++;

    os_exit_critical();
    return 0;
}

static void set_no_busy(void)
{
    os_enter_critical();

    if (my_display) {
        my_display->ops_busy--;
        if (my_display->exit_flag)
            thread_waiter_wakeup(&my_display->exit_wait);
    }

    os_exit_critical();
}

int gadget_display_get_buffer(struct display_buffer *display_buffer, uint32_t timeout_ms)
{
    int ret = 0;
    unsigned long               flags;
    struct usb_request        *req;

    if (!display_buffer)
        return -EINVAL;

    if (check_set_busy())
        return -ENODEV;

    usb_spin_lock_irqsave(&my_display->lock, flags);

    if (!my_display->connect_flag) {
        ret = -ENOLINK;
        goto out;
    }

    while (list_empty(&my_display->rx_buffers)) {
        if (!timeout_ms) {
            ret = -EAGAIN;
            goto out;
        }

        /* Sleep until data is available */
        usb_spin_unlock_irqrestore(&my_display->lock, flags);
        ret = thread_waiter_wait_timeout(&my_display->rx_wait, timeout_ms);
        usb_spin_lock_irqsave(&my_display->lock, flags);
        if (my_display->exit_flag) {
            ret = -ENODEV;
            goto out;
        }

        if (!my_display->connect_flag) {
            ret = -ENOLINK;
            goto out;
        }

        if (ret) {
            ret = -ETIMEDOUT;
            goto out;
        }
    }

    req = list_entry(my_display->rx_buffers.next, struct usb_request, list);
    list_del_init(&req->list);

    display_buffer->length = req->length;
    display_buffer->actual = req->actual;
    display_buffer->buf = req->buf;
    display_buffer->private_data = req;

out:
    usb_spin_unlock_irqrestore(&my_display->lock, flags);
    set_no_busy();
    return ret;
}

int gadget_display_put_buffer(struct display_buffer *display_buffer)
{
    struct usb_request        *req;

    if (!display_buffer || !display_buffer->private_data)
        return -EINVAL;

    os_enter_critical();

    if (my_display == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    req = display_buffer->private_data;
    list_add(&req->list, &my_display->rx_reqs);

    if (my_display->connect_flag)
        setup_rx_reqs(my_display);

    display_buffer->length = 0;
    display_buffer->actual = 0;
    display_buffer->buf = NULL;
    display_buffer->private_data = NULL;

    os_exit_critical();

    return 0;
}

int gadget_display_get_connect_status(void)
{
    int status;

    os_enter_critical();

    if (my_display == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    if (my_display->connect_flag)
        status = 1;
    else
        status = 0;

    os_exit_critical();

    return status;
}

int gadget_display_wait_connect(uint32_t timeout_ms)
{
    int ret = 0;

    if (check_set_busy())
        return -ENODEV;

    while (!my_display->connect_flag) {
        ret = thread_waiter_wait_timeout(&my_display->connect_wait, timeout_ms);

        if (my_display->exit_flag) {
            ret = -ENODEV;
            goto out;
        }

        if (ret) {
            ret = -ETIMEDOUT;
            goto out;
        }
    }

out:
    set_no_busy();
    return ret;
}

int gadget_display_get_status(u8 *status)
{
    assert(status);

    os_enter_critical();

    if (my_display == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    *status = my_display->display_status;

    os_exit_critical();

    return 0;
}

int gadget_display_set_status(u8 status)
{
    os_enter_critical();

    if (my_display == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    my_display->display_status = status;

    os_exit_critical();

    return 0;
}

int gadget_display_get_backlight(u8 *value)
{
    assert(value);

    os_enter_critical();

    if (my_display == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    *value = my_display->backlight;

    os_exit_critical();

    return 0;
}

int gadget_display_set_backlight(u8 value)
{
    os_enter_critical();

    if (my_display == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    my_display->backlight = value;

    os_exit_critical();

    return 0;
}