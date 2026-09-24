#ifndef _USB_AUDIO_H_
#define _USB_AUDIO_H_

#include <include/ring_buffer.h>
#include <include/driver/pcm.h>
#include <drivers/usb/host/usb.h>
#include <usb/host_uac.h>
#include "audio.h"

#define UAC_REQ_TIMEOUT                 5000
#define UAC_BITS_PER_BYTE               8
#define UAC_MAX_NR_RATES                1024
#define UAC_MAX_PACKS                   6        /* per URB */
#define UAC_MAX_PACKS_HS                (UAC_MAX_PACKS * 8)    /* in high speed mode */
#define UAC_MAX_URBS                    12
#define UAC_MAX_QUEUE                   18    /* try not to exceed this queue length, in ms */
#define UAC_IFACE_UNUSED                ((void *)-1L)

#define UAC_ERROR(fmt,arg...)           printf("<<-UAC-ERROR->> "fmt"\n",##arg)
#define UAC_INFO(fmt,arg...)            printf("<<-UAC-INFO->> "fmt"\n",##arg)

struct snd_usb_endpoint;

struct snd_usb_substream {
    struct snd_usb_stream *stream;
    struct usb_device *dev;

    char pcm_name[15];
    struct pcm_dev_data *pcm;

    struct urb *urb;
    struct usb_host_endpoint *ep;
    struct snd_usb_endpoint *data_endpoint;
    unsigned int flags;

    int direction;                      /* playback or capture */
    int ifnum;                          /* current interface */
    int endpoint;                       /* assigned endpoint */
    pcm_data_fmt pcm_format;            /* current audio format (for hw_params callback) */
    struct audioformat *cur_audiofmt;

    unsigned int transfer_done;         /* processed frames since last period update */
    unsigned int hwptr_done;            /* processed byte position in the buffer */
    unsigned int frame_limit;           /* limits number of packets in URB */
    unsigned int channels;              /* current number of channels (for hw_params callback) */
    unsigned int cur_rate;              /* current rate */
    unsigned int sample_bits;           /* sample bits */
    unsigned int period_frames;         /* current frames per period */
    unsigned int buffer_periods;        /* current periods per buffer */
    unsigned int period_bytes;          /* current period bytes (for hw_params callback) */
    unsigned int *rate_table;
    unsigned int ep_num;                /* the endpoint number */

    u64 formats;                        /* format bitmasks (all or'ed) */
    unsigned int num_formats;           /* number of supported audio formats (list) */
    struct list_head fmt_list;          /* format list */
    spinlock_t lock;
};

struct snd_urb_ctx {
    struct urb *urb;
    unsigned int buffer_size;           /* size of data buffer, if data URB */
    struct snd_usb_substream *subs;
    struct snd_usb_endpoint *ep;
    int index;                          /* index for urb array */
    int packets;                        /* number of packets per urb */
    int packet_size[UAC_MAX_PACKS_HS];  /* size of packets for next submission */
};

struct snd_usb_endpoint {
    struct uac_device *dev;

    int use_count;
    int ep_num;                         /* the referenced endpoint number */
    int type;                           /* SND_USB_ENDPOINT_TYPE_* */
    int direction;                      /* SND_USB_ENDPOINT_DIR_* */
    unsigned int flags;

    void (*prepare_data_urb) (struct snd_usb_substream *subs, struct urb *urb);
    void (*retire_data_urb) (struct snd_usb_substream *subs, struct urb *urb);

    struct snd_usb_substream *data_subs;
    struct snd_urb_ctx urb[UAC_MAX_URBS];

