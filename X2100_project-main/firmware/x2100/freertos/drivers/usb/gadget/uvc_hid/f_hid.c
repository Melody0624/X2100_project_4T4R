#include <common.h>
#include <malloc.h>
#include <os.h>
#include <usb/gadget_uvc_hid.h>

#include "../composite.h"
#include "hid.h"

#define QUEUE_SIZE        4

static struct f_hidg *my_hid;

/*-------------------------------------------------------------------------*/
/*                           Static descriptors                            */

static struct usb_interface_descriptor hidg_interface_desc = {
    .bLength        = sizeof hidg_interface_desc,
    .bDescriptorType    = USB_DT_INTERFACE,
    /* .bInterfaceNumber    = DYNAMIC */
    .bAlternateSetting    = 0,
    .bNumEndpoints        = 2,
    .bInterfaceClass    = USB_CLASS_HID,
    /* .bInterfaceSubClass    = DYNAMIC */
    /* .bInterfaceProtocol    = DYNAMIC */
    /* .iInterface        = DYNAMIC */
};

static struct hid_descriptor hidg_desc = {
    .bLength            = sizeof hidg_desc,
    .bDescriptorType        = HID_DT_HID,
    .bcdHID                = 0x0101,
    .bCountryCode            = 0x00,
    .bNumDescriptors        = 0x1,
    /*.desc[0].bDescriptorType    = DYNAMIC */
    /*.desc[0].wDescriptorLenght    = DYNAMIC */
};

/* High-Speed Support */

static struct usb_endpoint_descriptor hidg_hs_in_ep_desc = {
    .bLength        = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType    = USB_DT_ENDPOINT,
    .bEndpointAddress    = USB_DIR_IN,
    .bmAttributes        = USB_ENDPOINT_XFER_INT,
    /*.wMaxPacketSize    = DYNAMIC */
    .bInterval        = 4, /* FIXME: Add this field in the
                      * HID gadget configuration?
                      * (struct hidg_func_descriptor)
                      */
};

static struct usb_endpoint_descriptor hidg_hs_out_ep_desc = {
    .bLength        = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType    = USB_DT_ENDPOINT,
    .bEndpointAddress    = USB_DIR_OUT,
    .bmAttributes        = USB_ENDPOINT_XFER_INT,
    /*.wMaxPacketSize    = DYNAMIC */
    .bInterval        = 4, /* FIXME: Add this field in the
                      * HID gadget configuration?
                      * (struct hidg_func_descriptor)
                      */
};

static struct usb_descriptor_header *hidg_hs_descriptors[] = {
    (struct usb_descriptor_header *)&hidg_interface_desc,
    (struct usb_descriptor_header *)&hidg_desc,
    (struct usb_descriptor_header *)&hidg_hs_in_ep_desc,
    (struct usb_descriptor_header *)&hidg_hs_out_ep_desc,
    NULL,
};

/* Full-Speed Support */

static struct usb_endpoint_descriptor hidg_fs_in_ep_desc = {
    .bLength        = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType    = USB_DT_ENDPOINT,
    .bEndpointAddress    = USB_DIR_IN,
    .bmAttributes        = USB_ENDPOINT_XFER_INT,
    /*.wMaxPacketSize    = DYNAMIC */
    .bInterval        = 10, /* FIXME: Add this field in the
                       * HID gadget configuration?
                       * (struct hidg_func_descriptor)
                       */
};

static struct usb_endpoint_descriptor hidg_fs_out_ep_desc = {
    .bLength        = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType    = USB_DT_ENDPOINT,
    .bEndpointAddress    = USB_DIR_OUT,
    .bmAttributes        = USB_ENDPOINT_XFER_INT,
    /*.wMaxPacketSize    = DYNAMIC */
    .bInterval        = 10, /* FIXME: Add this field in the
                       * HID gadget configuration?
                       * (struct hidg_func_descriptor)
                       */
};

static struct usb_descriptor_header *hidg_fs_descriptors[] = {
    (struct usb_descriptor_header *)&hidg_interface_desc,
    (struct usb_descriptor_header *)&hidg_desc,
    (struct usb_descriptor_header *)&hidg_fs_in_ep_desc,
    (struct usb_descriptor_header *)&hidg_fs_out_ep_desc,
    NULL,
};

/*-------------------------------------------------------------------------*/
/*                                 Strings                                 */

#define CT_FUNC_HID_IDX    0

