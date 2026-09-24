#ifndef _GADGET_SERIAL_H_
#define _GADGET_SERIAL_H_

#include "gadget_common.h"

#ifdef CONFIG_USB_GADGET_SERIAL

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

/* serial param change callback */
typedef void (*serial_param_callback_t)(struct usb_cdc_serial_param *p);

/*
 serial device init 
    param:
        <id>  Manufacturer's device ID 
        <param> USB CDC serial param
        <connect_cb> serial hid callback 
        <serial_cb> serial param change callback
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_serial_init(const struct gadget_id *id, const struct usb_cdc_serial_param *param, 
        connect_callback_t connect_cb, serial_param_callback_t serial_cb);

/* serial hid device cleanup */
extern void gadget_serial_cleanup(void);

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
  Clear write buffer data
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
   Reset the internal read_wait state after suspending the USB receive task.
   This is needed when OTA takes over the USB serial for exclusive use,
   because the suspended USB receive thread may have left read_wait.thread
   in a non-NULL state, causing an assertion failure in thread_waiter_wait_timeout.
 */
extern void gadget_serial_reset_read_wait(void);

/*
   Reset the internal write_wait state after suspending the USB send task.
   This is needed when OTA takes over the USB serial for exclusive use,
   because the suspended USB send thread may have left write_wait.thread
   in a non-NULL state, causing an assertion failure in thread_waiter_wait_timeout.
 */
extern void gadget_serial_reset_write_wait(void);

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
   Check DTR Status 
    return:
        <normal> 1
        <abnormal> 0
 */
extern int gadget_serial_is_dtr_set(void); // 判断DTR是否被设置
#endif

#endif /* _GADGET_SERIAL_H_ */
