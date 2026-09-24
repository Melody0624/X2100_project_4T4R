#include <common.h>
#include <usb/ch9.h>

#include "otg.h"

static const char *const ep_type_names[] = {
	[USB_ENDPOINT_XFER_CONTROL] = "ctrl",
	[USB_ENDPOINT_XFER_ISOC] = "isoc",
	[USB_ENDPOINT_XFER_BULK] = "bulk",
	[USB_ENDPOINT_XFER_INT] = "intr",
};

const char *usb_ep_type_string(int ep_type)
{
	if (ep_type < 0 || ep_type >= ARRAY_SIZE(ep_type_names))
		return "unknown";

	return ep_type_names[ep_type];
}

static const char *const otg_state_names[] = {
	[OTG_STATE_A_IDLE] = "a_idle",
	[OTG_STATE_A_WAIT_VRISE] = "a_wait_vrise",
	[OTG_STATE_A_WAIT_BCON] = "a_wait_bcon",
	[OTG_STATE_A_HOST] = "a_host",
	[OTG_STATE_A_SUSPEND] = "a_suspend",
	[OTG_STATE_A_PERIPHERAL] = "a_peripheral",
	[OTG_STATE_A_WAIT_VFALL] = "a_wait_vfall",
	[OTG_STATE_A_VBUS_ERR] = "a_vbus_err",
	[OTG_STATE_B_IDLE] = "b_idle",
	[OTG_STATE_B_SRP_INIT] = "b_srp_init",
	[OTG_STATE_B_PERIPHERAL] = "b_peripheral",
	[OTG_STATE_B_WAIT_ACON] = "b_wait_acon",
	[OTG_STATE_B_HOST] = "b_host",
};

const char *usb_otg_state_string(enum usb_otg_state state)
{
	if (state < 0 || state >= ARRAY_SIZE(otg_state_names))
		return "UNDEFINED";

	return otg_state_names[state];
}

static const char *const speed_names[] = {
	[USB_SPEED_UNKNOWN] = "UNKNOWN",
	[USB_SPEED_LOW] = "low-speed",
	[USB_SPEED_FULL] = "full-speed",
	[USB_SPEED_HIGH] = "high-speed",
};

const char *usb_speed_string(enum usb_device_speed speed)
{
	if (speed < 0 || speed >= ARRAY_SIZE(speed_names))
		speed = USB_SPEED_UNKNOWN;
	return speed_names[speed];
}

static const char *const state_names[] = {
	[USB_STATE_NOTATTACHED] = "not attached",
	[USB_STATE_ATTACHED] = "attached",
	[USB_STATE_POWERED] = "powered",
	[USB_STATE_RECONNECTING] = "reconnecting",
	[USB_STATE_UNAUTHENTICATED] = "unauthenticated",
	[USB_STATE_DEFAULT] = "default",
	[USB_STATE_ADDRESS] = "addressed",
	[USB_STATE_CONFIGURED] = "configured",
	[USB_STATE_SUSPENDED] = "suspended",
};

const char *usb_state_string(enum usb_device_state state)
{
	if (state < 0 || state >= ARRAY_SIZE(state_names))
		return "UNKNOWN";

	return state_names[state];
}

static const char *const power_names[] = {
	[USB_POWER_UNKNOWN] = "UNKNOWN",
	[USB_POWER_SDP] = "Standard Downstream Port",
	[USB_POWER_DCP] = "Dedicated Charging Ports",
	[USB_POWER_CDP] = "Charging Downstream Port",
};

const char *usb_power_string(enum usb_device_power type)
{
	if (type < 0 || type >= ARRAY_SIZE(power_names))
		type = USB_POWER_UNKNOWN;
	return power_names[type];
}