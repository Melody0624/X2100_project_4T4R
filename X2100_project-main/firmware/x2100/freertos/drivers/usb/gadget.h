#ifndef _USB_GADGET_H
#define _USB_GADGET_H

#include <little_things.h>
#include <list.h>
#include <common.h>
#include <malloc.h>
#include <usb/ch9.h>

#include "hw.h"
#include "otg.h"

#define UDC_TRACE_STR_MAX	512
#define CONFIG_USB_GADGET_VBUS_DRAW			200

struct usb_ep;

/**
 * struct usb_request - describes one i/o request
 * @buf: Buffer used for data.  Always provide this; some controllers
 *	only use PIO, or don't use DMA for some endpoints.
 * @dma: DMA address corresponding to 'buf'.  If you don't set this
 *	field, and the usb controller needs one, it is responsible
 *	for mapping and unmapping the buffer.
 * @length: Length of that data
 * @no_interrupt: If true, hints that no completion irq is needed.
 *	Helpful sometimes with deep request queues that are handled
 *	directly by DMA controllers.
 * @zero: If true, when writing data, makes the last packet be "short"
 *     by adding a zero length packet as needed;
 * @short_not_ok: When reading data, makes short packets be
 *     treated as errors (queue stops advancing till cleanup).
 * @dma_mapped: Indicates if request has been mapped to DMA (internal)
 * @complete: Function called when request completes, so this request and
 *	its buffer may be re-used.  The function will always be called with
 *	interrupts disabled, and it must not sleep.
 *	Reads terminate with a short packet, or when the buffer fills,
 *	whichever comes first.  When writes terminate, some data bytes
 *	will usually still be in flight (often in a hardware fifo).
 *	Errors (for reads or writes) stop the queue from advancing
 *	until the completion function returns, so that any transfers
 *	invalidated by the error may first be dequeued.
 * @context: For use by the completion callback
 * @list: For use by the gadget driver.
 * @frame_number: Reports the interval number in (micro)frame in which the
 *	isochronous transfer was transmitted or received.
 * @status: Reports completion code, zero or a negative errno.
 *	Normally, faults block the transfer queue from advancing until
 *	the completion callback returns.
 *	Code "-ESHUTDOWN" indicates completion caused by device disconnect,
 *	or when the driver disabled the endpoint.
 * @actual: Reports bytes transferred to/from the buffer.  For reads (OUT
 *	transfers) this may be less than the requested length.  If the
 *	short_not_ok flag is set, short reads are treated as errors
 *	even when status otherwise indicates successful completion.
 *	Note that for writes (IN transfers) some data bytes may still
 *	reside in a device-side FIFO when the request is reported as
 *	complete.
 *
 * These are allocated/freed through the endpoint they're used with.  The
 * hardware's driver can add extra per-request data to the memory it returns,
 * which often avoids separate memory allocations (potential failures),
 * later when the request is queued.
 *
 * Request flags affect request handling, such as whether a zero length
 * packet is written (the "zero" flag), whether a short read should be
 * treated as an error (blocking request queue advance, the "short_not_ok"
 * flag), or hinting that an interrupt is not required (the "no_interrupt"
 * flag, for use with deep request queues).
 *
 * Bulk endpoints can use any size buffers, and can also be used for interrupt
 * transfers. interrupt-only endpoints can be much less functional.
 *
 * NOTE:  this is analogous to 'struct urb' on the host side, except that
 * it's thinner and promotes more pre-allocation.
 */

struct usb_request {
	void			*buf;
	unsigned		length;
	dma_addr_t		dma;

	unsigned		no_interrupt:1;
	unsigned		zero:1;
	unsigned		short_not_ok:1;
	unsigned		dma_mapped:1;

	void			(*complete)(struct usb_ep *ep,
					struct usb_request *req);
	void			*context;
	struct list_head	list;

	unsigned		frame_number;		/* ISO ONLY */

