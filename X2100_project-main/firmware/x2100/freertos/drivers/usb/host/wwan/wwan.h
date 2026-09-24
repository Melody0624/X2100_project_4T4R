#ifndef __USB_USB_WWAN
#define __USB_USB_WWAN

#include <le_byteshift.h>
#include <include/types.h>

#include "../usb.h"
#include <include/usb/cdc.h>
#include <include/usb/host_wwan.h>

#define WWAN_MAX_URBS                   5
#define WWAN_MAX_PORTS                  16

#define WWAN_PAGE_SIZE                  4096
#define WWAN_IN_BUFLEN                  4096
#define WWAN_OUT_BUFLEN                 4096

/*
 * Output control lines.
 */
#define WWAN_CTRL_DTR        (1 << 0)
#define WWAN_CTRL_RTS        (1 << 1)

struct wwan *wwan;

struct usb_serial_port {
    struct wwan   *wwan;
    struct usb_device *dev;                /* the corresponding usb device */
    struct usb_wwan_port_private portdata;
    int           minor;
    u8            port_number;

    volatile bool opened;                /* someone has this wwan's port open */
    volatile bool exiting;
    volatile unsigned int used;
    wwan_notify_callback_t notify_callback;

    unsigned int in, out;                /* i/o pipes */
    u8            bulk_in_endpointAddress;
    u8            bulk_out_endpointAddress;

    unsigned char   *interrupt_in_buffer;
    struct urb      *interrupt_in_urb;
    u8              interrupt_in_endpointAddress;
};

struct usb_wwan_intf_private {
    unsigned int use_send_setup:1;
    unsigned int use_zlp:1;
};

struct usb_serial_endpoints {
    unsigned char num_bulk_in;
    unsigned char num_bulk_out;
    unsigned char num_interrupt_in;
    unsigned char num_interrupt_out;
    struct usb_endpoint_descriptor *bulk_in[WWAN_MAX_PORTS];
    struct usb_endpoint_descriptor *bulk_out[WWAN_MAX_PORTS];
    struct usb_endpoint_descriptor *interrupt_in[WWAN_MAX_PORTS];
    struct usb_endpoint_descriptor *interrupt_out[WWAN_MAX_PORTS];
};

struct wwan {
    struct usb_device *dev;                /* the corresponding usb device */
    struct usb_interface *intf;            /* data interface */
    struct usb_wwan_intf_private private;

    unsigned int             opened_port;
    unsigned char            num_port_pointers;
    unsigned char            num_interrupt_in;
    unsigned char            num_interrupt_out;
    unsigned char            num_bulk_in;
    unsigned char            num_bulk_out;
    struct usb_serial_port        *port[WWAN_MAX_PORTS];
};

#endif /* __USB_USB_WWAN */
