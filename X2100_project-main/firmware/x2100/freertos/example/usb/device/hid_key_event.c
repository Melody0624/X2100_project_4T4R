#include <common.h>
#include <os.h>
#include <usb/gadget_hid.h>
#include "hid_key_event.h"


void hid_key_event_init(struct hid_key_event *event)
{
    memset(event, 0, sizeof(struct hid_key_event));
}

int hid_key_event_add(struct hid_key_event *event, int code, int value)
{
    int i;

    if (code >= USB_LEFTCTRL) {
        code -= USB_LEFTCTRL;
        if (value)
            event->hid_data[0] |= 1 << code;
        else
            event->hid_data[0] &= ~(1 << code);
        return 1;
    }

    for (i = 0; i < event->key_cnt; i++) {
        if (event->hid_data[i + 2] == code)
            break;
    }

    if (value) {
        if (event->key_cnt >= 6)
            return 0;

        if (i == event->key_cnt) {
            event->hid_data[i + 2] = code;
            event->key_cnt++;
        }
    } else {
        if (i == event->key_cnt)
            return -1;

        for (; i < event->key_cnt - 1; i++) {
            event->hid_data[i + 2] = event->hid_data[i + 3];
        }
        event->hid_data[i + 2] = 0;
        event->key_cnt--;
    }

    return 1;
}

void gadget_keyboard_report_event(struct hid_key_event *event)
{
    gadget_hid_write(event->hid_data, REPORT_LENGTH, 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
}

int hid_key_query_event(struct hid_key_event *event, int code)
{
    int i;

    if (code >= USB_LEFTCTRL) {
        code -= USB_LEFTCTRL;
        if (event->hid_data[0] & (1 << code))
            return 1;
    } else {
        for (i = 0; i < event->key_cnt; i++) {
        if (code == event->hid_data[i + 2])
            return 1;
        }
    }

    return 0;
}



