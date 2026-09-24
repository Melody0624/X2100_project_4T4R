#include <common.h>
#include <malloc.h>
#include <os.h>
#include <driver/cache.h>
#include <usb/gadget_printer.h>

#include "../composite.h"
#include "../../usb_lock.h"

/* cache data buffer size */
#define USB_BUFSIZE            8192
/* cache data buffer count */
#define Q_LEN            10

/* printer setup */
#define GET_DEVICE_ID        0
#define GET_PORT_STATUS        1
#define SOFT_RESET        2

/*-------------------------------------------------------------------------*/

struct printer_dev {
    spinlock_t        lock;        /* lock this structure */

    struct usb_gadget    *gadget;
    s8            interface;
    struct usb_ep        *in_ep, *out_ep;

    struct list_head    rx_reqs;    /* List of free RX structs */
    struct list_head    rx_reqs_active;    /* List of Active RX xfers */
    struct list_head    rx_buffers;    /* List of completed xfers */
    /* wait until there is data to be read. */
    thread_waiter_t    rx_wait;
    struct list_head    tx_reqs;    /* List of free TX structs */
    struct list_head    tx_reqs_active; /* List of Active TX xfers */
    /* Wait until there are write buffers available to use. */
    thread_waiter_t    tx_wait;
    /* Wait until all write buffers have been sent. */
    thread_waiter_t    tx_flush_wait;

    struct usb_request    *current_rx_req;
    size_t            current_rx_bytes;
    u8            *current_rx_buf;

    u8            printer_status;

    const char            *pnp_string;
    struct usb_function    function;

    uint32_t        ops_busy;
    uint8_t        exit_flag;
    thread_waiter_t    exit_wait;

    uint8_t connect_flag;
    connect_callback_t connect_cb;
    thread_waiter_t connect_wait;
};

static struct printer_dev *my_printer;

static inline struct printer_dev *func_to_printer(struct usb_function *f)
{
    return container_of(f, struct printer_dev, function);
}

/*-------------------------------------------------------------------------*/

static struct usb_interface_descriptor intf_desc = {
    .bLength =        sizeof(intf_desc),
    .bDescriptorType =    USB_DT_INTERFACE,
    .bNumEndpoints =    2,
    .bInterfaceClass =    USB_CLASS_PRINTER,
    .bInterfaceSubClass =    1,    /* Printer Sub-Class */
    .bInterfaceProtocol =    2,    /* Bi-Directional */
    .iInterface =        0
};

static struct usb_endpoint_descriptor fs_ep_in_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,
    .bEndpointAddress =    USB_DIR_IN,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK
};

static struct usb_endpoint_descriptor fs_ep_out_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,
    .bEndpointAddress =    USB_DIR_OUT,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK
};

static struct usb_descriptor_header *fs_printer_function[] = {
    (struct usb_descriptor_header *) &intf_desc,
    (struct usb_descriptor_header *) &fs_ep_in_desc,
    (struct usb_descriptor_header *) &fs_ep_out_desc,
    NULL
};

/*
 * usb 2.0 devices need to expose both high speed and full speed
 * descriptors, unless they only run at full speed.
 */

static struct usb_endpoint_descriptor hs_ep_in_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =    512
};

static struct usb_endpoint_descriptor hs_ep_out_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =    512
};

