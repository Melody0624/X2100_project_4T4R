#ifndef __USBAUDIO_ENDPOINT_H
#define __USBAUDIO_ENDPOINT_H

#define SND_USB_ENDPOINT_TYPE_DATA      0
#define SND_USB_ENDPOINT_TYPE_SYNC      1

#define SND_USB_ENDPOINT_DIR_IN         1
#define SND_USB_ENDPOINT_DIR_OUT        0

#define SND_USB_ENDPOINT_STATE_START    0
#define SND_USB_ENDPOINT_STATE_STOP     1
#define SND_USB_ENDPOINT_STATE_PAUSE    2

struct snd_usb_endpoint *snd_usb_add_endpoint(struct uac_device *dev,
                struct usb_host_interface *alts, int ep_num, int direction, int type);
void snd_usb_endpoints_release(struct uac_device *dev);

int snd_usb_endpoint_set_params(struct snd_usb_endpoint *ep,
                pcm_data_fmt pcm_format,
                unsigned int channels,
                unsigned int period_bytes,
                unsigned int period_frames,
                unsigned int buffer_periods,
                unsigned int rate,
                struct audioformat *fmt);

int snd_usb_endpoint_start(struct snd_usb_endpoint *ep);
void snd_usb_endpoint_stop(struct snd_usb_endpoint *ep);
void snd_usb_endpoint_pending_stop(struct snd_usb_endpoint *ep);
void snd_usb_endpoint_deactivate(struct snd_usb_endpoint *ep);
int snd_usb_endpoint_alloc_buffer(struct snd_usb_endpoint *ep, int size);
void snd_usb_endpoint_free_buffer(struct snd_usb_endpoint *ep);
void snd_usb_endpoint_set_ops(struct snd_usb_endpoint *ep, int state);
int snd_usb_endpoint_next_packet_size(struct snd_usb_endpoint *ep);

#endif /* __USBAUDIO_ENDPOINT_H */