static struct usb_string ct_func_string_defs[] = {
    [CT_FUNC_HID_IDX].s    = "HID Interface",
    {},            /* end of list */
};

static struct usb_gadget_strings ct_func_string_table = {
    .language    = 0x0409,    /* en-US */
    .strings    = ct_func_string_defs,
};

static struct usb_gadget_strings *ct_func_strings[] = {
    &ct_func_string_table,
    NULL,
};

/*-------------------------------------------------------------------------*/
/*                                usb_function                             */

static inline struct usb_request *hidg_alloc_ep_req(struct usb_ep *ep, unsigned length)
{
    return alloc_ep_req(ep, length);
}

static void hidg_set_report_complete(struct usb_ep *ep, struct usb_request *req)
{
    struct f_hidg *hidg = (struct f_hidg *) req->context;
    struct f_hidg_req_list *req_list;
    unsigned long flags;

    switch (req->status) {
    case 0:
        req_list = malloc(sizeof(*req_list));
        if (!req_list) {
            printf("%s: Unable to allocate mem for req_list\n", __func__);
            goto free_req;
        }
        memset(req_list, 0, sizeof(*req_list));

        req_list->req = req;

        usb_spin_lock_irqsave(&hidg->read_spinlock, flags);
        list_add_tail(&req_list->list, &hidg->completed_out_req);
        usb_spin_unlock_irqrestore(&hidg->read_spinlock, flags);

        thread_waiter_wakeup(&hidg->read_wait);
        break;
    default:
        printf("%s: Set report failed %d\n", __func__, req->status);
        /* FALLTHROUGH */
    case -ECONNABORTED:        /* hardware forced ep reset */
    case -ECONNRESET:        /* request dequeued */
    case -ESHUTDOWN:        /* disconnect from host */
free_req:
        free_ep_req(ep, req);
        return;
    }
}

static bool hidg_req_match(struct usb_function *f, const struct usb_ctrlrequest *ctrl, bool config0)
{
    struct f_hidg            *hidg = func_to_hidg(f);
    u16            w_index = ctrl->wIndex;
    u16 w_value    = ctrl->wValue;

    if (config0 || (w_index != hidg->interface_id))
        return false;

    switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
          | HID_REQ_GET_REPORT):
        break;

    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
          | HID_REQ_GET_PROTOCOL):
        break;

    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
          | HID_REQ_GET_IDLE):
        break;

    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
          | HID_REQ_SET_REPORT):
        break;

    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
          | HID_REQ_SET_IDLE):
        break;

    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
          | HID_REQ_SET_PROTOCOL):
        if (w_value > HID_REPORT_PROTOCOL)
            return false;
        break;

    case ((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) << 8
          | USB_REQ_GET_DESCRIPTOR):
        break;

    default:
        return false;
    }

    return true;
}

static int hidg_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
    struct f_hidg            *hidg = func_to_hidg(f);
    struct usb_composite_dev    *cdev = f->config->cdev;
    struct usb_request        *req  = cdev->req;
    int status = 0;
    u16 value, length;
    int ret = 0;

    value    = ctrl->wValue;
    length    = ctrl->wLength;

    switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
          | HID_REQ_GET_REPORT):
        if (hidg->request_cb) {
            ret = hidg->request_cb(req->buf, &length, HID_REQ_GET_REPORT);
            if (ret < 0)
                goto stall;
        } else {
            /* send an empty report */
            length = min_t(unsigned, length, hidg->report_length);
            memset(req->buf, 0x0, length);
        }
        goto respond;
        break;

    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
          | HID_REQ_GET_PROTOCOL):
        length = min_t(uint32_t, length, 1);
        ((u8 *) req->buf)[0] = hidg->protocol;
        goto respond;
        break;

    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
          | HID_REQ_GET_IDLE):
        if (hidg->request_cb) {
            ret = hidg->request_cb(req->buf, &length, HID_REQ_GET_IDLE);
            if (ret >= 0)
                goto respond;
        }
        goto stall;
        break;

    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
          | HID_REQ_SET_REPORT):
        if (hidg->request_cb) {
            ret = hidg->request_cb(req->buf, &length, HID_REQ_SET_REPORT);
            if (ret >= 0)
                goto respond;
        }
        goto stall;
        break;

    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
          | HID_REQ_SET_IDLE):
        if (hidg->request_cb) {
            ret = hidg->request_cb(req->buf, &length, HID_REQ_SET_IDLE);
            if (ret >= 0)
                goto respond;
        }
        goto stall;
        break;

    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
          | HID_REQ_SET_PROTOCOL):
        if (value > HID_REPORT_PROTOCOL)
            goto stall;
        length = 0;
        /*
         * We assume that programs implementing the Boot protocol
         * are also compatible with the Report Protocol
         */
        if (hidg->bInterfaceSubClass == USB_INTERFACE_SUBCLASS_BOOT) {
            hidg->protocol = value;
            goto respond;
        }
        goto stall;
        break;

    case ((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) << 8
          | USB_REQ_GET_DESCRIPTOR):
        switch (value >> 8) {
        case HID_DT_HID:
        {
            struct hid_descriptor hidg_desc_copy = hidg_desc;

            hidg_desc_copy.desc[0].bDescriptorType = HID_DT_REPORT;
            hidg_desc_copy.desc[0].wDescriptorLength = hidg->report_desc_length;

            length = min_t(unsigned short, length, hidg_desc_copy.bLength);
            memcpy(req->buf, &hidg_desc_copy, length);
            goto respond;
            break;
        }
        case HID_DT_REPORT:
            length = min_t(unsigned short, length, hidg->report_desc_length);
            memcpy(req->buf, hidg->report_desc, length);
            goto respond;
            break;

        default:
            printf("%s: Unknown descriptor request 0x%x\n", __func__, value >> 8);
            goto stall;
            break;
        }
        break;

    default:
        printf("%s: Unknown request 0x%x\n", __func__, ctrl->bRequest);
        goto stall;
        break;
    }

