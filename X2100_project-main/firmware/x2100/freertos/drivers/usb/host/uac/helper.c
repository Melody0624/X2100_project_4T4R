#include "uacaudio.h"
#include "endpoint.h"
#include "helper.h"
#include "pcm.h"

#define BITS_PER_LONG                   32
#define BIT_MASK_LONG(nr)               (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)                    ((nr) / BITS_PER_LONG)

unsigned int snd_usb_combine_bytes(unsigned char *bytes, int size)
{
    switch (size) {
    case 1:  return *bytes;
    case 2:  return combine_word(bytes);
    case 3:  return combine_triple(bytes);
    case 4:  return combine_quad(bytes);
    default: return 0;
    }
}

int snd_usb_get_list_num(struct list_head *list)
{
    struct list_head *pos, *n_list;
    int num = 0;

    if (list_empty(list))
        return 0;

    list_for_each_safe(pos, n_list, list)
        num++;
    return num;
}

void *snd_usb_find_desc(void *descstart, int desclen, void *after, u8 dtype)
{
    u8 *p, *end, *next;

    p = descstart;
    end = p + desclen;
    for (; p < end;) {
        if (p[0] < 2)
            return NULL;
        next = p + p[0];
        if (next > end)
            return NULL;
        if (p[1] == dtype && (!after || (void *)p > after)) {
            return p;
        }
        p = next;
    }
    return NULL;
}

void *snd_usb_find_csint_desc(void *buffer, int buflen, void *after, u8 dsubtype)
{
    unsigned char *p = after;

    while ((p = snd_usb_find_desc(buffer, buflen, p, USB_DT_CS_INTERFACE)) != NULL) {
        if (p[0] >= 3 && p[2] == dsubtype)
            return p;
    }
    return NULL;
}

int snd_usb_ctrl_intf(struct uac_device *dev)
{
    return snd_usb_get_iface_desc(dev->ctrl_intf)->bInterfaceNumber;
}

int snd_usb_ctl_msg(struct usb_device *dev, unsigned int pipe, u8 request,
            u8 requesttype, u16 value, u16 index, void *data, u16 size)
{
    return usb_control_msg(dev, pipe, request, requesttype, value, index, data, size, UAC_REQ_TIMEOUT);
}