	int			status;
	unsigned		actual;
};

/*-------------------------------------------------------------------------*/

/* endpoint-specific parts of the api to the usb controller hardware.
 * unlike the urb model, (de)multiplexing layers are not required.
 * (so this api could slash overhead if used on the host side...)
 *
 * note that device side usb controllers commonly differ in how many
 * endpoints they support, as well as their capabilities.
 */
struct usb_ep_ops {
	int (*enable) (struct usb_ep *ep,
		const struct usb_endpoint_descriptor *desc);
	int (*disable) (struct usb_ep *ep);
	void (*dispose) (struct usb_ep *ep);

	struct usb_request *(*alloc_request) (struct usb_ep *ep);
	void (*free_request) (struct usb_ep *ep, struct usb_request *req);

	int (*queue) (struct usb_ep *ep, struct usb_request *req);
	int (*dequeue) (struct usb_ep *ep, struct usb_request *req);

	int (*set_halt) (struct usb_ep *ep, int value);
	int (*set_wedge) (struct usb_ep *ep);

	int (*fifo_status) (struct usb_ep *ep);
	void (*fifo_flush) (struct usb_ep *ep);
};

/**
 * struct usb_ep_caps - endpoint capabilities description
 * @type_control:Endpoint supports control type (reserved for ep0).
 * @type_iso:Endpoint supports isochronous transfers.
 * @type_bulk:Endpoint supports bulk transfers.
 * @type_int:Endpoint supports interrupt transfers.
 * @dir_in:Endpoint supports IN direction.
 * @dir_out:Endpoint supports OUT direction.
 */
struct usb_ep_caps {
	unsigned type_control:1;
	unsigned type_iso:1;
	unsigned type_bulk:1;
	unsigned type_int:1;
	unsigned dir_in:1;
	unsigned dir_out:1;
};

#define USB_EP_CAPS_TYPE_CONTROL     0x01
#define USB_EP_CAPS_TYPE_ISO         0x02
#define USB_EP_CAPS_TYPE_BULK        0x04
#define USB_EP_CAPS_TYPE_INT         0x08
#define USB_EP_CAPS_TYPE_ALL \
	(USB_EP_CAPS_TYPE_ISO | USB_EP_CAPS_TYPE_BULK | USB_EP_CAPS_TYPE_INT)
#define USB_EP_CAPS_DIR_IN           0x01
#define USB_EP_CAPS_DIR_OUT          0x02
#define USB_EP_CAPS_DIR_ALL  (USB_EP_CAPS_DIR_IN | USB_EP_CAPS_DIR_OUT)

#define USB_EP_CAPS(_type, _dir) \
	{ \
		.type_control = !!(_type & USB_EP_CAPS_TYPE_CONTROL), \
		.type_iso = !!(_type & USB_EP_CAPS_TYPE_ISO), \
		.type_bulk = !!(_type & USB_EP_CAPS_TYPE_BULK), \
		.type_int = !!(_type & USB_EP_CAPS_TYPE_INT), \
		.dir_in = !!(_dir & USB_EP_CAPS_DIR_IN), \
		.dir_out = !!(_dir & USB_EP_CAPS_DIR_OUT), \
	}

/**
 * struct usb_ep - device side representation of USB endpoint
 * @name:identifier for the endpoint, such as "ep-a" or "ep9in-bulk"
 * @ops: Function pointers used to access hardware-specific operations.
 * @ep_list:the gadget's ep_list holds all of its endpoints
 * @caps:The structure describing types and directions supported by endpoint.
 * @enabled: The current endpoint enabled/disabled state.
 * @claimed: True if this endpoint is claimed by a function.
 * @maxpacket:The maximum packet size used on this endpoint.  The initial
 *	value can sometimes be reduced (hardware allowing), according to
 *	the endpoint descriptor used to configure the endpoint.
 * @maxpacket_limit:The maximum packet size value which can be handled by this
 *	endpoint. It's set once by UDC driver when endpoint is initialized, and
 *	should not be changed. Should not be confused with maxpacket.
 * @max_streams: The maximum number of streams supported
 *	by this EP (0 - 16, actual number is 2^n)
 * @mult: multiplier, 'mult' value for SS Isoc EPs
 * @driver_data:for use by the gadget driver.
 * @address: used to identify the endpoint when finding descriptor that
 *	matches connection speed
 * @desc: endpoint descriptor.  This pointer is set before the endpoint is
 *	enabled and remains valid until the endpoint is disabled.
 *
 * the bus controller driver lists all the general purpose endpoints in
 * gadget->ep_list.  the control endpoint (gadget->ep0) is not in that list,
 * and is accessed only in response to a driver setup() callback.
 */