stall:
    return -EOPNOTSUPP;

respond:
    req->zero = 0;
    req->length = length;
    status = usb_ep_queue(cdev->gadget->ep0, req);
    if (status < 0)
        printf("usb_ep_queue error on ep0 %d\n", value);
    return status;
}

static void hidg_disable(struct usb_function *f)
{
    struct f_hidg *hidg = func_to_hidg(f);
    struct f_hidg_req_list *list, *next;
    unsigned long flags;

    usb_ep_disable(hidg->in_ep);
    usb_ep_disable(hidg->out_ep);

    usb_spin_lock_irqsave(&hidg->read_spinlock, flags);

    list_for_each_entry_safe(list, next, &hidg->completed_out_req, list) {
        free_ep_req(hidg->out_ep, list->req);
        list_del(&list->list);
        free(list);
    }

    usb_spin_unlock_irqrestore(&hidg->read_spinlock, flags);

    usb_spin_lock_irqsave(&hidg->write_spinlock, flags);
    if (!hidg->write_pending) {
        free_ep_req(hidg->in_ep, hidg->req);
        hidg->write_pending = 1;
    }

    hidg->req = NULL;
    hidg->connect_flag = 0;
    usb_spin_unlock_irqrestore(&hidg->write_spinlock, flags);

    if (hidg->connect_cb)
        hidg->connect_cb(0);

    thread_waiter_wakeup(&my_hid->read_wait);
    thread_waiter_wakeup(&my_hid->write_wait);
}


static int hidg_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
    struct f_hidg                *hidg = func_to_hidg(f);
    struct usb_request            *req_in = NULL;
    unsigned long                flags;
    int i, status = 0;

    if (intf != hidg->interface_id)
        return -EINVAL;

    if (hidg->in_ep != NULL) {
        /* restart endpoint */
        usb_ep_disable(hidg->in_ep);

        status = config_ep_by_speed(f->config->cdev->gadget, f, hidg->in_ep);
        if (status) {
            printf("%s: config_ep_by_speed FAILED!\n", __func__);
            goto fail;
        }
        status = usb_ep_enable(hidg->in_ep);
        if (status < 0) {
            printf("%s: Enable IN endpoint FAILED!\n", __func__);
            goto fail;
        }
        hidg->in_ep->driver_data = hidg;

        req_in = hidg_alloc_ep_req(hidg->in_ep, hidg->report_length);
        if (!req_in) {
            status = -ENOMEM;
            goto disable_ep_in;
        }
    }

    if (hidg->out_ep != NULL) {
        /* restart endpoint */
        usb_ep_disable(hidg->out_ep);

        status = config_ep_by_speed(f->config->cdev->gadget, f, hidg->out_ep);
        if (status) {
            printf("%s: config_ep_by_speed FAILED!\n", __func__);
            goto free_req_in;
        }
        status = usb_ep_enable(hidg->out_ep);
        if (status < 0) {
            printf("%s: Enable OUT endpoint FAILED!\n", __func__);
            goto free_req_in;
        }
        hidg->out_ep->driver_data = hidg;

        /*
         * allocate a bunch of read buffers and queue them all at once.
         */
        for (i = 0; i < QUEUE_SIZE && status == 0; i++) {
            struct usb_request *req = hidg_alloc_ep_req(hidg->out_ep, hidg->report_length);
            if (req) {
                req->complete = hidg_set_report_complete;
                req->context  = hidg;
                status = usb_ep_queue(hidg->out_ep, req);
                if (status) {
                    printf("%s queue req --> %d\n", hidg->out_ep->name, status);
                    free_ep_req(hidg->out_ep, req);
                }
            }
        }
    }

    if (hidg->in_ep != NULL) {
        usb_spin_lock_irqsave(&hidg->write_spinlock, flags);
        hidg->req = req_in;
        hidg->connect_flag = 1;
        hidg->write_pending = 0;
        usb_spin_unlock_irqrestore(&hidg->write_spinlock, flags);

        if (hidg->connect_cb)
            hidg->connect_cb(1);

        thread_waiter_wakeup(&hidg->connect_wait);
        thread_waiter_wakeup(&hidg->write_wait);
    }

    return 0;

