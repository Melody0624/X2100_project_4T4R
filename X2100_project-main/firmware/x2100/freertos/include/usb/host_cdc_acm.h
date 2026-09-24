#ifndef _HOST_CDC_ACM_H_
#define _HOST_CDC_ACM_H_

#include <common.h>

#ifdef CONFIG_USB_HOST_CDC_ACM

#define ACM_MINORS        32

#define USB_CDC_1_STOP_BITS            0
#define USB_CDC_1_5_STOP_BITS            1
#define USB_CDC_2_STOP_BITS            2

#define USB_CDC_NO_PARITY            0
#define USB_CDC_ODD_PARITY            1
#define USB_CDC_EVEN_PARITY            2
#define USB_CDC_MARK_PARITY            3
#define USB_CDC_SPACE_PARITY            4

/*
 * Output control lines.
 */

#define ACM_CTRL_DTR        0x01
#define ACM_CTRL_RTS        0x02

/*
 * Input control lines and line errors.
 */

#define ACM_CTRL_DCD        0x01
#define ACM_CTRL_DSR        0x02
#define ACM_CTRL_BRK        0x04
#define ACM_CTRL_RI        0x08

#define ACM_CTRL_FRAMING    0x10
#define ACM_CTRL_PARITY        0x20
#define ACM_CTRL_OVERRUN    0x40

struct cdc_acm_line_coding {
    u32    dwDTERate;       /* Baud rate */
    u8    bCharFormat;      /* Stop bit */
    u8    bParityType;      /* Check mode */
    u8    bDataBits;        /* Data bits */
};

/* usb host cdc acm supports up to 32 devices, each bit represents a device */
typedef void (*cdc_acm_device_callback_t)(u32 devices_bit);

/* When a CDC ACM device is inserted or removed, callback will be triggered to update the state */
extern void usb_host_cdc_acm_register_callback(cdc_acm_device_callback_t callback);
extern u32 usb_host_cdc_acm_get_devices_bit(void);

typedef void (*cdc_acm_notify_callback_t)(u8 id, void *buf, int length);

/*
 open cdc acm device
    param:
        <id> cdc acm device index
        <callback> cdc acm notify callback
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int usb_host_cdc_acm_open(u8 id, cdc_acm_notify_callback_t callback);

/*
 close cdc acm device
    param:
        <id> cdc acm device index
    return:
        <normal> 0
        <abnormal> Error code 
    note:
        Successfully opened devices must be manually closed 
 */
extern void usb_host_cdc_acm_close(u8 id);

/*
 set cdc acm line coding
    param:
        <id> cdc acm device index
        <param> line coding
        <timeout> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int usb_host_cdc_acm_set_line_coding(u8 id, struct cdc_acm_line_coding *param, int timeout);

/*
 set cdc acm flow control 
    param:
        <id> cdc acm device index
        <ctrlout> flow control 
        <timeout> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int usb_host_cdc_acm_set_control(u8 id, u16 ctrlout, int timeout);

/*
 sent cdc break event
    param:
        <id> cdc acm device index
        <state> break state 
        <timeout> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int usb_host_cdc_acm_send_break(u8 id, int state, int timeout);

/*
 usb host cdc acm read data
    param:
        <id> cdc acm device index
        <data>  Buffer for reading data 
        <len>  Buffer size 
        <timeout> Block timeout
    return:
        <normal> Read Bytes 
        <abnormal> Error code 
 */
extern int usb_host_cdc_acm_read(u8 id, void *data, int len, int timeout);

/*
 usb host cdc acm write data
    param:
        <id> cdc acm device index
        <data>  Buffer for writing data 
        <len>  Buffer size 
        <timeout> Block timeout
    return:
        <normal> Write Bytes 
        <abnormal> Error code 
 */
extern int usb_host_cdc_acm_write(u8 id, void *data, int len, int timeout);

#endif

#endif /* _HOST_CDC_ACM_H_ */
