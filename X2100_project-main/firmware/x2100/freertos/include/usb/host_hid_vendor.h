#ifndef _HOST_HID_VENDOR_H_
#define _HOST_HID_VENDOR_H_

#include <errno.h>
#include <common.h>
#include <os.h>

#ifdef CONFIG_USB_HOST_HID_VENDOR

#define HID_VENDOR_MINORS               32

/* usb host hid_vendor supports up to 32 devices, each bit represents a device */
typedef void (*hid_vendor_device_callback_t)(u32 devices_bit);

/**
 * @callback:plug and unplug hid_vendor device callback function
*/
void usb_host_hid_vendor_register_callback(hid_vendor_device_callback_t callback);

/**
 * @id: hid_vendor device index
*/
int usb_host_hid_vendor_open(u8 id);

/**
 * @id: hid_vendor device index
*/
int usb_hid_vendor_get_in_size(u8 id);

/**
 * @id:hid_vendor device index
*/
int usb_hid_vendor_get_out_size(u8 id);

/**
 * @id:hid_vendor device index
 * @buf:buf for reading data
 * @len:the lenght of buf
 * @timeout:block timeout
*/
int usb_host_hid_vendor_read(u8 id, void *buf, int len, int timeout);

/**
 * @id:hid_vendor device index
 * @data:data to be written
 * @len:the lenght of data
 * @timeout:block timeout
*/
int usb_host_hid_vendor_write(u8 id, void *data, int len, int timeout);

/**
 * @id:hid_vendor device index
*/
void usb_host_hid_vendor_close(u8 id);

#endif

#endif /* _HOST_HID_VENDOR_H_ */