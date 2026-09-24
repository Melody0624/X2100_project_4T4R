#ifndef _GADGET_BULK_H_
#define _GADGET_BULK_H_

#include "gadget_common.h"
#include "u_os_desc.h"

#ifdef CONFIG_USB_GADGET_WINUSB

struct winusb_ext_prop {
    u8 type;
    const char *name;
    const void *data;
    int data_len;
};

struct winusb_descriptor {
    u8 qw_sign[14];
    u8 vendor_code;

    char ext_compat_id[16];
    u32 ext_prop_count;
    struct winusb_ext_prop *ext_prop;
};

/*
 winusb device init 
    param:
        <id>  Manufacturer's device ID 
        <connect_cb>  USB connection and disconnection callback functions 
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_winusb_init(const struct gadget_id *id, struct winusb_descriptor *des, connect_callback_t connect_cb);

/* winusb device cleanup */
extern void gadget_winusb_cleanup(void);

/*
 winusb read data
    param:
        <buffer>  Buffer for reading data 
        <len>  Buffer size 
        <block> Is it blocked 
        <timeout_ms> Block timeout
    return:
        <normal> Read Bytes 
        <abnormal> Error code 
 */
extern int gadget_winusb_read(uint8_t *buffer, uint32_t len, uint8_t block, uint32_t timeout_ms);

/*
 winusb write data
    param:
        <buffer>  Buffer for writing data 
        <len>  Buffer size 
        <block> Is it blocked 
        <timeout_ms> Block timeout
    return:
        <normal> Write Bytes 
        <abnormal> Error code 
 */
extern int gadget_winusb_write(const uint8_t *buffer, uint32_t len, uint8_t block, uint32_t timeout_ms);

/*
  Waiting for data to be sent from the write buffer 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_winusb_flush_data(uint32_t timeout_ms);

/*
   Get Connection Status 
    return:
        <normal> Connection Status
        <abnormal> Error code 
 */
extern int gadget_winusb_get_connect_status(void);

/*
   Waiting for usb link 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_winusb_wait_connect(uint32_t timeout_ms);
#endif

#endif /* _GADGET_BULK_H_ */
