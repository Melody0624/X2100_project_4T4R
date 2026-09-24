#ifndef __USBAUDIO_HELPER_H
#define __USBAUDIO_HELPER_H

#define combine_word(s)                 ((*(s)) | ((unsigned int)(s)[1] << 8))
#define combine_triple(s)               (combine_word(s) | ((unsigned int)(s)[2] << 16))
#define combine_quad(s)                 (combine_triple(s) | ((unsigned int)(s)[3] << 24))

#define snd_usb_get_iface_desc(iface)   (&(iface)->desc)
#define snd_usb_get_ep_desc(alt,ep)     (&(alt)->endpoint[ep].desc)
#define snd_usb_get_speed(dev)          ((dev)->speed)

unsigned int snd_usb_combine_bytes(unsigned char *bytes, int size);

void *snd_usb_find_desc(void *descstart, int desclen, void *after, u8 dtype);
void *snd_usb_find_csint_desc(void *buffer, int buflen, void *after, u8 dsubtype);

int snd_usb_ctl_msg(struct usb_device *dev, unsigned int pipe, u8 request,
     u8 requesttype, u16 value, u16 index, void *data, u16 size);
int snd_usb_ctrl_intf(struct uac_device *dev);

int snd_usb_get_list_num(struct list_head *list);

#endif /* __USBAUDIO_HELPER_H */
