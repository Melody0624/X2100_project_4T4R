#ifndef _GADGET_HID_H_
#define _GADGET_HID_H_

#include "gadget_common.h"

#ifdef CONFIG_USB_GADGET_HID

struct usb_function;

typedef int (*hid_request_callback_t)(void *data, u16 *length, int request);

struct hid_callback {
    connect_callback_t connect_cb;
    hid_request_callback_t request_cb;
};

struct hid_report_descriptor {
    uint8_t     subclass;
    uint8_t     protocol;
    unsigned short      report_length;
    unsigned short      report_desc_length;
    const uint8_t     *report_desc;
};

/*
 hid device init 
    param:
        <id>  Manufacturer's device ID 
        <report> USB HID report
        <hid_cb> USB HID callback function collection
            <connect_cb>  USB connection and disconnection callback functions
            <request_cb>  USB HID Class-Specific Requests callback functions
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_hid_init(const struct gadget_id *id, const struct hid_report_descriptor *report, struct hid_callback *hid_cb);

/* hid device cleanup */
extern void gadget_hid_cleanup(void);

/*
 hid read data
    param:
        <buffer>  Buffer for reading data 
        <count>  Buffer size 
        <block> Is it blocked 
        <timeout_ms> Block timeout
    return:
        <normal> Read Bytes 
        <abnormal> Error code 
 */
extern int gadget_hid_read(uint8_t *buffer, uint32_t count, uint8_t block, uint32_t timeout_ms);

/*
 hid write data
    param:
        <buffer>  Buffer for writing data 
        <count>  Buffer size 
        <block> Is it blocked 
        <timeout_ms> Block timeout
    return:
        <normal> Write Bytes 
        <abnormal> Error code 
 */
extern int gadget_hid_write(const uint8_t *buffer, uint32_t count, uint8_t block, uint32_t timeout_ms);

/*
   Get Connection Status 
    return:
        <normal> Connection Status
        <abnormal> Error code 
 */
extern int gadget_hid_get_connect_status(void);

/*
   Waiting for usb link 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_hid_wait_connect(uint32_t timeout_ms);


int gadget_hid_init2(const struct gadget_id *id,
 const struct hid_report_descriptor *report0, struct hid_callback *hid_cb0,
 const struct hid_report_descriptor *report1, struct hid_callback *hid_cb1);
extern void gadget_hid_cleanup2(void);

void gadget_hid_get2(struct usb_function **hid0, struct usb_function **hid1);

extern int gadget_hid_read2(struct usb_function *hid, uint8_t *buffer, uint32_t count, uint8_t block, uint32_t timeout_ms);
extern int gadget_hid_write2(struct usb_function *hid, const uint8_t *buffer, uint32_t count, uint8_t block, uint32_t timeout_ms);
extern int gadget_hid_get_connect_status2(struct usb_function *hid);
extern int gadget_hid_wait_connect2(struct usb_function *hid, uint32_t timeout_ms);

#endif

#endif /* _GADGET_HID_H_ */
