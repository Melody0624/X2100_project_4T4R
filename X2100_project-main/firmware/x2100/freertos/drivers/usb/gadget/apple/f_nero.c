#include <common.h>
#include <malloc.h>
#include <os.h>
#include <driver/cache.h>
#include <usb/gadget_apple.h>

#include "../composite.h"
#include "../../usb_lock.h"

struct nero_dev {
    spinlock_t          lock;           /* lock this structure */
    struct usb_gadget   *gadget;
    s8                  interface;
    struct usb_ep       *out_ep, *in_ep;

    struct list_head    rx_reqs;        /* List of free RX structs */
    struct list_head    rx_reqs_active; /* List of Active RX xfers */
    struct list_head    rx_buffers;     /* List of completed xfers */
    /* wait until there is data to be read. */
    thread_waiter_t     rx_wait;

    struct list_head    tx_reqs;        /* List of free TX structs */
    struct list_head    tx_reqs_active; /* List of Active TX xfers */
    /* Wait until there are write buffers available to use. */
    thread_waiter_t     tx_wait;
    /* Wait until all write buffers have been sent. */
    thread_waiter_t     tx_flush_wait;

    struct usb_function function;

    uint32_t            ops_busy;
    uint8_t             exit_flag;
    thread_waiter_t     exit_wait;

    u8                  interface_id;

    uint8_t             connect_flag;
    connect_callback_t  connect_cb;
    thread_waiter_t     connect_wait;
};

static struct nero_dev *my_nero;

static inline struct nero_dev *func_to_nero(struct usb_function *f)
{
    return container_of(f, struct nero_dev, function);
}

/*-------------------------------------------------------------------------*/
#define RX_REQ_MAX  2
#define TX_REQ_MAX  2

#define RX_USB_BUFSIZE             (128*1024)
#define TX_USB_BUFSIZE             (16*1024)

#define INTERFACE_STRING_INDEX    0
static struct usb_string nero_string_defs[] = {
    [INTERFACE_STRING_INDEX].s = "Nero",
    {},                         /* end of list */
};

static struct usb_gadget_strings nero_string_table = {
    .language = 0x0409,         /* en-US */
    .strings = nero_string_defs,
};

static struct usb_gadget_strings *nero_strings[] = {
    &nero_string_table,
    NULL,
};

static struct usb_interface_descriptor nero_interface_desc = {
    .bLength =        sizeof(nero_interface_desc),
    .bDescriptorType =    USB_DT_INTERFACE,
    .bNumEndpoints =    2,
    .bInterfaceClass =    USB_CLASS_VENDOR_SPEC,
    .bInterfaceSubClass =    0x2A,
    .bInterfaceProtocol =    0xff,
    .iInterface =        0
};

static struct usb_endpoint_descriptor fs_ep_out_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,
    .bEndpointAddress =    USB_DIR_OUT,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK
};

