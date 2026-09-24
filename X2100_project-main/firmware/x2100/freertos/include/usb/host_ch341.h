#ifndef _HOST_CH341_H_
#define _HOST_CH341_H_

#include <stdio.h>
#include <common.h>

#ifdef CONFIG_USB_HOST_CH341

#define CH341_MINORS        32

#define USB_CH341_1_STOP_BITS    0
#define USB_CH341_1_5_STOP_BITS 1
#define USB_CH341_2_STOP_BITS    2

#define USB_CH341_NO_PARITY    0
#define USB_CH341_ODD_PARITY   1
#define USB_CH341_EVEN_PARITY  2
#define USB_CH341_MARK_PARITY  3
#define USB_CH341_SPACE_PARITY 4

/*
 * Output control lines.
 */
#define CH341_CTO_O   0x10
#define CH341_CTO_D   0x20        /* dtr */
#define CH341_CTO_R   0x40        /* rts */

/*
 * Input control lines.
 */
#define CH341_CTI_C   0x01        /* cts */
#define CH341_CTI_DS  0x02        /* dsr */
#define CH341_CTRL_RI 0x04        /* ring */
#define CH341_CTI_DC  0x08        /* dcd */
#define CH341_CTI_ST  0x0f

#define CH341_CTT_M BIT(3)
#define CH341_CTT_F (BIT(2) | BIT(6))
#define CH341_CTT_P BIT(2)
#define CH341_CTT_O BIT(1)

struct ch341_line_coding {
    u32   dwDTERate;        /* Baud rate */
    u8    bCharFormat;      /* Stop bit */
    u8    bParityType;      /* Check mode */
    u8    bDataBits;        /* Data bits */
    u8    hardflow;         /* hardflow */
};

/* usb host ch341 supports up to 32 devices, each bit represents a device */
typedef void (*ch341_device_callback_t)(u32 devices_bit);

/* When a ch341 device is inserted or removed, callback will be triggered to update the state */
extern void usb_host_ch341_register_callback(ch341_device_callback_t callback);
extern u32 usb_host_ch341_get_devices_bit(void);

typedef void (*ch341_notify_callback_t)(u8 id, u8 *buf, int length);

/*
 open ch341 device
    param:
        <id> ch341 device index
        <callback> ch341 notify callback
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int usb_host_ch341_open(u8 id, ch341_notify_callback_t callback);

/*
 close ch341 device
    param:
        <id> ch341 device index
    return:
        <normal> 0
        <abnormal> Error code 
    note:
        Successfully opened devices must be manually closed 
 */
extern void usb_host_ch341_close(u8 id);

/*
 set ch341 line coding
    param:
        <id> ch341 device index
        <param> line coding
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int usb_host_ch341_set_line_coding(u8 id, struct ch341_line_coding *param);

/*
 set ch341 flow control 
    param:
        <id> ch341 device index
        <ctrlout> flow control 
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int usb_host_ch341_set_control(u8 id, u16 ctrlout);

/*
 sent ch341 break event
    param:
        <id> ch341 device index
        <state> break state 
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int usb_host_ch341_send_break(u8 id, int state);

/*
 usb host ch341 read data
    param:
        <id> ch341 device index
        <data>  Buffer for reading data 
        <len>  Buffer size 
        <timeout> Block timeout
    return:
        <normal> Read Bytes 
        <abnormal> Error code 
 */
extern int usb_host_ch341_read(u8 id, void *data, int len, int timeout);

/*
 usb host ch341 write data
    param:
        <id> ch341 device index
        <data>  Buffer for writing data 
        <len>  Buffer size 
        <timeout> Block timeout
    return:
        <normal> Write Bytes 
        <abnormal> Error code 
 */
extern int usb_host_ch341_write(u8 id, void *data, int len, int timeout);

#endif

#endif /* _HOST_CH341_H_ */