static struct usb_descriptor_header *hs_printer_function[] = {
    (struct usb_descriptor_header *) &intf_desc,
    (struct usb_descriptor_header *) &hs_ep_in_desc,
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

static struct usb_request *printer_req_alloc(struct usb_ep *ep, unsigned len)
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

static void printer_req_free(struct usb_ep *ep, struct usb_request *req)
{
    if (ep != NULL && req != NULL) {
        free(req->buf);
        usb_ep_free_request(ep, req);
    }
}

/*-------------------------------------------------------------------------*/

static void rx_complete(struct usb_ep *ep, struct usb_request *req)
{
    struct printer_dev    *dev = ep->driver_data;
    int            status = req->status;
    unsigned long        flags;

    usb_spin_lock_irqsave(&dev->lock, flags);

    list_del_init(&req->list);    /* Remode from Active List */

    switch (status) {

    /* normal completion */
    case 0:
        if (req->actual > 0) {
            list_add_tail(&req->list, &dev->rx_buffers);
            thread_waiter_wakeup(&dev->rx_wait);
        } else {
            list_add(&req->list, &dev->rx_reqs);
        }
        break;

    /* software-driven interface shutdown */
    case -ECONNRESET:        /* unlink */
    case -ESHUTDOWN:        /* disconnect etc */
        printf("gadget printer rx shutdown, code %d\n", status);
        list_add(&req->list, &dev->rx_reqs);
        break;

    /* for hardware automagic (such as pxa) */
    case -ECONNABORTED:        /* endpoint reset */
        printf("gadget printer rx %s reset\n", ep->name);
        list_add(&req->list, &dev->rx_reqs);
        break;

    /* data overrun */
    case -EOVERFLOW:
        // fallthrough;
    default:
        printf("gadget printer rx status %d\n", status);
        list_add(&req->list, &dev->rx_reqs);
        break;
    }

    usb_spin_unlock_irqrestore(&dev->lock, flags);
}

static void tx_complete(struct usb_ep *ep, struct usb_request *req)
{
    unsigned long flags;
    struct printer_dev    *dev = ep->driver_data;

    switch (req->status) {
    case 0:
        break;
    case -ECONNRESET:        /* unlink */
    case -ESHUTDOWN:        /* disconnect etc */
        break;
    default:
        printf("gadget printer tx err %d\n", req->status);
    }

    usb_spin_lock_irqsave(&dev->lock, flags);
    /* Take the request struct off the active list and put it on the
     * free list.
     */
    list_del_init(&req->list);
    list_add(&req->list, &dev->tx_reqs);
    thread_waiter_wakeup(&dev->tx_wait);
    if (likely(list_empty(&dev->tx_reqs_active)))
        thread_waiter_wakeup(&dev->tx_flush_wait);

    usb_spin_unlock_irqrestore(&dev->lock, flags);
}

/*-------------------------------------------------------------------------*/

static int set_printer_interface(struct printer_dev *dev)
{
    int            result = 0;

    dev->in_ep->desc = ep_desc(dev->gadget, &fs_ep_in_desc, &hs_ep_in_desc);
    dev->in_ep->driver_data = dev;

    dev->out_ep->desc = ep_desc(dev->gadget, &fs_ep_out_desc, &hs_ep_out_desc);
    dev->out_ep->driver_data = dev;

    result = usb_ep_enable(dev->in_ep);
    if (result != 0) {
        printf("gadget printer enable %s err--> %d\n", dev->in_ep->name, result);
        goto done;
    }

    result = usb_ep_enable(dev->out_ep);
    if (result != 0) {
        printf("gadget printer enable %s --> %d\n", dev->out_ep->name, result);
        goto done;
    }

done:
    /* on error, disable any endpoints  */
    if (result != 0) {
        (void) usb_ep_disable(dev->in_ep);
        (void) usb_ep_disable(dev->out_ep);
        dev->in_ep->desc = NULL;
        dev->out_ep->desc = NULL;
    } else {
        dev->connect_flag = 1;
        if (dev->connect_cb)
            dev->connect_cb(1);

        thread_waiter_wakeup(&dev->connect_wait);
        thread_waiter_wakeup(&dev->tx_wait);
    }

    /* caller is responsible for cleanup on error */
    return result;
}

static void printer_reset_interface(struct printer_dev *dev)
{
    unsigned long flags;

    if (dev->interface < 0)
        return;

    if (dev->in_ep->desc)
        usb_ep_disable(dev->in_ep);

    if (dev->out_ep->desc)
        usb_ep_disable(dev->out_ep);

    usb_spin_lock_irqsave(&dev->lock, flags);
    dev->in_ep->desc = NULL;
    dev->out_ep->desc = NULL;
    dev->interface = -1;
    dev->connect_flag = 0;
    usb_spin_unlock_irqrestore(&dev->lock, flags);

    if (dev->connect_cb)
        dev->connect_cb(0);

    thread_waiter_wakeup(&dev->rx_wait);
    thread_waiter_wakeup(&dev->tx_wait);
    thread_waiter_wakeup(&dev->tx_flush_wait);
}

/* Change our operational Interface. */
static int set_interface(struct printer_dev *dev, unsigned number)
{
    int            result = 0;

    /* Free the current interface */
    printer_reset_interface(dev);

    result = set_printer_interface(dev);
    if (result)
        printer_reset_interface(dev);
    else
        dev->interface = number;

    return result;
}

static void printer_soft_reset(struct printer_dev *dev)
{
    unsigned long flags;
    struct usb_request    *req;

    printf("Received Printer Reset Request\n");

    if (usb_ep_disable(dev->in_ep))
        printf("gadget printer failed to disable USB in_ep\n");
    if (usb_ep_disable(dev->out_ep))
        printf("Failed to disable USB out_ep\n");

    usb_spin_lock_irqsave(&dev->lock, flags);

    if (dev->current_rx_req != NULL) {
        list_add(&dev->current_rx_req->list, &dev->rx_reqs);
        dev->current_rx_req = NULL;
    }
    dev->current_rx_bytes = 0;
    dev->current_rx_buf = NULL;

    while (likely(!(list_empty(&dev->rx_buffers)))) {
        req = container_of(dev->rx_buffers.next, struct usb_request, list);
        list_del_init(&req->list);
        list_add(&req->list, &dev->rx_reqs);
    }

    while (likely(!(list_empty(&dev->rx_reqs_active)))) {
        req = container_of(dev->rx_reqs_active.next, struct usb_request, list);
        list_del_init(&req->list);
        list_add(&req->list, &dev->rx_reqs);
    }

    while (likely(!(list_empty(&dev->tx_reqs_active)))) {
        req = container_of(dev->tx_reqs_active.next, struct usb_request, list);
        list_del_init(&req->list);
        list_add(&req->list, &dev->tx_reqs);
    }

    usb_spin_unlock_irqrestore(&dev->lock, flags);

    if (usb_ep_enable(dev->in_ep))
       printf("gadget printer failed to enable USB in_ep\n");
    if (usb_ep_enable(dev->out_ep))
        printf("gadget printer failed to enable USB out_ep\n");

    thread_waiter_wakeup(&dev->rx_wait);
    thread_waiter_wakeup(&dev->tx_wait);
    thread_waiter_wakeup(&dev->tx_flush_wait);
}

/*-------------------------------------------------------------------------*/

static bool gprinter_req_match(struct usb_function *f,
                   const struct usb_ctrlrequest *ctrl,
                   bool config0)
{
    struct printer_dev    *dev = func_to_printer(f);
    u16            w_index = ctrl->wIndex;
    u16            w_value = ctrl->wValue;
    u16            w_length = ctrl->wLength;

    if (config0)
        return false;

    if ((ctrl->bRequestType & USB_RECIP_MASK) != USB_RECIP_INTERFACE ||
        (ctrl->bRequestType & USB_TYPE_MASK) != USB_TYPE_CLASS)
        return false;

    switch (ctrl->bRequest) {
    case GET_DEVICE_ID:
        w_index >>= 8;
        if (USB_DIR_IN & ctrl->bRequestType)
            break;
        return false;
    case GET_PORT_STATUS:
        if (!w_value && w_length == 1 &&
            (USB_DIR_IN & ctrl->bRequestType))
            break;
        return false;
    case SOFT_RESET:
        if (!w_value && !w_length &&
           !(USB_DIR_IN & ctrl->bRequestType))
            break;
        // fallthrough;
    default:
        return false;
    }
    return w_index == dev->interface;
}

/*
 * The setup() callback implements all the ep0 functionality that's not
 * handled lower down.
 */
static int printer_func_setup(struct usb_function *f,
        const struct usb_ctrlrequest *ctrl)
{
    struct printer_dev *dev = func_to_printer(f);
    struct usb_composite_dev *cdev = f->config->cdev;
    struct usb_request    *req = cdev->req;
    u8            *buf = req->buf;
    int            value = -EOPNOTSUPP;
    u16            wIndex = ctrl->wIndex;
    u16            wValue = ctrl->wValue;
    u16            wLength = ctrl->wLength;

    switch (ctrl->bRequestType & USB_TYPE_MASK) {
    case USB_TYPE_CLASS:
        switch (ctrl->bRequest) {
        case GET_DEVICE_ID: /* Get the IEEE-1284 PNP String */
            /* Only one printer interface is supported. */
            if ((wIndex >> 8) != dev->interface)
                break;

            if (!dev->pnp_string) {
                value = 0;
                break;
            }
            value = strlen(dev->pnp_string);
            buf[0] = (value >> 8) & 0xFF;
            buf[1] = value & 0xFF;
            memcpy(buf + 2, dev->pnp_string, value);
            break;

        case GET_PORT_STATUS: /* Get Port Status */
            /* Only one printer interface is supported. */
            if (wIndex != dev->interface)
                break;

            buf[0] = dev->printer_status;
            value = min_t(u16, wLength, 1);
            break;

        case SOFT_RESET: /* Soft Reset */
            /* Only one printer interface is supported. */
            if (wIndex != dev->interface)
                break;

            printer_soft_reset(dev);

            value = 0;
            break;

        default:
            goto unknown;
        }
        break;

    default:
unknown:
        printf("gadget printer unknown ctrl req%02x.%02x v%04x i%04x l%d\n",
            ctrl->bRequestType, ctrl->bRequest, wValue, wIndex, wLength);
        break;
    }
    /* host either stalls (value < 0) or reports success */
    if (value >= 0) {
        req->length = value;
        req->zero = value < wLength;
        value = usb_ep_queue(cdev->gadget->ep0, req);
        if (value < 0) {
            printf("%s:%d Error!\n", __func__, __LINE__);
            req->status = 0;
        }
    }
    return value;
}

static int printer_func_bind(struct usb_configuration *c,
        struct usb_function *f)
{
    struct usb_gadget *gadget = c->cdev->gadget;
    struct printer_dev *dev = func_to_printer(f);
    struct usb_composite_dev *cdev = c->cdev;
    struct usb_ep *in_ep;
    struct usb_ep *out_ep = NULL;
    struct usb_request *req;
    int id;
    int ret;
    u32 i;