free_req_in:
    if (req_in)
        free_ep_req(hidg->in_ep, req_in);
disable_ep_in:
    if (hidg->in_ep)
        usb_ep_disable(hidg->in_ep);

fail:
    return status;
}

static int hidg_bind(struct usb_configuration *c, struct usb_function *f)
{
    struct usb_ep        *ep;
    struct f_hidg        *hidg = func_to_hidg(f);
    struct usb_string    *us;
    int            status;

    /* maybe allocate device-global string IDs, and patch descriptors */
    us = usb_gstrings_attach(c->cdev, ct_func_strings, ARRAY_SIZE(ct_func_string_defs));
    if (IS_ERR(us))
        return PTR_ERR(us);
    hidg_interface_desc.iInterface = us[CT_FUNC_HID_IDX].id;

    /* allocate instance-specific interface IDs, and patch descriptors */
    status = usb_interface_id(c, f);
    if (status < 0)
        goto fail;
    hidg_interface_desc.bInterfaceNumber = status;
    hidg->interface_id = status;

    /* allocate instance-specific endpoints */
    status = -ENODEV;
    ep = usb_ep_autoconfig(c->cdev->gadget, &hidg_fs_in_ep_desc);
    if (!ep)
        goto fail;
    hidg->in_ep = ep;

    ep = usb_ep_autoconfig(c->cdev->gadget, &hidg_fs_out_ep_desc);
    if (!ep)
        goto fail;
    hidg->out_ep = ep;

    /* set descriptor dynamic values */
    hidg_interface_desc.bInterfaceSubClass = hidg->bInterfaceSubClass;
    hidg_interface_desc.bInterfaceProtocol = hidg->bInterfaceProtocol;
    hidg->protocol = HID_REPORT_PROTOCOL;
    hidg_hs_in_ep_desc.wMaxPacketSize = hidg->report_length;
    hidg_fs_in_ep_desc.wMaxPacketSize = hidg->report_length;
    hidg_hs_out_ep_desc.wMaxPacketSize = hidg->report_length;
    hidg_fs_out_ep_desc.wMaxPacketSize = hidg->report_length;
    /*
     * We can use hidg_desc struct here but we should not relay
     * that its content won't change after returning from this function.
     */
    hidg_desc.desc[0].bDescriptorType = HID_DT_REPORT;
    hidg_desc.desc[0].wDescriptorLength = hidg->report_desc_length;

    hidg_hs_in_ep_desc.bEndpointAddress = hidg_fs_in_ep_desc.bEndpointAddress;
    hidg_hs_out_ep_desc.bEndpointAddress = hidg_fs_out_ep_desc.bEndpointAddress;

    status = usb_assign_descriptors(f, hidg_fs_descriptors, hidg_hs_descriptors);
    if (status)
        goto fail;

    return 0;

fail:
    printf("hidg_bind FAILED\n");
    if (hidg->req != NULL) {
        free_ep_req(hidg->in_ep, hidg->req);
        hidg->req = NULL;
    }

    return status;
}