static struct usb_endpoint_descriptor fs_ep_in_desc = {
    .bLength        = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType    = USB_DT_ENDPOINT,
    .bEndpointAddress    = USB_DIR_IN,
    .bmAttributes        = USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *fs_bulk_function[] = {
    (struct usb_descriptor_header *) &nero_interface_desc,
    (struct usb_descriptor_header *) &fs_ep_out_desc,
    (struct usb_descriptor_header *) &fs_ep_in_desc,
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

static struct usb_endpoint_descriptor hs_ep_in_desc = {
    .bLength        = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType    = USB_DT_ENDPOINT,
    .bEndpointAddress    = USB_DIR_IN,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =    512
};

static struct usb_descriptor_header *hs_bulk_function[] = {
    (struct usb_descriptor_header *) &nero_interface_desc,
    (struct usb_descriptor_header *) &hs_ep_out_desc,
    (struct usb_descriptor_header *) &hs_ep_in_desc,
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

static struct usb_request *nero_req_alloc(struct usb_ep *ep, unsigned len)
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

static void nero_req_free(struct usb_ep *ep, struct usb_request *req)
{
    if (ep != NULL && req != NULL) {
        free(req->buf);
        usb_ep_free_request(ep, req);
    }
}

/*-------------------------------------------------------------------------*/

static void rx_complete(struct usb_ep *ep, struct usb_request *req)
{
    struct nero_dev    *dev = ep->driver_data;
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
        printf("gadget nero rx shutdown, code %d\n", status);
        list_add(&req->list, &dev->rx_reqs);
        break;

    /* for hardware automagic (such as pxa) */
    case -ECONNABORTED:        /* endpoint reset */
        printf("gadget nero rx %s reset\n", ep->name);
        list_add(&req->list, &dev->rx_reqs);
        break;

    /* data overrun */
    case -EOVERFLOW:

    default:
        printf("gadget nero rx status %d\n", status);
        list_add(&req->list, &dev->rx_reqs);
        break;
    }

    usb_spin_unlock_irqrestore(&dev->lock, flags);
}

static void tx_complete(struct usb_ep *ep, struct usb_request *req)
{
    unsigned long flags;
    struct nero_dev    *dev = ep->driver_data;

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

/* This function must be called with interrupts turned off. */
static int setup_rx_reqs(struct nero_dev *dev)
{
    struct usb_request              *req;
    int error = 0;

    while (!list_empty(&dev->rx_reqs)) {

        req = list_entry(dev->rx_reqs.next, struct usb_request, list);
        list_del_init(&req->list);

        /* The USB Host sends us whatever amount of data it wants to
         * so we always set the length field to the full RX_USB_BUFSIZE.
         * If the amount of data is more than the read() caller asked
         * for it will be stored in the request buffer until it is
         * asked for by read().
         */
        req->length = RX_USB_BUFSIZE;
        req->complete = rx_complete;

        /* here, we unlock, and only unlock, to avoid deadlock. */
        error = usb_ep_queue(dev->out_ep, req);
        if (error) {
            printf("%s: rx submit err --> %d\n", __func__, error);
            list_add(&req->list, &dev->rx_reqs);
            break;
        }
        /* if the req is empty, then add it into dev->rx_reqs_active. */
        else {
            if (list_empty(&req->list))
                list_add(&req->list, &dev->rx_reqs_active);
        }
    }

    return error;
}

static int
set_nero_interface(struct nero_dev *dev)
{
    int            result = 0;

    dev->out_ep->desc = ep_desc(dev->gadget, &fs_ep_out_desc, &hs_ep_out_desc);
    dev->out_ep->driver_data = dev;

    dev->in_ep->desc = ep_desc(dev->gadget, &fs_ep_in_desc, &hs_ep_in_desc);
    dev->in_ep->driver_data = dev;

    result = usb_ep_enable(dev->out_ep);
    if (result != 0) {
        printf("gadget generic bulk  enable %s --> %d\n", dev->out_ep->name, result);
        goto done;
    }

    result = usb_ep_enable(dev->in_ep);
    if (result != 0) {
        printf("gadget generic bulk enable %s err--> %d\n", dev->in_ep->name, result);
        goto done;
    }

    setup_rx_reqs(dev);

done:
    /* on error, disable any endpoints  */
    if (result != 0) {
        usb_ep_disable(dev->out_ep);
        usb_ep_disable(dev->in_ep);
        dev->out_ep->desc = NULL;
        dev->in_ep->desc = NULL;
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

static void nero_reset_interface(struct nero_dev *dev)
{
    unsigned long   flags;

    if (dev->interface < 0)
        return;

    if (dev->out_ep->desc)
        usb_ep_disable(dev->out_ep);

    if (dev->in_ep->desc)
        usb_ep_disable(dev->in_ep);

    usb_spin_lock_irqsave(&dev->lock, flags);
    dev->out_ep->desc = NULL;
    dev->in_ep->desc = NULL;
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
static int set_interface(struct nero_dev *dev, unsigned number)
{
    int            result = 0;

    /* Free the current interface */
    nero_reset_interface(dev);

    result = set_nero_interface(dev);
    if (result)
        nero_reset_interface(dev);
    else
        dev->interface = number;

    return result;
}

static bool nero_req_match(struct usb_function *f,
                   const struct usb_ctrlrequest *ctrl,
                   bool config0)
{
    struct nero_dev *dev = func_to_nero(f);

    if (config0 || (ctrl->wIndex != dev->interface_id))
        return false;

    return false;
}

static int nero_func_bind(struct usb_configuration *c,
        struct usb_function *f)
{
    struct usb_gadget   *gadget = c->cdev->gadget;
    struct nero_dev     *dev = func_to_nero(f);
    struct usb_composite_dev *cdev = c->cdev;
    struct usb_ep       *out_ep = NULL;
    struct usb_ep       *in_ep = NULL;
    struct usb_request  *req;
    struct usb_string   *us;
    int id;
    int ret;
    u32 i;

    /* maybe allocate device-global string IDs, and patch descriptors */
    us = usb_gstrings_attach(cdev, nero_strings, ARRAY_SIZE(nero_string_defs));
    if (IS_ERR(us))
        return PTR_ERR(us);
    nero_interface_desc.iInterface = us[INTERFACE_STRING_INDEX].id;;

    id = usb_interface_id(c, f);
    if (id < 0)
        return id;
    nero_interface_desc.bInterfaceNumber = id;
    dev->interface_id = id;

    /* finish hookup to lower layer ... */
    dev->gadget = gadget;

    /* all we really need is bulk IN/OUT */
    out_ep = usb_ep_autoconfig(cdev->gadget, &fs_ep_out_desc);
    if (!out_ep)
        panic("%s %d: can't autoconfigure on %s\n", __func__, __LINE__, cdev->gadget->name);

    in_ep = usb_ep_autoconfig(cdev->gadget, &fs_ep_in_desc);
    if (!in_ep)
        panic("%s %d: can't autoconfigure on %s\n", __func__, __LINE__, cdev->gadget->name);

    /* assumes that all endpoints are dual-speed */
    hs_ep_out_desc.bEndpointAddress = fs_ep_out_desc.bEndpointAddress;
    hs_ep_in_desc.bEndpointAddress = fs_ep_in_desc.bEndpointAddress;
    ret = usb_assign_descriptors(f, fs_bulk_function, hs_bulk_function);
    if (ret)
        return ret;

    dev->out_ep = out_ep;
    dev->in_ep = in_ep;

    ret = -ENOMEM;

    for (i = 0; i < RX_REQ_MAX; i++) {
        req = nero_req_alloc(dev->out_ep, RX_USB_BUFSIZE);
        if (!req)
            goto fail_rx_reqs;
        list_add(&req->list, &dev->rx_reqs);
    }

    for (i = 0; i < TX_REQ_MAX; i++) {
        req = nero_req_alloc(dev->in_ep, TX_USB_BUFSIZE);
        if (!req)
            goto fail_tx_reqs;
        list_add(&req->list, &dev->tx_reqs);
    }

    return 0;

fail_tx_reqs:
    while (!list_empty(&dev->tx_reqs)) {
        req = container_of(dev->tx_reqs.next, struct usb_request, list);
        list_del(&req->list);
        nero_req_free(dev->in_ep, req);
    }
fail_rx_reqs:
    while (!list_empty(&dev->rx_reqs)) {
        req = list_entry(dev->rx_reqs.next, struct usb_request, list);
        list_del(&req->list);
        nero_req_free(dev->out_ep, req);
    }

    usb_free_all_descriptors(f);
    return ret;
}

static int nero_func_set_alt(struct usb_function *f,
        unsigned intf, unsigned alt)
{
    struct nero_dev *dev = func_to_nero(f);
    int ret = -EINVAL;

    printf("%s: alt %d\n", __func__, alt);

    if (!alt)
        ret = set_interface(dev, intf);

    return ret;
}

static void nero_func_disable(struct usb_function *f)
{
    struct nero_dev *dev = func_to_nero(f);

    nero_reset_interface(dev);
}


static void nero_func_unbind(struct usb_configuration *c,
        struct usb_function *f)
{
    struct nero_dev    *dev;
    struct usb_request    *req;

    dev = func_to_nero(f);

    /* we must already have been disconnected ... no i/o may be active */
    if ((!list_empty(&dev->tx_reqs_active)) || (!list_empty(&dev->rx_reqs_active)))
        printf("%s: error !!!\n", __func__);

    /* Free all memory for this driver. */
    while (!list_empty(&dev->tx_reqs)) {
        req = container_of(dev->tx_reqs.next, struct usb_request, list);
        list_del(&req->list);
        nero_req_free(dev->in_ep, req);
    }

    while (!list_empty(&dev->rx_reqs)) {
        req = list_entry(dev->rx_reqs.next,
                struct usb_request, list);
        list_del(&req->list);
        nero_req_free(dev->out_ep, req);
    }

    while (!list_empty(&dev->rx_buffers)) {
        req = list_entry(dev->rx_buffers.next,
                struct usb_request, list);
        list_del(&req->list);
        nero_req_free(dev->out_ep, req);
    }

    usb_free_all_descriptors(f);
}

void nero_device_free(void)
{
    os_enter_critical();
    assert(my_nero);
    assert(!my_nero->exit_flag);
    my_nero->exit_flag = 1;
    while (my_nero->ops_busy) {
        thread_waiter_wakeup(&my_nero->rx_wait);
        thread_waiter_wakeup(&my_nero->tx_wait);
        thread_waiter_wakeup(&my_nero->tx_flush_wait);
        thread_waiter_wakeup(&my_nero->connect_wait);
        os_exit_critical();
        thread_waiter_wait(&my_nero->exit_wait);
        os_enter_critical();
    }
    free(my_nero);
    my_nero = NULL;
    os_exit_critical();
}

struct usb_function *nero_device_alloc(connect_callback_t connect_cb)
{
    os_enter_critical();

    assert(my_nero == NULL);

    my_nero = malloc(sizeof(*my_nero));
    if (my_nero == NULL) {
        os_exit_critical();
        return ERR_PTR(-ENOMEM);
    }
    memset(my_nero, 0, sizeof(*my_nero));

    my_nero->connect_cb = connect_cb;

    my_nero->function.name = "accessory2";
    my_nero->function.strings = nero_strings;
    my_nero->function.bind = nero_func_bind;
    my_nero->function.unbind = nero_func_unbind;
    my_nero->function.set_alt = nero_func_set_alt;
    my_nero->function.disable = nero_func_disable;
    my_nero->function.req_match = nero_req_match;

    INIT_LIST_HEAD(&my_nero->tx_reqs);
    INIT_LIST_HEAD(&my_nero->rx_reqs);
    INIT_LIST_HEAD(&my_nero->rx_buffers);
    INIT_LIST_HEAD(&my_nero->tx_reqs_active);
    INIT_LIST_HEAD(&my_nero->rx_reqs_active);

    spin_lock_init_recursive(&my_nero->lock);
    thread_waiter_init(&my_nero->rx_wait);
    thread_waiter_init(&my_nero->tx_wait);
    thread_waiter_init(&my_nero->tx_flush_wait);
    thread_waiter_init(&my_nero->connect_wait);
    thread_waiter_init(&my_nero->exit_wait);

    my_nero->interface = -1;
    os_exit_critical();

    return &my_nero->function;
}

/*-------------------------------------------------------------------------*/

static int check_set_busy(void)
{
    os_enter_critical();

    if (my_nero == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    my_nero->ops_busy++;

    os_exit_critical();
    return 0;
}

static void set_no_busy(void)
{
    os_enter_critical();

    if (my_nero) {
        my_nero->ops_busy--;
        if (my_nero->exit_flag)
            thread_waiter_wakeup(&my_nero->exit_wait);
    }

    os_exit_critical();
}

int gadget_nero_get_buffer(struct nero_buffer *nero_buffer, uint32_t timeout_ms)
{
    int ret = 0;
    unsigned long           flags;
    struct usb_request      *req;

    if (!nero_buffer)
        return -EINVAL;

    if (check_set_busy())
        return -ENODEV;

    usb_spin_lock_irqsave(&my_nero->lock, flags);

    if (!my_nero->connect_flag) {
        ret = -ENOLINK;
        goto out;
    }

    if (setup_rx_reqs(my_nero) < 0) {
        ret = -EIO;
        goto out;
    }

    while (list_empty(&my_nero->rx_buffers)) {
        if (!timeout_ms) {
            ret = -EAGAIN;
            goto out;
        }

        /* Sleep until data is available */
        usb_spin_unlock_irqrestore(&my_nero->lock, flags);
        ret = thread_waiter_wait_timeout(&my_nero->rx_wait, timeout_ms);
        usb_spin_lock_irqsave(&my_nero->lock, flags);
        if (my_nero->exit_flag) {
            ret = -ENODEV;
            goto out;
        }

        if (!my_nero->connect_flag) {
            ret = -ENOLINK;
            goto out;
        }

        if (ret) {
            ret = -ETIMEDOUT;
            goto out;
        }
    }

    req = list_entry(my_nero->rx_buffers.next, struct usb_request, list);
    list_del_init(&req->list);

    nero_buffer->length = req->length;
    nero_buffer->actual = req->actual;
    nero_buffer->buf = req->buf;
    nero_buffer->private_data = req;

out:
    usb_spin_unlock_irqrestore(&my_nero->lock, flags);
    set_no_busy();
    return ret;
}

int gadget_nero_put_buffer(struct nero_buffer *nero_buffer)
{
    struct usb_request        *req;

    if (!nero_buffer || !nero_buffer->private_data)
        return -EINVAL;

    os_enter_critical();

    if (my_nero == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    req = nero_buffer->private_data;
    list_add(&req->list, &my_nero->rx_reqs);

    if (my_nero->connect_flag)
        setup_rx_reqs(my_nero);

    nero_buffer->length = 0;
    nero_buffer->actual = 0;
    nero_buffer->buf = NULL;
    nero_buffer->private_data = NULL;

    os_exit_critical();

    return 0;
}

int gadget_nero_write(const uint8_t *buffer, uint32_t len, uint32_t timeout_ms)
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

    usb_spin_lock_irqsave(&my_nero->lock, flags);

    if (!my_nero->connect_flag) {
        ret = -ENOLINK;
        goto out;
    }

    /* Check if there is any available write buffers */
    while (list_empty(&my_nero->tx_reqs)) {
        if (!timeout_ms) {
            ret = -EAGAIN;
            goto out;
        }

        /* Sleep until a write buffer is available */
        usb_spin_unlock_irqrestore(&my_nero->lock, flags);
        ret = thread_waiter_wait_timeout(&my_nero->tx_wait, timeout_ms);
        usb_spin_lock_irqsave(&my_nero->lock, flags);

        if (my_nero->exit_flag) {
            ret = -ENODEV;
            goto out;
        }

        if (!my_nero->connect_flag) {
            ret = -ENOLINK;
            goto out;
        }

        if (ret) {
            ret = -ETIMEDOUT;
            goto out;
        }

    }

    while (!list_empty(&my_nero->tx_reqs) && len) {
        if (len > TX_USB_BUFSIZE)
            size = TX_USB_BUFSIZE;
        else
            size = len;

        req = container_of(my_nero->tx_reqs.next, struct usb_request, list);
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
            req->zero = ((len % my_nero->in_ep->maxpacket) == 0);

        memcpy(req->buf, buffer, size);

        value = usb_ep_queue(my_nero->in_ep, req);
        if (value) {
            printf("%s: %s %s err %d\n", __func__, "queue", my_nero->in_ep->name, value);
            list_add(&req->list, &my_nero->tx_reqs);
            break;
        } else {
            bytes_copied += size;
            len -= size;
            buffer += size;
            list_add(&req->list, &my_nero->tx_reqs_active);
        }
    }

    if (bytes_copied)
        ret = bytes_copied;
    else
        ret = -EAGAIN;
out:
    usb_spin_unlock_irqrestore(&my_nero->lock, flags);
    set_no_busy();
    return ret;
}

int gadget_nero_flush_data(uint32_t timeout_ms)
{
    int ret = 0;

    if (check_set_busy())
        return -ENODEV;

    if (!my_nero->connect_flag) {
        ret = -ENOLINK;
        goto out;
    }

    while (!list_empty(&my_nero->tx_reqs_active)) {
        ret = thread_waiter_wait_timeout(&my_nero->tx_flush_wait, timeout_ms);

        if (my_nero->exit_flag) {
            ret = -ENODEV;
            goto out;
        }

        if (!my_nero->connect_flag) {
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

int gadget_nero_get_connect_status(void)
{
    int status;

    os_enter_critical();

    if (my_nero == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    if (my_nero->connect_flag)
        status = 1;
    else
        status = 0;

    os_exit_critical();

    return status;
}

int gadget_nero_wait_connect(uint32_t timeout_ms)
{
    int ret = 0;

    if (check_set_busy())
        return -ENODEV;

    while (!my_nero->connect_flag) {
        ret = thread_waiter_wait_timeout(&my_nero->connect_wait, timeout_ms);

        if (my_nero->exit_flag) {
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