    id = usb_interface_id(c, f);
    if (id < 0)
        return id;
    intf_desc.bInterfaceNumber = id;

    /* finish hookup to lower layer ... */
    dev->gadget = gadget;

    /* all we really need is bulk IN/OUT */
    in_ep = usb_ep_autoconfig(cdev->gadget, &fs_ep_in_desc);
    if (!in_ep) {
autoconf_fail:
        printf("%s: can't autoconfigure on %s\n", __func__, cdev->gadget->name);
        return -ENODEV;
    }

    out_ep = usb_ep_autoconfig(cdev->gadget, &fs_ep_out_desc);
    if (!out_ep)
        goto autoconf_fail;

    /* assumes that all endpoints are dual-speed */
    hs_ep_in_desc.bEndpointAddress = fs_ep_in_desc.bEndpointAddress;
    hs_ep_out_desc.bEndpointAddress = fs_ep_out_desc.bEndpointAddress;

    ret = usb_assign_descriptors(f, fs_printer_function, hs_printer_function);
    if (ret)
        return ret;

    dev->in_ep = in_ep;
    dev->out_ep = out_ep;

    ret = -ENOMEM;
    for (i = 0; i < Q_LEN; i++) {
        req = printer_req_alloc(dev->in_ep, USB_BUFSIZE);
        if (!req)
            goto fail_tx_reqs;
        list_add(&req->list, &dev->tx_reqs);
    }

