#ifndef __HID_H
#define __HID_H

#include "../../usb_lock.h"

/*
 * USB HID (Human Interface Device) interface class code
 */

#define USB_INTERFACE_CLASS_HID        3

/*
 * USB HID interface subclass and protocol codes
 */

#define USB_INTERFACE_SUBCLASS_BOOT    1
#define USB_INTERFACE_PROTOCOL_KEYBOARD    1
#define USB_INTERFACE_PROTOCOL_MOUSE    2

/*
 * HID class requests
 */

#define HID_REQ_GET_REPORT        0x01
#define HID_REQ_GET_IDLE        0x02
#define HID_REQ_GET_PROTOCOL        0x03
#define HID_REQ_SET_REPORT        0x09
#define HID_REQ_SET_IDLE        0x0A
#define HID_REQ_SET_PROTOCOL        0x0B

/*
 * HID class descriptor types
 */

#define HID_DT_HID            (USB_TYPE_CLASS | 0x01)
#define HID_DT_REPORT            (USB_TYPE_CLASS | 0x02)
#define HID_DT_PHYSICAL            (USB_TYPE_CLASS | 0x03)

#define HID_MAX_DESCRIPTOR_SIZE        4096

/*
 * HID protocol status
 */
#define HID_REPORT_PROTOCOL    1
#define HID_BOOT_PROTOCOL    0

struct hid_class_descriptor {
    u8  bDescriptorType;
    u16 wDescriptorLength;
} __attribute__ ((packed));

struct hid_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u16 bcdHID;
    u8  bCountryCode;
    u8  bNumDescriptors;

    struct hid_class_descriptor desc[1];
} __attribute__ ((packed));

/*-------------------------------------------------------------------------*/
/*                            HID gadget struct                            */

struct f_hidg_req_list {
    struct usb_request    *req;
    uint32_t        pos;
    struct list_head     list;
};

struct f_hidg {
    /* configuration */
    uint8_t            bInterfaceSubClass;
    uint8_t            bInterfaceProtocol;
    uint8_t            protocol;
    unsigned short            report_desc_length;
    uint8_t                *report_desc;
    unsigned short            report_length;

    /* recv report */
    spinlock_t            read_spinlock;
    struct list_head        completed_out_req;
    thread_waiter_t        read_wait;

    /* send report */
    spinlock_t            write_spinlock;
    bool                write_pending;
    thread_waiter_t        write_wait;
    struct usb_request        *req;

    struct usb_function        func;

    struct usb_ep            *in_ep;
    struct usb_ep            *out_ep;

    uint32_t             ops_busy;
    uint8_t        exit_flag;
    thread_waiter_t    exit_wait;

    uint8_t connect_flag;
    u8 interface_id;

    connect_callback_t connect_cb;
    hid_request_callback_t request_cb;
    thread_waiter_t connect_wait;
};


static inline struct f_hidg *func_to_hidg(struct usb_function *f)
{
    return container_of(f, struct f_hidg, func);
}

struct usb_function *hid_device_alloc(const struct hid_report_descriptor *hid_report, connect_callback_t connect_cb, hid_request_callback_t request_cb);
void hid_device_free(void);

#endif /* __HID_H */
