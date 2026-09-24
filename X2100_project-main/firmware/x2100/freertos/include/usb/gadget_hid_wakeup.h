#ifndef _GADGET_HID_WAKEUP_H_
#define _GADGET_HID_WAKEUP_H_

#include "gadget_common.h"

#ifdef CONFIG_USB_GADGET_HID_WAKEUP

struct hid_report_descriptor {
    uint8_t     subclass;
    uint8_t     protocol;
    unsigned short      report_length;
    unsigned short      report_desc_length;
    const uint8_t     *report_desc;
};

typedef int (*hid_request_callback_t)(void *data, u16 *length, int request);

struct hid_callback {
    connect_callback_t connect_cb;
    hid_request_callback_t request_cb;
};

/*
 hid device init 
    param:
        <id>  Manufacturer's device ID 
        <report> USB HID report
        <hid_cb> USB HID callback function collection
            <connect_cb>  USB connection and disconnection callback functions
            <request_cb>  USB HID Class-Specific Requests callback functions
        <suspend_cb>  USB suspend and resume callback functions 
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_hid_wakeup_init(const struct gadget_id *id, const struct hid_report_descriptor *report,
        struct hid_callback *hid_cb, suspend_callback_t suspend_cb);

/* hid device cleanup */
extern void gadget_hid_wakeup_cleanup(void);

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

/*
   Get Suspend Status 
    return:
        <normal> Suspend Status
        <abnormal> Error code 
 */
extern int gadget_hid_get_suspend_status(void);

/*
   Waiting for usb resume 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_hid_wait_resume(uint32_t timeout_ms);

/*
   Wakeup usb host
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_hid_wakeup(void);
#endif

#endif /* _GADGET_HID_WAKEUP_H_ */