    for (i = 0; i < Q_LEN; i++) {
        req = printer_req_alloc(dev->out_ep, USB_BUFSIZE);
        if (!req)
            goto fail_rx_reqs;
        list_add(&req->list, &dev->rx_reqs);
    }

    return 0;

fail_rx_reqs:
    while (!list_empty(&dev->rx_reqs)) {
        req = container_of(dev->rx_reqs.next, struct usb_request, list);
        list_del(&req->list);
        printer_req_free(dev->out_ep, req);
    }

fail_tx_reqs:
    while (!list_empty(&dev->tx_reqs)) {
        req = container_of(dev->tx_reqs.next, struct usb_request, list);
        list_del(&req->list);
        printer_req_free(dev->in_ep, req);
    }

    usb_free_all_descriptors(f);
    return ret;
}

static int printer_func_set_alt(struct usb_function *f,
        unsigned intf, unsigned alt)
{
    struct printer_dev *dev = func_to_printer(f);
    int ret = -EINVAL;

    if (!alt)
        ret = set_interface(dev, intf);

    return ret;
}

static void printer_func_disable(struct usb_function *f)
{
    struct printer_dev *dev = func_to_printer(f);

    printer_reset_interface(dev);
}

static void printer_func_unbind(struct usb_configuration *c,
        struct usb_function *f)
{
    struct printer_dev    *dev;
    struct usb_request    *req;

