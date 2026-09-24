#ifndef _HOST_UAC_H_
#define _HOST_UAC_H_

#include <common.h>
#include <usb/uac.h>

#ifdef CONFIG_USB_HOST_UAC

#define UAC_MINORS                      8

enum {
    UAC_SUBSTREAM_PLAYBACK = 0,
    UAC_SUBSTREAM_CAPTURE,

    UAC_SUBSTREAM_NUMS,
};

typedef struct {
    char *pcm_name;
    unsigned long channels_list;
    unsigned long pcm_data_fmt_list;
    unsigned long pcm_sample_rate_list;
} uac_substream_inform_t;

typedef struct {
    uac_substream_inform_t subs[UAC_SUBSTREAM_NUMS];
} uac_stream_inform_t;

typedef enum {
    UAC_GET_VOLUME = 0,
    UAC_SET_VOLUME,
} uac_volume_ctrl_type_t;

/* usb host uac supports up to 8 devices, each bit represents a device */
typedef void (*uac_device_callback_t)(u8 devices_bit);

/* When uac device is inserted or removed, callback will be triggered to update the state */
void usb_host_uac_register_callback(uac_device_callback_t callback);

/*
 get first found supporting audio format of uac substream
    param:
        <id> uac device index
        <inform> pointer of uac inform structure
        <stream_cnt> count of streams
    return:
        <normal> 0
        <abnormal> error code
 */
extern int usb_host_uac_get_inform(u8 id, uac_stream_inform_t inform[], int stream_cnt);

#endif

#endif /* _HOST_UAC_H_ */
