// SPDX-License-Identifier: GPL-2.0+
/*
 * u_serial.h - interface to USB gadget "serial port"/TTY utilities
 *
 * Copyright (C) 2008 David Brownell
 * Copyright (C) 2008 by Nokia Corporation
 */

#ifndef __U_SERIAL_H
#define __U_SERIAL_H

#include <usb/gadget_serial.h>
#include <usb/cdc.h>
#include "../composite.h"

/*
 * One non-multiplexed "serial" I/O port ... there can be several of these
 * on any given USB peripheral device, if it provides enough endpoints.
 *
 * The "u_serial" utility component exists to do one thing:  manage TTY
 * style I/O using the USB peripheral endpoints listed here, including
 * hookups to sysfs and /dev for each logical "tty" device.
 *
 * REVISIT at least ACM could support tiocmget() if needed.
 *
 * REVISIT someday, allow multiplexing several TTYs over these endpoints.
 */
struct gserial {
	struct usb_function		func;

	/* port is managed by gserial_{connect,disconnect} */
	struct gs_port			*ioport;

	struct usb_ep			*in;
	struct usb_ep			*out;

	/* notification callbacks */
	void (*connect)(struct gserial *p);
	void (*disconnect)(struct gserial *p);
	int (*send_break)(struct gserial *p, int duration);
};

/* utilities to allocate/free request and buffer */
struct usb_request *gs_alloc_req(struct usb_ep *ep, unsigned len);
void gs_free_req(struct usb_ep *, struct usb_request *req);

/* management of individual TTY ports */
int gserial_alloc_line(struct usb_cdc_serial_param *serial_param, connect_callback_t connect_cb, serial_param_callback_t serial_cb);
void gserial_free_line(void);

/* connect/disconnect is handled by individual functions */
int gserial_connect(struct gserial *);
void gserial_disconnect(struct gserial *);

void gs_port_update_conding(struct usb_cdc_line_coding *coding);

#endif /* __U_SERIAL_H */