    dev = func_to_printer(f);

    /* we must already have been disconnected ... no i/o may be active */
    if ((!list_empty(&dev->tx_reqs_active)) || (!list_empty(&dev->rx_reqs_active)))
        printf("%s: error !!!\n", __func__);

    /* Free all memory for this driver. */
    while (!list_empty(&dev->tx_reqs)) {
        req = container_of(dev->tx_reqs.next, struct usb_request, list);
        list_del(&req->list);
        printer_req_free(dev->in_ep, req);
    }

    if (dev->current_rx_req != NULL)
        printer_req_free(dev->out_ep, dev->current_rx_req);

    while (!list_empty(&dev->rx_reqs)) {
        req = container_of(dev->rx_reqs.next,
                struct usb_request, list);
        list_del(&req->list);
        printer_req_free(dev->out_ep, req);
    }

    while (!list_empty(&dev->rx_buffers)) {
        req = container_of(dev->rx_buffers.next,
                struct usb_request, list);
        list_del(&req->list);
        printer_req_free(dev->out_ep, req);
    }
    usb_free_all_descriptors(f);
}

void printer_device_free(void)
{
    os_enter_critical();
    assert(my_printer);
    assert(!my_printer->exit_flag);
    my_printer->exit_flag = 1;
    while (my_printer->ops_busy) {
        thread_waiter_wakeup(&my_printer->rx_wait);
        thread_waiter_wakeup(&my_printer->tx_wait);
        thread_waiter_wakeup(&my_printer->tx_flush_wait);
        thread_waiter_wakeup(&my_printer->connect_wait);
        os_exit_critical();
        thread_waiter_wait(&my_printer->exit_wait);
        os_enter_critical();
    }
    free(my_printer);
    my_printer = NULL;
    os_exit_critical();
}

struct usb_function *printer_device_alloc(connect_callback_t connect_cb, const char *pnp_string)
{
    os_enter_critical();

    my_printer = malloc(sizeof(*my_printer));
    if (my_printer == NULL) {
        os_exit_critical();
        printf("%s: out of memory\n", __func__);
        return ERR_PTR(-ENOMEM);
    }
    memset(my_printer, 0, sizeof(*my_printer));

    my_printer->pnp_string = pnp_string;
    my_printer->connect_cb = connect_cb;

    my_printer->function.name = "printer";
    my_printer->function.bind = printer_func_bind;
    my_printer->function.setup = printer_func_setup;
    my_printer->function.unbind = printer_func_unbind;
    my_printer->function.set_alt = printer_func_set_alt;
    my_printer->function.disable = printer_func_disable;
    my_printer->function.req_match = gprinter_req_match;

    INIT_LIST_HEAD(&my_printer->tx_reqs);
    INIT_LIST_HEAD(&my_printer->rx_reqs);
    INIT_LIST_HEAD(&my_printer->rx_buffers);
    INIT_LIST_HEAD(&my_printer->tx_reqs_active);
    INIT_LIST_HEAD(&my_printer->rx_reqs_active);

    spin_lock_init_recursive(&my_printer->lock);
    thread_waiter_init(&my_printer->rx_wait);
    thread_waiter_init(&my_printer->tx_wait);
    thread_waiter_init(&my_printer->tx_flush_wait);
    thread_waiter_init(&my_printer->connect_wait);
    thread_waiter_init(&my_printer->exit_wait);

    my_printer->interface = -1;
    my_printer->printer_status = PRINTER_STATUS_NOT_ERROR;
    my_printer->current_rx_req = NULL;
    my_printer->current_rx_bytes = 0;
    my_printer->current_rx_buf = NULL;
    os_exit_critical();

