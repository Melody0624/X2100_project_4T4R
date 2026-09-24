#include <os.h>
#include <errno.h>
#include <stdio.h>
#include <common.h>
#include <stdbool.h>
#include <driver/input.h>
#include <usb/gadget_hid.h>
#include <include/devices/gt9xx_touch.h>

#define MUTILPLE_TOUCH_MAX              10

#define REPORTID_TOUCH                  1
#define REPORTID_FEATURE                2
#define TIP_SWITCH                      (1 << 0)
#define CONTACT_ID(x)                   (x << 1)

/* HID class requests */
#define HID_REQ_GET_REPORT              0x01
#define HID_REQ_GET_IDLE                0x02
#define HID_REQ_SET_REPORT              0x09
#define HID_REQ_SET_IDLE                0x0A

static const unsigned char touch_report[] =
{
    0x05, 0x0D,                         // Usage Page (Digitizer)
    0x09, 0x04,                         // Usage (Touch Screen)
    0xA1, 0x01,                         // Collection (Application)
    0x85, REPORTID_TOUCH,               //   Report ID (TOUCH)

    /* Finger 1 */
    0x05, 0x0D,                         //   Usage Page (Digitizer)
    0x09, 0x22,                         //   Usage (Finger)
    0xA1, 0x02,                         //   Collection (Logical)
    0x09, 0x42,                         //     Usage (Tip Switch)
    0x25, 0x01,                         //     Logical Maximum (1)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x51,                         //     Usage (Contact Identifier)
    0x25, MUTILPLE_TOUCH_MAX,           //     Logical Maximum (Touch Max)
    0x75, 0x04,                         //     Report Size (4)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x03,                         //     Report Count (3)
    0x81, 0x03,                         //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,                         //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,                         //     Usage (X)
    0x26, 0x00, 0x05,                   //     Logical Maximum (1280)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x31,                         //     Usage (Y)
    0x26, 0xD0, 0x02,                   //     Logical Maximum (720)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,                               //   End Collection

    /* Finger 2 */
    0x05, 0x0D,                         //   Usage Page (Digitizer)
    0x09, 0x22,                         //   Usage (Finger)
    0xA1, 0x02,                         //   Collection (Logical)
    0x09, 0x42,                         //     Usage (Tip Switch)
    0x25, 0x01,                         //     Logical Maximum (1)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x51,                         //     Usage (Contact Identifier)
    0x25, 0x0A,                         //     Logical Maximum (10)
    0x75, 0x04,                         //     Report Size (4)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x03,                         //     Report Count (3)
    0x81, 0x03,                         //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,                         //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,                         //     Usage (X)
    0x26, 0x00, 0x05,                   //     Logical Maximum (1280)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x31,                         //     Usage (Y)
    0x26, 0xD0, 0x02,                   //     Logical Maximum (720)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,                               //   End Collection

    /* Finger 3 */
    0x05, 0x0D,                         //   Usage Page (Digitizer)
    0x09, 0x22,                         //   Usage (Finger)
    0xA1, 0x02,                         //   Collection (Logical)
    0x09, 0x42,                         //     Usage (Tip Switch)
    0x25, 0x01,                         //     Logical Maximum (1)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x51,                         //     Usage (Contact Identifier)
    0x25, 0x0A,                         //     Logical Maximum (10)
    0x75, 0x04,                         //     Report Size (4)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x03,                         //     Report Count (3)
    0x81, 0x03,                         //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,                         //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,                         //     Usage (X)
    0x26, 0x00, 0x05,                   //     Logical Maximum (1280)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x31,                         //     Usage (Y)
    0x26, 0xD0, 0x02,                   //     Logical Maximum (720)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,                               //   End Collection

    /* Finger 4 */
    0x05, 0x0D,                         //   Usage Page (Digitizer)
    0x09, 0x22,                         //   Usage (Finger)
    0xA1, 0x02,                         //   Collection (Logical)
    0x09, 0x42,                         //     Usage (Tip Switch)
    0x25, 0x01,                         //     Logical Maximum (1)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x51,                         //     Usage (Contact Identifier)
    0x25, 0x0A,                         //     Logical Maximum (10)
    0x75, 0x04,                         //     Report Size (4)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x03,                         //     Report Count (3)
    0x81, 0x03,                         //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,                         //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,                         //     Usage (X)
    0x26, 0x00, 0x05,                   //     Logical Maximum (1280)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x31,                         //     Usage (Y)
    0x26, 0xD0, 0x02,                   //     Logical Maximum (720)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,                               //   End Collection

    /* Finger 5 */
    0x05, 0x0D,                         //   Usage Page (Digitizer)
    0x09, 0x22,                         //   Usage (Finger)
    0xA1, 0x02,                         //   Collection (Logical)
    0x09, 0x42,                         //     Usage (Tip Switch)
    0x25, 0x01,                         //     Logical Maximum (1)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x51,                         //     Usage (Contact Identifier)
    0x25, 0x0A,                         //     Logical Maximum (10)
    0x75, 0x04,                         //     Report Size (4)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x03,                         //     Report Count (3)
    0x81, 0x03,                         //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,                         //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,                         //     Usage (X)
    0x26, 0x00, 0x05,                   //     Logical Maximum (1280)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x31,                         //     Usage (Y)
    0x26, 0xD0, 0x02,                   //     Logical Maximum (720)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,                               //   End Collection

    /* Finger 6 */
    0x05, 0x0D,                         //   Usage Page (Digitizer)
    0x09, 0x22,                         //   Usage (Finger)
    0xA1, 0x02,                         //   Collection (Logical)
    0x09, 0x42,                         //     Usage (Tip Switch)
    0x25, 0x01,                         //     Logical Maximum (1)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x51,                         //     Usage (Contact Identifier)
    0x25, 0x0A,                         //     Logical Maximum (10)
    0x75, 0x04,                         //     Report Size (4)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x03,                         //     Report Count (3)
    0x81, 0x03,                         //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,                         //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,                         //     Usage (X)
    0x26, 0x00, 0x05,                   //     Logical Maximum (1280)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x31,                         //     Usage (Y)
    0x26, 0xD0, 0x02,                   //     Logical Maximum (720)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,                               //   End Collection

    /* Finger 7 */
    0x05, 0x0D,                         //   Usage Page (Digitizer)
    0x09, 0x22,                         //   Usage (Finger)
    0xA1, 0x02,                         //   Collection (Logical)
    0x09, 0x42,                         //     Usage (Tip Switch)
    0x25, 0x01,                         //     Logical Maximum (1)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x51,                         //     Usage (Contact Identifier)
    0x25, 0x0A,                         //     Logical Maximum (10)
    0x75, 0x04,                         //     Report Size (4)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x03,                         //     Report Count (3)
    0x81, 0x03,                         //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,                         //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,                         //     Usage (X)
    0x26, 0x00, 0x05,                   //     Logical Maximum (1280)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x31,                         //     Usage (Y)
    0x26, 0xD0, 0x02,                   //     Logical Maximum (720)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,                               //   End Collection

    /* Finger 8 */
    0x05, 0x0D,                         //   Usage Page (Digitizer)
    0x09, 0x22,                         //   Usage (Finger)
    0xA1, 0x02,                         //   Collection (Logical)
    0x09, 0x42,                         //     Usage (Tip Switch)
    0x25, 0x01,                         //     Logical Maximum (1)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x51,                         //     Usage (Contact Identifier)
    0x25, 0x0A,                         //     Logical Maximum (10)
    0x75, 0x04,                         //     Report Size (4)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x03,                         //     Report Count (3)
    0x81, 0x03,                         //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,                         //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,                         //     Usage (X)
    0x26, 0x00, 0x05,                   //     Logical Maximum (1280)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x31,                         //     Usage (Y)
    0x26, 0xD0, 0x02,                   //     Logical Maximum (720)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,                               //   End Collection

    /* Finger 9 */
    0x05, 0x0D,                         //   Usage Page (Digitizer)
    0x09, 0x22,                         //   Usage (Finger)
    0xA1, 0x02,                         //   Collection (Logical)
    0x09, 0x42,                         //     Usage (Tip Switch)
    0x25, 0x01,                         //     Logical Maximum (1)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x51,                         //     Usage (Contact Identifier)
    0x25, 0x0A,                         //     Logical Maximum (10)
    0x75, 0x04,                         //     Report Size (4)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x03,                         //     Report Count (3)
    0x81, 0x03,                         //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,                         //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,                         //     Usage (X)
    0x26, 0x00, 0x05,                   //     Logical Maximum (1280)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x31,                         //     Usage (Y)
    0x26, 0xD0, 0x02,                   //     Logical Maximum (720)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,                               //   End Collection

    /* Finger 10 */
    0x05, 0x0D,                         //   Usage Page (Digitizer)
    0x09, 0x22,                         //   Usage (Finger)
    0xA1, 0x02,                         //   Collection (Logical)
    0x09, 0x42,                         //     Usage (Tip Switch)
    0x25, 0x01,                         //     Logical Maximum (1)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x51,                         //     Usage (Contact Identifier)
    0x25, 0x0A,                         //     Logical Maximum (10)
    0x75, 0x04,                         //     Report Size (4)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x01,                         //     Report Size (1)
    0x95, 0x03,                         //     Report Count (3)
    0x81, 0x03,                         //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,                         //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,                         //     Usage (X)
    0x26, 0x00, 0x05,                   //     Logical Maximum (1280)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x31,                         //     Usage (Y)
    0x26, 0xD0, 0x02,                   //     Logical Maximum (720)
    0x15, 0x00,                         //     Logical Minimum (0)
    0x75, 0x10,                         //     Report Size (16)
    0x95, 0x01,                         //     Report Count (1)
    0x81, 0x02,                         //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,                               //   End Collection

    /* Contact Count */
    0x05, 0x0D,                         // Usage Page (Digitizer)
    0x09, 0x54,                         // Usage (Contact Count)
    0x25, MUTILPLE_TOUCH_MAX,           // Logical Maximum (Touch Max)
    0x15, 0x00,                         // Logical Minimum (0)
    0x75, 0x08,                         // Report Size (8)
    0x95, 0x01,                         // Report Count (1)
    0x81, 0x02,                         // Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

    /* Contact Count Maximum */
    0x05, 0x0D,                         // Usage Page (Digitizer)
    0x85, REPORTID_FEATURE,             // Report ID (Feature)
    0x09, 0x55,                         // Usage (Contact Count Maximum)
    0x75, 0x08,                         // Report Size (8)
    0x95, 0x01,                         // Report Count (1)
    0xB1, 0x02,                         // Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)

    0xC0,                               // End Collection
};