struct usb_ep {
	void			*driver_data;

	const char		*name;
	const struct usb_ep_ops	*ops;
	struct list_head	ep_list;
	struct usb_ep_caps	caps;
	bool			claimed;
	bool			enabled;
	unsigned		maxpacket:16;
	unsigned		maxpacket_limit:16;
	unsigned		max_streams:16;
	unsigned		mult:2;
	u8			address;
	const struct usb_endpoint_descriptor	*desc;

	void (*set_halt) (struct usb_ep *ep, int value);
	void			*context;
};

/*-------------------------------------------------------------------------*/

#if defined(CONFIG_USB_IS_DEVICE)
void usb_ep_set_maxpacket_limit(struct usb_ep *ep, unsigned maxpacket_limit);
int usb_ep_enable(struct usb_ep *ep);
int usb_ep_disable(struct usb_ep *ep);
struct usb_request *usb_ep_alloc_request(struct usb_ep *ep);
void usb_ep_free_request(struct usb_ep *ep, struct usb_request *req);
int usb_ep_queue(struct usb_ep *ep, struct usb_request *req);
int usb_ep_dequeue(struct usb_ep *ep, struct usb_request *req);
int usb_ep_set_halt(struct usb_ep *ep);
int usb_ep_clear_halt(struct usb_ep *ep);
int usb_ep_set_wedge(struct usb_ep *ep);
int usb_ep_fifo_status(struct usb_ep *ep);
void usb_ep_fifo_flush(struct usb_ep *ep);
#else
static inline void usb_ep_set_maxpacket_limit(struct usb_ep *ep,
		unsigned maxpacket_limit)
{ }
static inline int usb_ep_enable(struct usb_ep *ep)
{ return 0; }
static inline int usb_ep_disable(struct usb_ep *ep)
{ return 0; }
static inline struct usb_request *usb_ep_alloc_request(struct usb_ep *ep)
{ return NULL; }
static inline void usb_ep_free_request(struct usb_ep *ep,
		struct usb_request *req)
{ }
static inline int usb_ep_queue(struct usb_ep *ep, struct usb_request *req)
{ return 0; }
static inline int usb_ep_dequeue(struct usb_ep *ep, struct usb_request *req)
{ return 0; }
static inline int usb_ep_set_halt(struct usb_ep *ep)
{ return 0; }
static inline int usb_ep_clear_halt(struct usb_ep *ep)
{ return 0; }
static inline int usb_ep_set_wedge(struct usb_ep *ep)
{ return 0; }
static inline int usb_ep_fifo_status(struct usb_ep *ep)
{ return 0; }
static inline void usb_ep_fifo_flush(struct usb_ep *ep)
{ }
#endif /* USB_GADGET */

/*-------------------------------------------------------------------------*/

struct usb_gadget;
struct usb_gadget_driver;
struct usb_udc;

/* the rest of the api to the controller hardware: device operations,
 * which don't involve endpoints (or i/o).
 */