    unsigned int nurbs;                 /* # urbs */
    unsigned int active_mask;          /* bitmask of active urbs */
    unsigned int unlink_mask;          /* bitmask of unlinked urbs */
    unsigned int pipe;                  /* the data i/o pipe */
    unsigned int freqn;                 /* nominal sampling rate in fs/fps in Q16.16 format */
    unsigned int freqm;                 /* momentary sampling rate in fs/fps in Q16.16 format */
    unsigned int freqmax;               /* maximum sampling rate, used for buffer management */
    unsigned int phase;                 /* phase accumulator */
    unsigned int maxpacksize;           /* max packet size in bytes */
    unsigned int maxframesize;          /* max packet size in frames */
    unsigned int max_urb_frames;        /* max URB size in frames */
    unsigned int curpacksize;           /* current packet size in bytes (for capture) */
    unsigned int curframesize;          /* current packet size in frames (for capture) */
    unsigned int fill_max:1;            /* fill max packet size always */
    unsigned int datainterval;          /* log_2 of data packet interval */
    unsigned char silence_value;
    unsigned int stride;
    int iface, altsetting;

    unsigned char *ring_area;           /* ring buffer area */
    size_t ring_bytes;                  /* size of ring buffer area */
    struct ring_buffer_writer writer;
    struct ring_buffer_reader reader;

    pcm_data_fmt format;                /* data format */
    unsigned int rate;                  /* rate in Hz */
    unsigned int channels;              /* channels */
    unsigned long buffer_size;          /* bytes per buffer */
    unsigned long period_size;          /* bytes per period */
    unsigned int frame_bits;            /* bits per frame */
    unsigned int sample_bits;           /* bits per sample */

    spinlock_t lock;
    struct list_head list;
};

struct snd_usb_stream {
    struct uac_device *uac;             /* uac device */
    unsigned int fmt_type;              /* USB audio format type (1-3) */
    int pcm_index;                      /* index for pcm_list of uac device */
    int opened_pcm;                     /* number of opened pcm */
    struct snd_usb_substream substream[2];
    struct list_head list;
};

struct usb_mixer_interface {
    u8 idx;
    int protocol;
    int channels;
    struct usb_host_interface *hostif;
    int vol_max;
    int vol_min;
    int vol_cur;
    void *prv_data;                     /* uac device */
};

struct uac_device {
    struct usb_device *udev;
    struct usb_interface *intf;
    struct usb_host_interface *ctrl_intf;
    u8 minor;                           /* uac minor number */
    char name[32];
    bool exiting;
    unsigned int used;
    int opened_stream;                  /* number of opened stream */

    struct list_head pcm_list;
    struct list_head ep_list;
    struct usb_mixer_interface *mixer;
};

struct audioformat {
    struct list_head list;
    u64 formats;                        /* ALSA format bits */
    unsigned int channels;              /* # channels */
    unsigned int fmt_type;              /* USB audio format type (1-3) */
    unsigned int fmt_bits;              /* number of significant bits */
    int iface;                          /* interface number */
    unsigned char altsetting;           /* corresponding alternate setting */
    unsigned char altset_idx;           /* array index of altenate setting */
    unsigned char attributes;           /* corresponding attributes of cs endpoint */
    unsigned char endpoint;             /* endpoint */
    unsigned char ep_attr;              /* endpoint attributes */
    unsigned char datainterval;         /* log_2 of data packet interval */
    unsigned int maxpacksize;           /* max. packet size */
    unsigned int rates;                 /* rate bitmasks */
    unsigned int rate_min, rate_max;    /* min/max rates */
    unsigned int nr_rates;              /* number of rate table entries */
    unsigned int *rate_table;           /* rate table */
    unsigned char clock;                /* associated clock */
};

/* core driver */
extern struct uac_device *uac_table[UAC_MINORS];
extern struct mutex device_lock[UAC_MINORS];
extern thread_cond_t free_cond[UAC_MINORS];

#define UAC_READY(dev)    (dev && dev->udev && !dev->exiting)

void usb_uac_driver_register(void);
void usb_uac_driver_deregister(void);

void update_devices_bit(void);

/* clock for uac version 2.0 */
struct uac_clock_source_descriptor *
    snd_usb_find_clock_source(struct usb_host_interface *ctrl_iface, int clock_id);
int snd_usb_clock_find_source(struct uac_device *dev, int entity_id, bool validate);

#endif /* _USB_AUDIO_H_ */
