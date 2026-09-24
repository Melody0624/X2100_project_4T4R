#ifndef __USBAUDIO_PCM_H
#define __USBAUDIO_PCM_H

#define SNDRV_PCM_RATE_CONTINUOUS       (1U<<30)    /* continuous range */

pcm_sample_rate snd_pcm_rate_to_rate_enum(unsigned int rate);
u64 pcm_format_to_bits(pcm_data_fmt pcm_format);

int snd_usb_mixer_create(struct uac_device *dev, int ctrlif);
void snd_usb_mixer_delete(struct uac_device *uac);

int snd_usb_pcm_new(struct snd_usb_stream *as, int stream);
void snd_usb_pcm_free(struct pcm_dev_data *pcm);

void snd_usb_init_interface(struct uac_device *dev, int iface_no, int altno,
            struct usb_host_interface *alts, struct audioformat *fp);
void snd_usb_init_substream(struct snd_usb_stream *as,int stream, struct audioformat *fp);


#endif /* __USBAUDIO_PCM_H */