struct usb_gadget_ops {
	int	(*get_frame)(struct usb_gadget *);
	int	(*wakeup)(struct usb_gadget *);
	int	(*set_selfpowered) (struct usb_gadget *, int is_selfpowered);
	int	(*vbus_session) (struct usb_gadget *, int is_active);
	int	(*vbus_draw) (struct usb_gadget *, unsigned mA);
	int	(*pullup) (struct usb_gadget *, int is_on);
	int	(*ioctl)(struct usb_gadget *,
				unsigned code, unsigned long param);
	int	(*udc_start)(struct usb_gadget *,
			struct usb_gadget_driver *);
	int	(*udc_stop)(struct usb_gadget *);
	void	(*udc_set_speed)(struct usb_gadget *, enum usb_device_speed);
	struct usb_ep *(*match_ep)(struct usb_gadget *,
			struct usb_endpoint_descriptor *);
};

/**
 * struct usb_gadget - represents a usb slave device
 * @udc: struct usb_udc pointer for this gadget
 * @ops: Function pointers used to access hardware-specific operations.
 * @ep0: Endpoint zero, used when reading or writing responses to
 *	driver setup() requests
 * @ep_list: List of other endpoints supported by the device.
 * @speed: Speed of current connection to USB host.
 * @max_speed: Maximal speed the UDC can handle.  UDC must support this
 *      and all slower speeds.
 * @state: the state we are now (attached, suspended, configured, etc)
 * @name: Identifies the controller hardware type.  Used in diagnostics
 *	and sometimes configuration.
 * @dev: Driver model state for this abstract device.
 * @out_epnum: last used out ep number
 * @in_epnum: last used in ep number
 * @mA: last set mA value
 * @otg_caps: OTG capabilities of this gadget.
 * @is_selfpowered: if the gadget is self-powered.
 * @deactivated: True if gadget is deactivated - in deactivated state it cannot
 *	be connected.
 * @connected: True if gadget is connected.
 *
 * Gadgets have a mostly-portable "gadget driver" implementing device
 * functions, handling all usb configurations and interfaces.  Gadget
 * drivers talk to hardware-specific code indirectly, through ops vectors.
 * That insulates the gadget driver from hardware details, and packages
 * the hardware endpoints through generic i/o queues.  The "usb_gadget"
 * and "usb_ep" interfaces provide that insulation from the hardware.
 *
 * Except for the driver data, all fields in this structure are
 * read-only to the gadget driver.  That driver data is part of the
 * "driver model" infrastructure in 2.6 (and later) kernels, and for
 * earlier systems is grouped in a similar structure that's not known
 * to the rest of the kernel.
 *
 */
struct usb_gadget {
	struct usb_udc			*udc;
	/* readonly to gadget driver */
	const struct usb_gadget_ops	*ops;
	struct usb_ep			*ep0;
	struct list_head		ep_list;	/* of usb_ep */
	enum usb_device_speed		speed;
	enum usb_device_speed		max_speed;
	enum usb_device_state		state;
	const char			*name;
	unsigned			out_epnum;
	unsigned			in_epnum;
	unsigned			mA;
	struct usb_otg_caps		*otg_caps;

	enum usb_device_state		suspend_state;
	void (*state_callback)(enum usb_device_state state);

	enum usb_device_power power_type;
	void (*power_callback)(enum usb_device_power type, unsigned mA);

	unsigned			is_selfpowered:1;
	unsigned			deactivated:1;
	unsigned			connected:1;

	void *drv_data;
};

static inline void set_gadget_data(struct usb_gadget *gadget, void *data)
	{ gadget->drv_data = data; }
static inline void *get_gadget_data(struct usb_gadget *gadget)
	{ return gadget->drv_data; }

/* iterates the non-control endpoints; 'tmp' is a struct usb_ep pointer */
#define gadget_for_each_ep(tmp, gadget) \
	list_for_each_entry(tmp, &(gadget)->ep_list, ep_list)

/**
 * usb_ep_align - returns @len aligned to ep's maxpacketsize.
 * @ep: the endpoint whose maxpacketsize is used to align @len
 * @len: buffer size's length to align to @ep's maxpacketsize
 *
 * This helper is used to align buffer's size to an ep's maxpacketsize.
 */