    return &my_printer->function;
}

/* ------------------------------------------------------------------------- */

/* This function must be called with interrupts turned off. */
static void setup_rx_reqs(struct printer_dev *dev)
{
    struct usb_request              *req;

    while (likely(!list_empty(&dev->rx_reqs))) {
        int error;

        req = container_of(dev->rx_reqs.next, struct usb_request, list);
        list_del_init(&req->list);

        /* The USB Host sends us whatever amount of data it wants to
         * so we always set the length field to the full USB_BUFSIZE.
         * If the amount of data is more than the read() caller asked
         * for it will be stored in the request buffer until it is
         * asked for by read().
         */
        req->length = USB_BUFSIZE;
        req->complete = rx_complete;

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

static int check_set_busy(void)
{
    os_enter_critical();

    if (my_printer == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    my_printer->ops_busy++;

    os_exit_critical();

    return 0;
}

static void set_no_busy(void)
{
    os_enter_critical();

    if (my_printer) {
        my_printer->ops_busy--;
        if (my_printer->exit_flag)
            thread_waiter_wakeup(&my_printer->exit_wait);
    }

    os_exit_critical();
}

int gadget_printer_read(uint8_t *buffer, uint32_t len, uint8_t block, uint32_t timeout_ms)
{
    int ret;
    unsigned long            flags;
    unsigned int                size;
    unsigned int                bytes_copied;
    struct usb_request        *req;
    /* This is a pointer to the current USB rx request. */
    struct usb_request        *current_rx_req;
    /* This is the number of bytes in the current rx buffer. */
    int                current_rx_bytes;
    /* This is a pointer to the current rx buffer. */
    u8                *current_rx_buf;

    if (!len || buffer == NULL)
        return 0;

    if (check_set_busy())
        return -ENODEV;

    usb_spin_lock_irqsave(&my_printer->lock, flags);

    if (!my_printer->connect_flag) {
        ret = -ENOLINK;
        goto out;
    }

    setup_rx_reqs(my_printer);

    bytes_copied = 0;
    current_rx_req = my_printer->current_rx_req;
    current_rx_bytes = my_printer->current_rx_bytes;
    current_rx_buf = my_printer->current_rx_buf;
    my_printer->current_rx_req = NULL;
    my_printer->current_rx_bytes = 0;
    my_printer->current_rx_buf = NULL;

    /* Check if there is any data in the read buffers. Please note that
     * current_rx_bytes is the number of bytes in the current rx buffer.
     * If it is zero then check if there are any other rx_buffers that
     * are on the completed list. We are only out of data if all rx
     * buffers are empty.
     */
    while ((current_rx_bytes == 0) && (list_empty(&my_printer->rx_buffers))) {
        /*
         * If no data is available check if this is a NON-Blocking
         * call or not.
         */
        if (!block) {
            ret = -EAGAIN;
            goto out;
        }

        /* Sleep until data is available */
        usb_spin_unlock_irqrestore(&my_printer->lock, flags);
        ret = thread_waiter_wait_timeout(&my_printer->rx_wait, timeout_ms);
        usb_spin_lock_irqsave(&my_printer->lock, flags);

        if (my_printer->exit_flag) {
            ret = -ENODEV;
            goto out;
        }

        if (!my_printer->connect_flag) {
            ret = -ENOLINK;
            goto out;
        }

        if (ret) {
            ret = -ETIMEDOUT;
            goto out;
        }
    }

    /* We have data to return then copy it to the caller's buffer.*/
    while ((current_rx_bytes || !list_empty(&my_printer->rx_buffers)) && len) {
        if (current_rx_bytes == 0) {
            req = list_entry(my_printer->rx_buffers.next, struct usb_request, list);
            list_del_init(&req->list);

            if (req->actual && req->buf) {
                current_rx_req = req;
                current_rx_bytes = req->actual;
                current_rx_buf = req->buf;
            } else {
                list_add(&req->list, &my_printer->rx_reqs);
                continue;
            }
        }

        if (len > current_rx_bytes)
            size = current_rx_bytes;
        else
            size = len;

        memcpy(buffer, current_rx_buf, size);
        bytes_copied += size;
        len -= size;
        buffer += size;

        /* If we not returning all the data left in this RX request
         * buffer then adjust the amount of data left in the buffer.
         * Othewise if we are done with this RX request buffer then
         * requeue it to get any incoming data from the USB host.
         */
        if (size < current_rx_bytes) {
            current_rx_bytes -= size;
            current_rx_buf += size;
        } else {
            list_add(&current_rx_req->list, &my_printer->rx_reqs);
            current_rx_bytes = 0;
            current_rx_buf = NULL;
            current_rx_req = NULL;
        }
    }

    my_printer->current_rx_req = current_rx_req;
    my_printer->current_rx_bytes = current_rx_bytes;
    my_printer->current_rx_buf = current_rx_buf;

    if (bytes_copied)
        ret = bytes_copied;
    else
        ret = -EAGAIN;

out:
    usb_spin_unlock_irqrestore(&my_printer->lock, flags);
    set_no_busy();
    return ret;
}

int gadget_printer_write(const uint8_t *buffer, uint32_t len, uint8_t block, uint32_t timeout_ms)
{
    int ret;
    unsigned long        flags;
    size_t            size;    /* Amount of data in a TX request. */
    size_t            bytes_copied = 0;
    struct usb_request    *req;
    int            value;

    if (!len || buffer == NULL)
        return 0;

    if (check_set_busy())
        return -ENODEV;

    usb_spin_lock_irqsave(&my_printer->lock, flags);

    if (!my_printer->connect_flag) {
        ret = -ENOLINK;
        goto out;
    }

    /* Check if there is any available write buffers */
    while (list_empty(&my_printer->tx_reqs)) {
        if (!block) {
            ret = -EAGAIN;
            goto out;
        }

        /* Sleep until a write buffer is available */
        usb_spin_unlock_irqrestore(&my_printer->lock, flags);
        ret = thread_waiter_wait_timeout(&my_printer->tx_wait, timeout_ms);
        usb_spin_lock_irqsave(&my_printer->lock, flags);

        if (my_printer->exit_flag) {
            ret = -ENODEV;
            goto out;
        }

        if (!my_printer->connect_flag) {
            ret = -ENOLINK;
            goto out;
        }

        if (ret) {
            ret = -ETIMEDOUT;
            goto out;
        }

    }

    while (!list_empty(&my_printer->tx_reqs) && len) {

        if (len > USB_BUFSIZE)
            size = USB_BUFSIZE;
        else
            size = len;

        req = container_of(my_printer->tx_reqs.next, struct usb_request, list);
        list_del_init(&req->list);

        req->complete = tx_complete;
        req->length = size;

        /* Check if we need to send a zero length packet. */
        if (len > size)
            /* They will be more TX requests so no yet. */
            req->zero = 0;
        else
            /* If the data amount is not a multiple of the
             * maxpacket size then send a zero length packet.
             */
            req->zero = ((len % my_printer->in_ep->maxpacket) == 0);

        memcpy(req->buf, buffer, size);

        value = usb_ep_queue(my_printer->in_ep, req);
        if (value) {
            printf("%s: %s %s err %d\n", __func__, "queue", my_printer->in_ep->name, value);
            list_add(&req->list, &my_printer->tx_reqs);
            break;
        } else {
            bytes_copied += size;
            len -= size;
            buffer += size;
            list_add(&req->list, &my_printer->tx_reqs_active);
        }
    }

    if (bytes_copied)
        ret = bytes_copied;
    else
        ret = -EAGAIN;
out:
    usb_spin_unlock_irqrestore(&my_printer->lock, flags);
    set_no_busy();
    return ret;
}

int gadget_printer_flush_data(uint32_t timeout_ms)
{
    int ret = 0;

    if (check_set_busy())
        return -ENODEV;

    if (!my_printer->connect_flag) {
        ret = -ENOLINK;
        goto out;
    }

    while (!list_empty(&my_printer->tx_reqs_active)) {
        ret = thread_waiter_wait_timeout(&my_printer->tx_flush_wait, timeout_ms);

        if (my_printer->exit_flag) {
            ret = -ENODEV;
            goto out;
        }

        if (!my_printer->connect_flag) {
            ret = -ENOLINK;
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

int gadget_printer_wait_connect(uint32_t timeout_ms)
{
    int ret = 0;
    
    if (check_set_busy())
        return -ENODEV;

    while (!my_printer->connect_flag) {
        ret = thread_waiter_wait_timeout(&my_printer->connect_wait, timeout_ms);

        if (my_printer->exit_flag) {
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

int gadget_printer_get_connect_status(void)
{
    int status;

    os_enter_critical();

    if (my_printer == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    if (my_printer->connect_flag)
        status = 1;
    else
        status = 0;

    os_exit_critical();

    return status;
}

int gadget_printer_get_status(u8 *status)
{
    assert(status);

    os_enter_critical();

    if (my_printer == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    *status = my_printer->printer_status;

    os_exit_critical();

    return 0;
}

int gadget_printer_set_status(u8 status)
{
    os_enter_critical();

    if (my_printer == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    my_printer->printer_status = status;

    os_exit_critical();

    return 0;
}