void hid_device_free(void)
{
    os_enter_critical();
    assert(my_hid);
    assert(!my_hid->exit_flag);
    my_hid->exit_flag = 1;
    while (my_hid->ops_busy) {
        thread_waiter_wakeup(&my_hid->read_wait);
        thread_waiter_wakeup(&my_hid->write_wait);
        thread_waiter_wakeup(&my_hid->connect_wait);
        os_exit_critical();
        thread_waiter_wait(&my_hid->exit_wait);
        os_enter_critical();
    }
    free(my_hid->report_desc);
    my_hid->report_desc = NULL;
    free(my_hid);
    my_hid = NULL;
    os_exit_critical();
}

static void hidg_unbind(struct usb_configuration *c, struct usb_function *f)
{
    usb_free_all_descriptors(f);
}

struct usb_function *hid_device_alloc(const struct hid_report_descriptor *hid_report, connect_callback_t connect_cb, hid_request_callback_t request_cb)
{
    os_enter_critical();

    assert(my_hid == NULL);

    my_hid = malloc(sizeof(*my_hid));
    if (my_hid == NULL) {
        os_exit_critical();
        printf("%s: out of memory\n", __func__);
        return ERR_PTR(-ENOMEM);
    }
    memset(my_hid, 0, sizeof(*my_hid));

    my_hid->connect_cb = connect_cb;
    my_hid->request_cb = request_cb;
    my_hid->bInterfaceSubClass = hid_report->subclass;
    my_hid->bInterfaceProtocol = hid_report->protocol;
    my_hid->report_length = hid_report->report_length;
    my_hid->report_desc_length = hid_report->report_desc_length;
    my_hid->report_desc = malloc(hid_report->report_desc_length);
    if(my_hid->report_desc == NULL) {
        free(my_hid);
        my_hid = NULL;
        os_exit_critical();
        printf("%s: out of memory\n", __func__);
        return ERR_PTR(-ENOMEM);
    }

    memcpy(my_hid->report_desc, hid_report->report_desc, hid_report->report_desc_length);

    my_hid->func.name    = "hid";
    my_hid->func.bind    = hidg_bind;
    my_hid->func.unbind  = hidg_unbind;
    my_hid->func.set_alt = hidg_set_alt;
    my_hid->func.disable = hidg_disable;
    my_hid->func.setup   = hidg_setup;
    my_hid->func.req_match   = hidg_req_match;

    my_hid->req = NULL;
    my_hid->write_pending = 1;
    spin_lock_init_recursive(&my_hid->write_spinlock);
    spin_lock_init_recursive(&my_hid->read_spinlock);
    thread_waiter_init(&my_hid->write_wait);
    thread_waiter_init(&my_hid->read_wait);
    thread_waiter_init(&my_hid->connect_wait);
    thread_waiter_init(&my_hid->exit_wait);
    INIT_LIST_HEAD(&my_hid->completed_out_req);

    os_exit_critical();

    return &my_hid->func;
}

static void f_hidg_req_complete(struct usb_ep *ep, struct usb_request *req)
{
    struct f_hidg *hidg = (struct f_hidg *)ep->driver_data;
    unsigned long flags;

    if (req->status != 0)
        printf("%s: End Point Request ERROR: %d\n", __func__, req->status);

    usb_spin_lock_irqsave(&hidg->write_spinlock, flags);
    hidg->write_pending = 0;
    usb_spin_unlock_irqrestore(&hidg->write_spinlock, flags);
    thread_waiter_wakeup(&hidg->write_wait);

}

/*-------------------------------------------------------------------------*/
/*                              Char Device                                */

