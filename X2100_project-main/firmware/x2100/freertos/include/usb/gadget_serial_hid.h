#ifndef _GADGET_SERIAL_HID_H_
#define _GADGET_SERIAL_HID_H_

#include "gadget_common.h"

#ifdef CONFIG_USB_GADGET_SERIAL_HID

#define USB_CDC_1_STOP_BITS            0
#define USB_CDC_1_5_STOP_BITS            1
#define USB_CDC_2_STOP_BITS            2

#define USB_CDC_NO_PARITY            0
#define USB_CDC_ODD_PARITY            1
#define USB_CDC_EVEN_PARITY            2
#define USB_CDC_MARK_PARITY            3
#define USB_CDC_SPACE_PARITY            4

struct usb_cdc_serial_param {
    u32    dwDTERate;       /* Baud rate */
    u8    bCharFormat;      /* Stop bit */
    u8    bParityType;      /* Check mode */
    u8    bDataBits;        /* Data bits */
};

struct hid_report_descriptor {
    uint8_t     subclass;
    uint8_t     protocol;
    unsigned short      report_length;
    unsigned short      report_desc_length;
    const uint8_t     *report_desc;
};

typedef void (*serial_param_callback_t)(struct usb_cdc_serial_param *p);
typedef int (*hid_request_callback_t)(void *data, u16 *length, int request);

struct cdc_hid_callback {
    connect_callback_t hid_connect_cb; /* hid connect callback */
    hid_request_callback_t hid_request_cb; /* hid request callback */
    connect_callback_t cdc_connect_cb; /* serial connect callback */
    serial_param_callback_t serial_cb; /* serial param change callback */
};

/*
 serial hid device init 
    param:
        <id>  Manufacturer's device ID 
        <report> USB HID report
        <param> USB CDC serial param
        <callback>  serial hid callback 
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_serial_hid_init(const struct gadget_id *id, const struct hid_report_descriptor *report, 
        const struct usb_cdc_serial_param *param, const struct cdc_hid_callback* callback);

/* serial hid device cleanup */
extern void gadget_serial_hid_cleanup(void);

/* sent serial break event */
extern int gadget_serial_break_ctl(int duration);

/*
  Waiting for serial data to be sent from the write buffer 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_serial_flush_chars(uint32_t timeout_ms);

/*
  Clear serial write buffer data
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_serial_clean_write(void);

/*
 serial read data
    param:
        <buffer>  Buffer for reading data 
        <count>  Buffer size 
        <block> Is it blocked 
        <timeout_ms> Block timeout
    return:
        <normal> Read Bytes 
        <abnormal> Error code 
 */
extern int gadget_serial_read(uint8_t *buf, uint32_t count, uint8_t block, uint32_t timeout_ms);

/*
 serial write data
    param:
        <buffer>  Buffer for writing data 
        <count>  Buffer size 
        <block> Is it blocked 
        <timeout_ms> Block timeout
    return:
        <normal> Write Bytes 
        <abnormal> Error code 
 */
extern int gadget_serial_write(const uint8_t *buf, uint32_t count, uint32_t block, uint32_t timeout_ms);

/*
   Get Serial Connection Status 
    return:
        <normal> Connection Status
        <abnormal> Error code 
 */
extern int gadget_serial_get_connect_status(void);

/*
   Waiting for serial link 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_serial_wait_connect(uint32_t timeout_ms);

/*
   Get Serial Param 
    param:
        <cdc_param> serial param pointer
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_serial_get_param(struct usb_cdc_serial_param *cdc_param);

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
   Get HID Connection Status 
    return:
        <normal> Connection Status
        <abnormal> Error code 
 */
extern int gadget_hid_get_connect_status(void);

/*
   Waiting for hid link 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_hid_wait_connect(uint32_t timeout_ms);
#endif

#endif /* _GADGET_SERIAL_HID_H_ */
