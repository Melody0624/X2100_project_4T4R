#include <common.h>
#include <os.h>
#include <usb/gadget_hid.h>

#define ONE_MOVE_DISTANCE 1
#define MOVE_STEPS 400
static const unsigned char mouse_report[] = {
    0x05, 0x01,    /* USAGE_PAGE (Generic Desktop Control)   */
    0x09, 0x02,    /* USAGE (Mouse)                          */
    0xa1, 0x01,    /* COLLECTION (Application)               */
    0x09, 0x01,    /* USAGE (Pointer)                        */
    0xa1, 0x00,    /* COLLECTION (Physical)                  */

    0x05, 0x09,    /* USAGE_PAGE (Button)                    */
    0x19, 0x01,    /* USAGE_MINIMUM (1)                      */
    0x29, 0x03,    /* USAGE_MINIMUM (3)                      */
    0x15, 0x00,    /* LOGICAL_MINIMUM (0)                    */
    0x25, 0x01,    /* LOGICAL_MINIMUM (1)                    */
    0x75, 0x01,    /* REPORT_SIZE (1)                        */
    0x95, 0x03,    /* REPORT_COUNT (3)                       */
    0x81, 0x02,    /* INPUT (Data,Var,Abs)                   */

    0x75, 0x05,    /* REPORT_SIZE (5)                        */
    0x95, 0x01,    /* REPORT_COUNT (1)                       */
    0x81, 0x01,    /* INPUT (Constant)                       */

    0x05, 0x01,    /* USAGE_PAGE (Generic Desktop Control)   */
    0x09, 0x30,    /* USAGE (X)                              */
    0x09, 0x31,    /* USAGE (Y)                              */
    0x09, 0x38,    /* USAGE (Whell)                          */
    0x15, 0x81,    /* LOGICAL_MINIMUM (-127)                 */
    0x25, 0x7f,    /* LOGICAL_MINIMUM (127)                  */
    0x75, 0x08,    /* REPORT_SIZE (8)                        */
    0x95, 0x03,    /* REPORT_COUNT (3)                       */
    0x81, 0x06,    /* INPUT (Data, Var, Rel)                 */
    0xc0,               /* END_COLLECTION                    */
    0xc0                /* END_COLLECTION                    */
};

/* hid descriptor for a mouse */
static const struct hid_report_descriptor hid_report = {
    .subclass       = 0, /* No subclass */
    .protocol       = 2, /* Mouse */
    .report_length      = 4,
    .report_desc_length     = sizeof(mouse_report),
    .report_desc        = mouse_report
};

static const struct gadget_id usb_id = {
    .vendor_id     = 0x0525,
    .product_id    = 0xb4b0
};

static void hid_connect_callback(int connect)
{
    printf("hid_connect_callback %d\n", connect);
}

static void usb_gadget_hid_thread(void *data)
{
    int cnt;
    unsigned char hid_data[4];
    while(1) {
        memset(hid_data, 0, sizeof(hid_data));
        hid_data[2] = ONE_MOVE_DISTANCE;
        for (cnt = 0; cnt < MOVE_STEPS; cnt++)
            gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);

        memset(hid_data, 0, sizeof(hid_data));
        hid_data[1] = ONE_MOVE_DISTANCE;
        for (cnt = 0; cnt < MOVE_STEPS; cnt++)
            gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);

        memset(hid_data, 0, sizeof(hid_data));
        hid_data[2] = -ONE_MOVE_DISTANCE;
        for (cnt = 0; cnt < MOVE_STEPS; cnt++)
            gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);

        memset(hid_data, 0, sizeof(hid_data));
        hid_data[1] = -ONE_MOVE_DISTANCE;
        for (cnt = 0; cnt < MOVE_STEPS; cnt++)
            gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
    }
}

static struct hid_callback hid_cb = {
    .connect_cb = hid_connect_callback,
    .request_cb = NULL,
};

int gadget_usb_hid_mouse_test(void)
{
    gadget_hid_init(&usb_id, &hid_report, &hid_cb);
    thread_create("usb_gadget_hid_mouse_thread", 8192, usb_gadget_hid_thread, NULL);
    return 0;
}