static int check_set_busy(void)
{
    os_enter_critical();

    if (my_hid == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    my_hid->ops_busy++;
    os_exit_critical();

    return 0;
}

static void set_no_busy(void)
{
    os_enter_critical();
    if (my_hid) {
        my_hid->ops_busy--;
        if (my_hid->exit_flag)
            thread_waiter_wakeup(&my_hid->exit_wait);
    }
    os_exit_critical();
}

int gadget_hid_read(uint8_t *buffer, uint32_t count, uint8_t block, uint32_t timeout_ms)
{
    struct f_hidg_req_list *list;
    struct usb_request *req;
    unsigned long flags;
    int ret;

    if (!count || buffer == NULL)
        return 0;

    if (check_set_busy())
        return -ENODEV;

    usb_spin_lock_irqsave(&my_hid->read_spinlock, flags);

    if (!my_hid->connect_flag) {
        ret = -ENOLINK;
        goto out;
    }

    /* wait for at least one buffer to complete */
    while (list_empty(&my_hid->completed_out_req)) {
        if (!block) {
            ret = -EAGAIN;
            goto out;
        }

        usb_spin_unlock_irqrestore(&my_hid->read_spinlock, flags);
        ret = thread_waiter_wait_timeout(&my_hid->read_wait, timeout_ms);
        usb_spin_lock_irqsave(&my_hid->read_spinlock, flags);

        if (my_hid->exit_flag) {
            ret = -ENODEV;
            goto out;
        }

        if (!my_hid->connect_flag) {
            ret = -ENOLINK;
            goto out;
        }

        if (ret) {
            ret = -ETIMEDOUT;
            goto out;
        }
    }

    /* pick the first one */
    list = list_first_entry(&my_hid->completed_out_req,
                struct f_hidg_req_list, list);

    /*
     * Remove this from list to protect it from beign free()
     * while host disables our function
     */
    req = list->req;
    count = min_t(uint32_t, count, req->actual - list->pos);

    /* copy to user outside spinlock */
    memcpy(buffer, req->buf + list->pos, count);
    list->pos += count;

    /*
     * if this request is completely handled and transfered to
     * userspace, remove its entry from the list and requeue it
     * again. Otherwise, we will revisit it again upon the next
     * call, taking into account its current read position.
     */
    if (list->pos == req->actual) {
        list_del(&list->list);
        free(list);

        req->length = my_hid->report_length;
        ret = usb_ep_queue(my_hid->out_ep, req);
        if (ret < 0) {
            printf("%s queue req --> %d\n", my_hid->out_ep->name, ret);
            free_ep_req(my_hid->out_ep, req);
        }
    } else {
        thread_waiter_wakeup(&my_hid->read_wait);
    }

    ret = count;
out:
    usb_spin_unlock_irqrestore(&my_hid->read_spinlock, flags);
    set_no_busy();
    return ret;
}

int gadget_hid_write(const uint8_t *buffer, uint32_t count, uint8_t block, uint32_t timeout_ms)
{
    int ret;
    uint32_t len = 0;
    unsigned long flags;
    struct usb_request *req;

    if (!count || buffer == NULL)
        return 0;

    if (check_set_busy())
        return -ENODEV;

    usb_spin_lock_irqsave(&my_hid->write_spinlock, flags);

    if (!my_hid->connect_flag) {
        ret = -ENOLINK;
        goto out;
    }

    /* write queue */
    while (my_hid->write_pending) {
        if (!block) {
            ret = -EAGAIN;
            goto out;
        }

        usb_spin_unlock_irqrestore(&my_hid->write_spinlock, flags);
        ret = thread_waiter_wait_timeout(&my_hid->write_wait, timeout_ms);
        usb_spin_lock_irqsave(&my_hid->write_spinlock, flags);

        if (my_hid->exit_flag) {
            ret = -ENODEV;
            goto out;
        }

        if (!my_hid->connect_flag) {
            ret = -ENOLINK;
            goto out;
        }

        if (ret) {
            ret = -ETIMEDOUT;
            goto out;
        }
    }

    my_hid->write_pending = 1;

    if (my_hid->req) {
        req = my_hid->req;
        len  = min_t(unsigned, count, my_hid->report_length);

        memcpy(req->buf, buffer, len);

        req->status   = 0;
        req->zero     = 0;
        req->length   = len;
        req->complete = f_hidg_req_complete;
        req->context  = my_hid;

        ret = usb_ep_queue(my_hid->in_ep, req);
        if (ret < 0) {
            my_hid->write_pending = 0;
            thread_waiter_wakeup(&my_hid->write_wait);
            printf("usb_ep_queue error on int endpoint %d\n", ret);
            goto out;
        }
    }

    ret = len;
out:
    usb_spin_unlock_irqrestore(&my_hid->write_spinlock, flags);
    set_no_busy();
    return ret;
}

int gadget_hid_get_connect_status(void)
{
    int status;

    os_enter_critical();

    if (my_hid == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    if (my_hid->connect_flag)
        status = 1;
    else
        status = 0;

    os_exit_critical();

    return status;
}

int gadget_hid_wait_connect(uint32_t timeout_ms)
{
    int ret;

    if (check_set_busy())
        return -ENODEV;

    while (!my_hid->connect_flag) {
        ret = thread_waiter_wait_timeout(&my_hid->connect_wait, timeout_ms);

        if (my_hid->exit_flag) {
            ret = -ENODEV;
            goto out;
        }

        if (ret) {
            ret = -ETIMEDOUT;
            goto out;
        }
    }

    ret = 0;
out:
    set_no_busy();
    return ret;
}