/* hid descriptor for a touch screen */
static const struct hid_report_descriptor hid_report = {
    .subclass           = 0,  /* No subclass */
    .protocol           = 0,  /* None */
    .report_length      = 5 * MUTILPLE_TOUCH_MAX + 2,
    .report_desc_length = sizeof(touch_report),
    .report_desc        = touch_report
};

static const struct gadget_id usb_id = {
    .vendor_id     = 0x0525,
    .product_id    = 0xb4b0
};

static struct input_event_table {
    int reported;
    struct input_event event;
};

static void hid_connect_callback(int connect)
{
    printf("hid_connect_callback %d\n", connect);
}

static int hid_request_callback(void *data, u16 *length, int request)
{
    switch (request)
    {
    case HID_REQ_GET_REPORT:
        *length = 2;
        ((u8 *) data)[0] = 0x02;    /* Report ID (Feature) */
        ((u8 *) data)[1] = 0x0A;    /* Contact Count Maximum */
        return 0;

    case HID_REQ_GET_IDLE:
    case HID_REQ_SET_REPORT:
    case HID_REQ_SET_IDLE:
    default:
        return -EOPNOTSUPP;
    }
}

static struct hid_callback hid_cb = {
    .connect_cb = hid_connect_callback,
    .request_cb = hid_request_callback,
};

