#ifndef _GADGET_PRINTER_H_
#define _GADGET_PRINTER_H_

#include "gadget_common.h"

#ifdef CONFIG_USB_GADGET_PRINTER

#define PRINTER_STATUS_NOT_ERROR                0x08
#define PRINTER_STATUS_SELECTED                 0x10
#define PRINTER_STATUS_PAPER_EMPTY              0x20

/*
 printer device init 
    param:
        <id>  Manufacturer's device ID 
        <connect_cb>  USB connection and disconnection callback functions 
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_printer_init(const struct gadget_id *id, connect_callback_t connect_cb);

/* printer device cleanup */
extern void gadget_printer_cleanup(void);

/*
 printer read data
    param:
        <buffer>  Buffer for reading data 
        <len>  Buffer size 
        <block> Is it blocked 
        <timeout_ms> Block timeout
    return:
        <normal> Read Bytes 
        <abnormal> Error code 
 */
extern int gadget_printer_read(uint8_t *buffer, uint32_t len, uint8_t block, uint32_t timeout_ms);

/*
 generic bulk write data
    param:
        <buffer>  Buffer for writing data 
        <len>  Buffer size 
        <block> Is it blocked 
        <timeout_ms> Block timeout
    return:
        <normal> Write Bytes 
        <abnormal> Error code 
 */
extern int gadget_printer_write(const uint8_t *buffer, uint32_t len, uint8_t block, uint32_t timeout_ms);

/*
  Waiting for data to be sent from the write buffer 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_printer_flush_data(uint32_t timeout_ms);

/*
   Get Connection Status 
    return:
        <normal> Connection Status
        <abnormal> Error code 
 */
extern int gadget_printer_get_connect_status(void);

/*
   Waiting for usb link 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_printer_wait_connect(uint32_t timeout_ms);

/*
   Get Printer Status 
    param:
        <status> Get status pointer
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_printer_get_status(u8 *status);

/*
   Set Printer Status 
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_printer_set_status(u8 status);
#endif

#endif /* _GADGET_PRINTER_H_ */
