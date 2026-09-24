#ifndef _GADGET_BULK_H_
#define _GADGET_BULK_H_

#include "gadget_common.h"

#ifdef CONFIG_USB_GADGET_BULK

/*
 generic bulk device init 
    param:
        <id>  Manufacturer's device ID 
        <connect_cb>  USB connection and disconnection callback functions 
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_bulk_init(const struct gadget_id *id, connect_callback_t connect_cb);

/* generic bulk device cleanup */
extern void gadget_bulk_cleanup(void);

/*
 generic bulk read data
    param:
        <buffer>  Buffer for reading data 
        <len>  Buffer size 
        <block> Is it blocked 
        <timeout_ms> Block timeout
    return:
        <normal> Read Bytes 
        <abnormal> Error code 
 */
extern int gadget_bulk_read(uint8_t *buffer, uint32_t len, uint8_t block, uint32_t timeout_ms);

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
extern int gadget_bulk_write(const uint8_t *buffer, uint32_t len, uint8_t block, uint32_t timeout_ms);

/*
  Waiting for data to be sent from the write buffer 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_bulk_flush_data(uint32_t timeout_ms);

/*
   Get Connection Status 
    return:
        <normal> Connection Status
        <abnormal> Error code 
 */
extern int gadget_bulk_get_connect_status(void);

/*
   Waiting for usb link 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_bulk_wait_connect(uint32_t timeout_ms);
#endif

#endif /* _GADGET_BULK_H_ */