static void usb_gadget_hid_thread(void *data)
{
    int ret;
    struct input_handle *handle;
    struct goodix_ts_data gt9xx;
    struct input_event event;
    uint16_t x = 0, y = 0;
    unsigned char hid_data[5 * MUTILPLE_TOUCH_MAX + 2];
    struct input_event_table gt9xx_table[MUTILPLE_TOUCH_MAX];
    unsigned int cnt = 0, rpt_cnt = 0;

    /* 初始化gt9xx触摸屏 */
    goodix_touch_init(&gt9xx);
    /* 开启gt9xx触摸屏 */
    handle = input_open("gt9xx");

    while(1){

        rpt_cnt = 0;
        memset(gt9xx_table, 0, sizeof(gt9xx_table));

        for (cnt = 0; cnt < MUTILPLE_TOUCH_MAX; cnt++) {
            ret = input_read(handle, &event, 1000);
            if (ret == -ETIMEDOUT) {
                continue;
            } else if (ret < 0) {
                printf("read touch pos failure ret == %d", ret);
                goto err_timeout;
            }

            if (event.id >= MUTILPLE_TOUCH_MAX) {
                cnt -= 1;
                printf("Support mutilple touch max %d, but input %d\n",
                    MUTILPLE_TOUCH_MAX, event.id + 1);
                continue;
            }

            gt9xx_table[event.id].reported = 1;
            memcpy(&gt9xx_table[event.id].event, &event, sizeof(struct input_event));
        }

        printf("\nID  ");
        for (int i = 0; i < 10; i++)
            printf("%4d ", i);
        printf("\nX   ");
        for (int i = 0; i < 10; i++)
            printf("%4d ", gt9xx_table[i].event.pos.x);
        printf("\nY   ");
        for (int i = 0; i < 10; i++)
            printf("%4d ", gt9xx_table[i].event.pos.y);
        printf("\n");

        memset(hid_data, 0, sizeof(hid_data));
        hid_data[0] = 0x01;                                 /* report id */

        for (cnt = 0; cnt < MUTILPLE_TOUCH_MAX; cnt ++) {
            if (gt9xx_table[cnt].reported
                    && gt9xx_table[cnt].event.pos.x >=0
                    && gt9xx_table[cnt].event.pos.y >=0) {
                rpt_cnt += 1;
                x = 1280 - gt9xx_table[cnt].event.pos.y;
                y = gt9xx_table[cnt].event.pos.x;

                hid_data[5 * cnt + 1] = TIP_SWITCH | CONTACT_ID(cnt);
                hid_data[5 * cnt + 2] = x & 0xFF;
                hid_data[5 * cnt + 3] = (x >> 8) & 0xFF;
                hid_data[5 * cnt + 4] = y & 0xFF;
                hid_data[5 * cnt + 5] = (y >> 8) & 0xFF;

            } else {
                hid_data[5 * cnt + 1] = CONTACT_ID(cnt);
            }
        }

        hid_data[5 * MUTILPLE_TOUCH_MAX + 1] = rpt_cnt;     /* Contact count */

        gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);

    }

err_timeout:
    input_close(handle);
    goodix_touch_deinit(&gt9xx);

    return;
}

int gadget_usb_hid_mutil_touch_test(void)
{
    gadget_hid_init(&usb_id, &hid_report, &hid_cb);
    thread_create("usb_gadget_hid_thread", 8192, usb_gadget_hid_thread, NULL);
    return 0;
}