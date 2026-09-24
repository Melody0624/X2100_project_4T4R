#ifndef __U_AUDIO_H
#define __U_AUDIO_H

#include "../composite.h"
#include <usb/gadget_uvc_uac1.h>

struct g_audio {
	struct usb_function func;
	struct usb_gadget *gadget;

	struct usb_ep *in_ep;
	struct usb_ep *out_ep;

	/* Max packet size for all in_ep possible speeds */
	unsigned int in_ep_maxpsize;
	/* Max packet size for all out_ep possible speeds */
	unsigned int out_ep_maxpsize;

	struct snd_uac_chip *uac;

	uac1_feature_callback_t feature_cb;
	u8 control_cs;
	u8 control_req;

	u8 endpoint_req;
	u8 endpoint;

	struct uac1_params params;
};

static inline struct g_audio *func_to_g_audio(struct usb_function *f)
{
	return container_of(f, struct g_audio, func);
}

static inline uint num_channels(uint chanmask)
{
	uint num = 0;

	while (chanmask) {
		num += (chanmask & 1);
		chanmask >>= 1;
	}

	return num;
}

int g_audio_setup(struct g_audio *g_audio);
void g_audio_cleanup(struct g_audio *g_audio);

void u_audio_connect_status_update(struct g_audio *audio_dev, u8 connect);

int u_audio_start_playback(struct g_audio *audio_dev);
int u_audio_set_rate_playback(struct g_audio *audio_dev);
void u_audio_stop_playback(struct g_audio *audio_dev);

int u_audio_start_capture(struct g_audio *audio_dev);
int u_audio_set_rate_capture(struct g_audio *audio_dev);
void u_audio_stop_capture(struct g_audio *audio_dev);

#endif /* __U_AUDIO_H */