static inline size_t usb_ep_align(struct usb_ep *ep, size_t len)
{
	int max_packet_size = (size_t)usb_endpoint_maxp(ep->desc) & 0x7ff;

	return round_up(len, max_packet_size);
}

/**
 * gadget_is_dualspeed - return true iff the hardware handles high speed
 * @g: controller that might support both high and full speeds
 */
static inline int gadget_is_dualspeed(struct usb_gadget *g)
{
	return g->max_speed >= USB_SPEED_HIGH;
}

/*-------------------------------------------------------------------------*/

#if defined(CONFIG_USB_IS_DEVICE)
int usb_gadget_frame_number(struct usb_gadget *gadget);
int usb_gadget_wakeup(struct usb_gadget *gadget);
int usb_gadget_set_selfpowered(struct usb_gadget *gadget);
int usb_gadget_clear_selfpowered(struct usb_gadget *gadget);
int usb_gadget_vbus_connect(struct usb_gadget *gadget);
int usb_gadget_vbus_draw(struct usb_gadget *gadget, unsigned mA);
int usb_gadget_vbus_disconnect(struct usb_gadget *gadget);
int usb_gadget_connect(struct usb_gadget *gadget);
int usb_gadget_disconnect(struct usb_gadget *gadget);
int usb_gadget_deactivate(struct usb_gadget *gadget);
int usb_gadget_activate(struct usb_gadget *gadget);
#else
static inline int usb_gadget_frame_number(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_wakeup(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_set_selfpowered(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_clear_selfpowered(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_vbus_connect(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_vbus_draw(struct usb_gadget *gadget, unsigned mA)
{ return 0; }
static inline int usb_gadget_vbus_disconnect(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_connect(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_disconnect(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_deactivate(struct usb_gadget *gadget)
{ return 0; }
static inline int usb_gadget_activate(struct usb_gadget *gadget)
{ return 0; }
#endif /* CONFIG_USB_IS_DEVICE */

/*-------------------------------------------------------------------------*/

/**
 * struct usb_gadget_driver - driver for usb gadget devices
 * @function: String describing the gadget's function
 * @max_speed: Highest speed the driver handles.
 * @setup: Invoked for ep0 control requests that aren't handled by
 *	the hardware level driver. Most calls must be handled by
 *	the gadget driver, including descriptor and configuration
 *	management.  The 16 bit members of the setup data are in
 *	USB byte order. Called in_interrupt; this may not sleep.  Driver
 *	queues a response to ep0, or returns negative to stall.
 * @disconnect: Invoked after all transfers have been stopped,
 *	when the host is disconnected.  May be called in_interrupt; this
 *	may not sleep.  Some devices can't detect disconnect, so this might
 *	not be called except as part of controller shutdown.
 * @bind: the driver's bind callback
 * @unbind: Invoked when the driver is unbound from a gadget,
 *	usually from rmmod (after a disconnect is reported).
 *	Called in a context that permits sleeping.
 * @suspend: Invoked on USB suspend.  May be called in_interrupt.
 * @resume: Invoked on USB resume.  May be called in_interrupt.
 * @reset: Invoked on USB bus reset. It is mandatory for all gadget drivers
 *	and should be called in_interrupt.
 * @driver: Driver model state for this driver.
 * @udc_name: A name of UDC this driver should be bound to. If udc_name is NULL,
 *	this driver will be bound to any available UDC.
 * @pending: UDC core private data used for deferred probe of this driver.
 *
 * Devices are disabled till a gadget driver successfully bind()s, which
 * means the driver will handle setup() requests needed to enumerate (and
 * meet "chapter 9" requirements) then do some useful work.
 *
 * Drivers use hardware-specific knowledge to configure the usb hardware.
 * endpoint addressing is only one of several hardware characteristics that
 * are in descriptors the ep0 implementation returns from setup() calls.
 *
 * Except for ep0 implementation, most driver code shouldn't need change to
 * run on top of different usb controllers.  It'll use endpoints set up by
 * that ep0 implementation.
 *
 * The usb controller driver handles a few standard usb requests.  Those
 * include set_address, and feature flags for devices, interfaces, and
 * endpoints (the get_status, set_feature, and clear_feature requests).
 *
 * Accordingly, the driver's setup() callback must always implement all
 * get_descriptor requests, returning at least a device descriptor and
 * a configuration descriptor.  Drivers must make sure the endpoint
 * descriptors match any hardware constraints. Some hardware also constrains
 * other descriptors. (The pxa250 allows only configurations 1, 2, or 3).
 *
 * The driver's setup() callback must also implement set_configuration,
 * and should also implement set_interface, get_configuration, and
 * get_interface.  Setting a configuration (or interface) is where
 * endpoints should be activated or (config 0) shut down.
 *
 * (Note that only the default control endpoint is supported.  Neither
 * hosts nor devices generally support control traffic except to ep0.)
 *
 * Most devices will ignore USB suspend/resume operations, and so will
 * not provide those callbacks.  However, some may need to change modes
 * when the host is not longer directing those activities.  For example,
 * local controls (buttons, dials, etc) may need to be re-enabled since
 * the (remote) host can't do that any longer; or an error state might
 * be cleared, to make the device behave identically whether or not
 * power is maintained.
 */
struct usb_gadget_driver {
	char			*function;
	enum usb_device_speed	max_speed;
	int			(*bind)(struct usb_gadget *gadget,
					struct usb_gadget_driver *driver);
	void			(*unbind)(struct usb_gadget *);
	int			(*setup)(struct usb_gadget *,
					const struct usb_ctrlrequest *);
	void			(*disconnect)(struct usb_gadget *);
	void			(*suspend)(struct usb_gadget *);
	void			(*resume)(struct usb_gadget *);
	void			(*reset)(struct usb_gadget *);

	char			*udc_name;
	struct list_head	pending;
};



/*-------------------------------------------------------------------------*/

/* driver modules register and unregister, as usual.
 * these calls must be made in a context that can sleep.
 *
 * these will usually be implemented directly by the hardware-dependent
 * usb bus interface driver, which will only support a single driver.
 */

/**
 * usb_gadget_probe_driver - probe a gadget driver
 * @driver: the driver being registered
 * Context: can sleep
 *
 * Call this in your gadget driver's module initialization function,
 * to tell the underlying usb controller driver about your driver.
 * The @bind() function will be called to bind it to a gadget before this
 * registration call returns.  It's expected that the @bind() function will
 * be in init sections.
 */
int usb_gadget_probe_driver(struct usb_gadget_driver *driver);

/**
 * usb_gadget_unregister_driver - unregister a gadget driver
 * @driver:the driver being unregistered
 * Context: can sleep
 *
 * Call this in your gadget driver's module cleanup function,
 * to tell the underlying usb controller that your driver is
 * going away.  If the controller is connected to a USB host,
 * it will first disconnect().  The driver is also requested
 * to unbind() and clean up any device state, before this procedure
 * finally returns.  It's expected that the unbind() functions
 * will be in exit sections, so may not be linked in some kernels.
 */
int usb_gadget_unregister_driver(struct usb_gadget_driver *driver);

extern int usb_add_gadget_udc_release(struct usb_gadget *gadget);
extern int usb_add_gadget_udc(struct usb_gadget *gadget);
extern void usb_del_gadget_udc(struct usb_gadget *gadget);
extern char *usb_get_gadget_udc_name(void);

/*-------------------------------------------------------------------------*/

/* utility to simplify dealing with string descriptors */

/**
 * struct usb_string - wraps a C string and its USB id
 * @id:the (nonzero) ID for this string
 * @s:the string, in UTF-8 encoding
 *
 * If you're using usb_gadget_get_string(), use this to wrap a string
 * together with its ID.
 */
struct usb_string {
	u8			id;
	const char		*s;
};

/**
 * struct usb_gadget_strings - a set of USB strings in a given language
 * @language:identifies the strings' language (0x0409 for en-us)
 * @strings:array of strings with their ids
 *
 * If you're using usb_gadget_get_string(), use this to wrap all the
 * strings for a given language.
 */
struct usb_gadget_strings {
	u16			language;	/* 0x0409 for en-us */
	struct usb_string	*strings;
};

struct usb_gadget_string_container {
	struct list_head        list;
	u8                      *stash[];
};

/* put descriptor for string with that id into buf (buflen >= 256) */
int usb_gadget_get_string(const struct usb_gadget_strings *table, int id, u8 *buf);

/* check if the given language identifier is valid */
bool usb_validate_langid(u16 langid);

/*-------------------------------------------------------------------------*/

/* utility to simplify managing config descriptors */

/* write vector of descriptors into buffer */
int usb_descriptor_fillbuf(void *, unsigned,
		const struct usb_descriptor_header **);

/* build config descriptor from single descriptor vector */
int usb_gadget_config_buf(const struct usb_config_descriptor *config,
	void *buf, unsigned buflen, const struct usb_descriptor_header **desc);

/* copy a NULL-terminated vector of descriptors */
struct usb_descriptor_header **usb_copy_descriptors(
		struct usb_descriptor_header **);

/**
 * usb_free_descriptors - free descriptors returned by usb_copy_descriptors()
 * @v: vector of descriptors
 */
static inline void usb_free_descriptors(struct usb_descriptor_header **v)
{
	free(v);
}

struct usb_function;
int usb_assign_descriptors(struct usb_function *f,
		struct usb_descriptor_header **fs,
		struct usb_descriptor_header **hs);
void usb_free_all_descriptors(struct usb_function *f);

/*-------------------------------------------------------------------------*/

/* utility to simplify map/unmap of usb_requests to/from DMA */
extern void usb_gadget_map_request(struct usb_gadget *gadget,
		struct usb_request *req, int is_in);
extern void usb_gadget_unmap_request(struct usb_gadget *gadget,
		struct usb_request *req, int is_in);

/*-------------------------------------------------------------------------*/

/* utility to set gadget state properly */

extern void usb_gadget_set_state(struct usb_gadget *gadget,
		enum usb_device_state state);

/*-------------------------------------------------------------------------*/

/* utility to tell udc core that the bus reset occurs */
extern void usb_gadget_udc_reset(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver);

/*-------------------------------------------------------------------------*/

/* utility to give requests back to the gadget layer */

extern void usb_gadget_giveback_request(struct usb_ep *ep,
		struct usb_request *req);

/*-------------------------------------------------------------------------*/

/* utility to find endpoint by name */

extern struct usb_ep *gadget_find_ep_by_name(struct usb_gadget *g,
		const char *name);

/*-------------------------------------------------------------------------*/

/* utility to check if endpoint caps match descriptor needs */

extern int usb_gadget_ep_match_desc(struct usb_gadget *gadget,
		struct usb_ep *ep, struct usb_endpoint_descriptor *desc);

/*-------------------------------------------------------------------------*/

/* utility to update vbus status for udc core, it may be scheduled */
extern void usb_udc_vbus_handler(struct usb_gadget *gadget, bool status);

/*-------------------------------------------------------------------------*/

/* utility wrapping a simple endpoint selection policy */

extern struct usb_ep *usb_ep_autoconfig(struct usb_gadget *,
			struct usb_endpoint_descriptor *);

extern void usb_ep_autoconfig_release(struct usb_ep *);

extern void usb_ep_autoconfig_reset(struct usb_gadget *);

extern const char *usb_speed_string(enum usb_device_speed speed);

#endif /* _USB_GADGET_H */
