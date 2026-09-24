#include <common.h>
#include <os.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <driver/backlight.h>
#include <driver/fb.h>
#include <devices/pwm_backlight.h>
#include <felix/felix_h264_decoder.h>

#include <driver/pcm.h>
#include <driver/pcm_adapter.h>

#include <usb/gadget_apple.h>

#include "apple_mirror.h"

extern uint64_t systick_get_time_us(void);
extern void soc_fb_set_rotate(enum lcdc_rotate_angle angle);

#define MIRROR_DEBUG 0

#ifdef MIRROR_DEBUG
#define MIR_LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#else
#define MIR_LOG(fmt, ...)
#endif

#ifdef MIRROR_DECODER_DEBUG
#define MIR_DEC_LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#else
#define MIR_DEC_LOG(fmt, ...)
#endif

#ifdef MIRROR_USB_DEBUG
#define MIR_USB_LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#else
#define MIR_USB_LOG(fmt, ...)
#endif

static const struct gadget_id usb_id = {
    .vendor_id  = 0x05ac,
    .product_id = 0x12ad,
};

struct mirror_decoder {
    struct fb_handle *fb;
    struct fb_info fb_info;
    struct backlight *backlight;

    struct felix_h264_decoder *decoder;
    struct felix_h264_output *outputs[2];
    int output_index;

    uint8_t *frame_buf;
    size_t frame_capacity;

    uint32_t width;
    uint32_t height;
    int format_change;

    int display_blank;
    int fb_enabled;
    int backlight_level;
    enum lcdc_rotate_angle rotation_angle;
};

static struct mirror_decoder g_decoder;

struct mirror_audior {
    struct pcm_device *playback_dai;
    struct pcm_device *playback_codec;
    struct pcm_adapter *adapter;
    struct pcm_params params;
    struct pcm_adapter_param adapter_param;

    unsigned int channels;
    unsigned int sample_size;
    unsigned int sample_rate;
    unsigned int frame_size;

    int format_change;
    thread_waiter_t update_waiter;
    thread_waiter_t finish_waiter;

    int alive;
    thread_waiter_t play_waiter;
    thread_ptr_t playback_thread;

    int playback_blank;
    uint8_t *blank_buf;
    size_t blank_frames;

    uint8_t *buf;
    size_t buf_size;
    uint8_t *ptr_write;
    uint8_t *ptr_read;
    size_t avail_size;
    spinlock_t lock;
};

static struct mirror_audior g_audior;

/* ------------------------------ utils -------------------------------------- */

uint64_t os_get_ns(void)
{
    return systick_get_time_us() * 1000;
}

/* ------------------------------ video -------------------------------------- */

#define MIRROR_BACKLIGHT_BRIGHTNESS 100

static void mirror_decoder_display(struct mirror_decoder *dec, struct felix_h264_output *out);

static void mirror_decoder_set_backlight(struct mirror_decoder *dec, int level)
{
    if (!dec || !dec->backlight)
        return;
    if (dec->backlight_level == level)
        return;
    backlight_set_brightness(dec->backlight, level);
    dec->backlight_level = level;
}

static void mirror_decoder_set_fb(struct mirror_decoder *dec, int enable)
{
    if (!dec || !dec->fb)
        return;
    if (dec->fb_enabled == enable)
        return;

    if (enable)
        fb_enable(dec->fb);
    else
        fb_disable(dec->fb);
    dec->fb_enabled = enable;
}

static void mirror_decoder_blank_display(struct mirror_decoder *dec)
{
    if (!dec || dec->display_blank)
        return;

    mirror_decoder_set_backlight(dec, 0);
    mirror_decoder_set_fb(dec, 0);

    dec->display_blank = 1;
}

static void mirror_decoder_release_static_buffers(struct mirror_decoder *dec)
{
    if (!dec)
        return;

    if (dec->frame_buf) {
        free(dec->frame_buf);
        dec->frame_buf = NULL;
    }
    dec->frame_capacity = 0;
}

static void mirror_decoder_store_format(struct mirror_decoder *dec,
                                        const struct apple_mirror_video_format *fmt)
{
    if (!dec || !fmt)
        return;

    if (fmt->codec) {
        if (fmt->codec != APPLE_CODEC_H264)
            printf("mirror: unsupported codec -- only h264 supported\n");
    }

    if (fmt->width && fmt->height &&
        (dec->width != fmt->width || dec->height != fmt->height)) {
        dec->width = fmt->width;
        dec->height = fmt->height;
        dec->format_change = 1;
        MIR_DEC_LOG("[mirror_decoder] format change width=%u height=%u", fmt->width, fmt->height);
    }
}

static int mirror_decoder_prepare_display(struct mirror_decoder *dec)
{
    if (!dec)
        return -1;

    if (!dec->fb) {
        dec->fb = fb_open("fb0");
        if (!dec->fb) {
            printf("mirror: failed to open fb0\n");
            return -1;
        }
        fb_enable(dec->fb);
        fb_get_info(dec->fb, &dec->fb_info);
        fb_enable_config(dec->fb);
    }

    if (!dec->backlight) {
        dec->backlight = backlight_open("backlight_pwm0");
        if (dec->backlight) {
            backlight_set_brightness(dec->backlight, MIRROR_BACKLIGHT_BRIGHTNESS);
            dec->backlight_level = MIRROR_BACKLIGHT_BRIGHTNESS;
        }
    }

    return 0;
}

static void mirror_decoder_release_decoder(struct mirror_decoder *dec)
{
    if (!dec)
        return;

    if (dec->decoder) {
        felix_h264_decoder_deinit(dec->decoder);
        dec->decoder = NULL;
    }
    if (dec->outputs[0]) {
        felix_h264_decoder_free_output_buf(dec->outputs[0]);
        dec->outputs[0] = NULL;
    }
    if (dec->outputs[1]) {
        felix_h264_decoder_free_output_buf(dec->outputs[1]);
        dec->outputs[1] = NULL;
    }
    dec->output_index = 0;
    if (dec->frame_buf) {
        free(dec->frame_buf);
        dec->frame_buf = NULL;
        dec->frame_capacity = 0;
    }
    dec->display_blank = 0;
    dec->backlight_level = -1;
    dec->rotation_angle = -1;
}

static int mirror_decoder_configure(struct mirror_decoder *dec, uint32_t width, uint32_t height)
{
    if (!dec || !width || !height)
        return -1;

    if (mirror_decoder_prepare_display(dec))
        return -1;

    if (dec->decoder && !dec->format_change)
        return 0;

    dec->format_change = 0;

    mirror_decoder_release_decoder(dec);

    MIR_DEC_LOG("[mirror_decoder] init decoder width=%u height=%u", width, height);

    struct felix_h264_decoder_param param = {
        .width = width,
        .height = height,
    };

    dec->decoder = felix_h264_decoder_init(&param);
    if (!dec->decoder) {
        printf("mirror: h264 decoder init failed\n");
        return -1;
    }

    dec->outputs[0] = felix_h264_decoder_alloc_output_buf(dec->decoder);
    dec->outputs[1] = felix_h264_decoder_alloc_output_buf(dec->decoder);
    if (!dec->outputs[0] || !dec->outputs[1]) {
        printf("mirror: output buffer alloc failed\n");
        mirror_decoder_release_decoder(dec);
        return -1;
    }

    dec->output_index = 0;
    dec->width = width;
    dec->height = height;
    return 0;
}

static void mirror_decoder_display(struct mirror_decoder *dec, struct felix_h264_output *out)
{
    if (!dec || !dec->fb || !out)
        return;

    int width = out->width - (out->crop_left + out->crop_right);
    int height = out->height - (out->crop_top + out->crop_bottom);
    enum lcdc_rotate_angle desired_angle = out->width < out->height ? ROTATE_0 : ROTATE_90;

    if (width <= 0 || height <= 0)
        return;

    struct lcdc_layer layer_cfg = {
        .fb_fmt = fb_fmt_NV12,
        .xres = width,
        .yres = height,
        .xpos = 0,
        .ypos = 0,
        .layer_order = lcdc_layer_0,
        .layer_enable = 1,
        .y = {
            .mem = out->y_mem + out->crop_top * out->width + out->crop_left,
            .stride = out->width,
        },
        .uv = {
            .mem = out->uv_mem + (out->crop_top * out->width / 2) + out->crop_left,
            .stride = out->width,
        },
        .alpha = {
            .enable = 0,
            .value = 0xff,
        },
        .convert_type = FB_CSC_BT709_FULL_RANGE,
    };

    if (out->crop_left % 8 || out->crop_top % 2)
        return;

    if (layer_cfg.xres > 2047)
        layer_cfg.xres = 2047;
    if (layer_cfg.yres > 2047)
        layer_cfg.yres = 2047;

    if (layer_cfg.xres != dec->fb_info.xres || layer_cfg.yres != dec->fb_info.yres) {
        layer_cfg.scaling.enable = 1;
        layer_cfg.scaling.xres = dec->fb_info.xres;
        layer_cfg.scaling.yres = dec->fb_info.yres;
    }

    if (!fb_set_config(dec->fb, &layer_cfg)) {
        fb_pan_display(dec->fb, 0);
    }

    if (dec->rotation_angle != desired_angle) {
        soc_fb_set_rotate(desired_angle);
        dec->rotation_angle = desired_angle;
        fb_get_info(dec->fb, &dec->fb_info);
    }
}

static void mirror_decoder_reset_stream(struct mirror_decoder *dec)
{
    if (!dec)
        return;
    dec->output_index = 0;
    dec->display_blank = 0;
}

static void mirror_decoder_process_sample(const struct apple_mirror_sample *sample,
                                          const struct apple_mirror_video_format *format,
                                          void *user)
{
    struct mirror_decoder *dec = user;
    if (!dec)
        return;

    if (!sample) {
        MIR_DEC_LOG("[mirror_decoder] blank display: screen off");
        mirror_decoder_blank_display(dec);
        return;
    }

    if (format)
        mirror_decoder_store_format(dec, format);

    if (!dec->width || !dec->height) {
        MIR_DEC_LOG("[mirror_decoder] skip: missing dimensions width=%u height=%u",
                    dec->width,
                    dec->height);
        return;
    }

    if (mirror_decoder_configure(dec, dec->width, dec->height)) {
        MIR_DEC_LOG("[mirror_decoder] configure failed width=%u height=%u",
                    dec->width,
                    dec->height);
        return;
    }

    struct felix_h264_output *out = dec->outputs[dec->output_index];
    if (felix_h264_decoder_decode(dec->decoder, (void *)sample->data, (int)sample->data_len, out)) {
        printf("[mirror_decoder] decoder decode fail\n");
        return;
    }

    if (out->got_frame) {
        mirror_decoder_set_fb(dec, 1);
        mirror_decoder_display(dec, dec->outputs[dec->output_index]);
        dec->output_index ^= 1;
        dec->display_blank = 0;
        mirror_decoder_set_backlight(dec, MIRROR_BACKLIGHT_BRIGHTNESS);
    }
}

static void mirror_video_format_cb(const struct apple_mirror_video_format *format, void *user)
{
    if (!format || !user)
        return;

    struct mirror_decoder *dec = user;

    MIR_LOG("[VIDEO_FORMAT] Received format update:");
    MIR_LOG("  - Codec: %s", (format->codec == APPLE_CODEC_H264) ? "H264" : "H265");
    MIR_LOG("  - Resolution: %u x %u", format->width, format->height);

    if (dec && format->width && format->height &&
        (dec->width != format->width || dec->height != format->height)) {
        mirror_decoder_release_decoder(dec);
    }

    mirror_decoder_store_format(dec, format);
}

static void mirror_video_cb(const struct apple_mirror_sample *sample,
                            const struct apple_mirror_video_format *format,
                            void *user)
{
    mirror_decoder_process_sample(sample, format, user);
}

/* ------------------------------ audio -------------------------------------- */

#define GPIO_PB(n)                                  (1 * 32 + (n))
#define GPIO_SPK_EN                                 GPIO_PB(11)

#define PCM_BUF_SIZE_MS                             100
#define MIRROR_BUF_SIZE_MS                          1500

static pcm_data_fmt mirror_audio_format_to_enum(unsigned int sample_size)
{
    switch (sample_size) {
    case 2: return pcm_fmt_S16LE;
    case 4: return pcm_fmt_S32LE;
    default:
        printf("mirror: format %d bits per channel not support\n", sample_size);
        return -1;
    }
}

static pcm_sample_rate mirror_audio_rate_to_enum(unsigned int rate)
{
    switch (rate) {
    case 5512: return pcm_rate_5512;
    case 8000: return pcm_rate_8000;
    case 11025: return pcm_rate_11025;
    case 12000: return pcm_rate_12000;
    case 16000: return pcm_rate_16000;
    case 22050: return pcm_rate_22050;
    case 24000: return pcm_rate_24000;
    case 32000: return pcm_rate_32000;
    case 44100: return pcm_rate_44100;
    case 48000: return pcm_rate_48000;
    case 64000: return pcm_rate_64000;
    case 88200: return pcm_rate_88200;
    case 96000: return pcm_rate_96000;
    case 176400: return pcm_rate_176400;
    case 192000: return pcm_rate_192000;
    default:
        printf("mirror: rate %d not support\n", rate);
        return -1;
    }
}

static void mirror_audior_deinit_adapter(struct mirror_audior *audior)
{
    if (audior->adapter) {
        pcm_adapter_release(audior->adapter);
        audior->adapter = NULL;
        memset(&audior->adapter_param, 0, sizeof(audior->adapter_param));
    }
}

static void mirror_audior_init_adapter(struct mirror_audior *audior)
{
    if (!audior || !audior->playback_dai)
        return;

    struct pcm_adapter_param new_param = {
        .channels = audior->channels,
        .data_fmt = mirror_audio_format_to_enum(audior->sample_size),
        .sample_rate = mirror_audio_rate_to_enum(audior->sample_rate),
    };

    if (audior->adapter &&
        audior->params.channels == new_param.channels &&
        audior->params.pcm_data_fmt == new_param.data_fmt &&
        audior->params.pcm_sample_rate == new_param.sample_rate) {
        /* not need adapter */
        mirror_audior_deinit_adapter(audior);
        return;
    }

    if (audior->adapter) {
        if (audior->adapter_param.channels == new_param.channels &&
            audior->adapter_param.data_fmt == new_param.data_fmt &&
            audior->adapter_param.sample_rate == new_param.sample_rate) {
            /* adapter params not changed */
            return;
        } else {
            /* need to reinit adapter */
            mirror_audior_deinit_adapter(audior);
        }
    }

    audior->adapter = pcm_adapter_create(audior->playback_dai, &new_param);
    assert(audior->adapter);
    audior->adapter_param = new_param;
}

static void mirror_audior_prepare_blank_buffer(struct mirror_audior *audior)
{
    if (!audior)
        return;

    /* already prepared */
    if (audior->blank_buf && audior->blank_frames > 0)
        return;

    unsigned int sample_rate = pcm_data_sample_rate(audior->params.pcm_sample_rate);
    unsigned int sample_size = pcm_data_sample_size(audior->params.pcm_data_fmt);
    unsigned int frame_size = sample_size * audior->params.channels;
    ssize_t blank_size = sample_rate * frame_size * PCM_BUF_SIZE_MS / 1000;

    audior->blank_frames = blank_size / frame_size;
    audior->blank_buf = malloc(blank_size);
    assert(audior->blank_buf);

    memset(audior->blank_buf, 0, blank_size);
}

static void mirror_audior_release_blank_buffer(struct mirror_audior *audior)
{
    if (!audior)
        return;

    if (audior->blank_buf)
        free(audior->blank_buf);
    audior->blank_buf = NULL;
    audior->blank_frames = 0;
}

static void mirror_audior_reserve_data_buffer(struct mirror_audior *audior)
{
    if (!audior)
        return;

    size_t buf_size = audior->sample_rate * audior->frame_size * MIRROR_BUF_SIZE_MS / 1000;

    if (audior->buf) {
        if (audior->buf_size >= buf_size)
            goto reset_buf;
        free(audior->buf);
        audior->buf_size = 0;
    }

    audior->buf = malloc(buf_size);
    assert(audior->buf);
    audior->buf_size = buf_size;

reset_buf:
    memset(audior->buf, 0, audior->buf_size);
    audior->ptr_write = audior->buf;
    audior->ptr_read = audior->buf;
    audior->avail_size = 0;
}

static void mirror_audior_release_data_buffer(struct mirror_audior *audior)
{
    if (!audior)
        return;

    if (audior->buf)
        free(audior->buf);
    audior->buf = NULL;
    audior->buf_size = 0;
}

static void mirror_audior_blank_playback(struct mirror_audior *audior)
{
    if (!audior || audior->playback_blank)
        return;

    if (!audior->blank_buf || audior->blank_frames <= 0)
        mirror_audior_prepare_blank_buffer(audior);

    pcm_write_frame(audior->playback_dai, (void *)audior->blank_buf, audior->blank_frames);

    audior->playback_blank = 1;
}

static void mirror_audior_write_frames(struct mirror_audior *audior,
                                       void *data, int frames)
{
    if (audior->adapter)
        pcm_adapter_write_frame(audior->adapter, data, frames);
    else
        pcm_write_frame(audior->playback_dai, data, frames);
}

static void mirror_audior_playback_thread(void *data)
{
    struct mirror_audior *audior = data;
    int frame_count;
    int timeout;

    while (audior->alive) {
        timeout = thread_waiter_wait_timeout(&audior->play_waiter, 100);

        if (!audior->alive)
            break;

        if (audior->format_change) {
            thread_waiter_wakeup(&audior->update_waiter);
            thread_waiter_wait(&audior->finish_waiter);
            continue;
        }

        if (timeout < 0) {
            mirror_audior_blank_playback(audior);
            continue;
        }

        spin_lock(&audior->lock);
        frame_count = audior->avail_size / audior->frame_size;
        spin_unlock(&audior->lock);

        if (frame_count <= 0)
            continue;

        uint8_t *buf_start = audior->buf;
        uint8_t *buf_end = buf_start + audior->buf_size;
        uint8_t *read_ptr = audior->ptr_read;
        int written_frames = 0;

        while (frame_count > 0) {
            int tail_frames = (buf_end - read_ptr) / audior->frame_size;
            if (tail_frames <= 0) {
                read_ptr = buf_start;
                continue;
            }

            int write_frames = (tail_frames < frame_count) ? tail_frames : frame_count;
            mirror_audior_write_frames(audior, (void *)read_ptr, write_frames);
            read_ptr += write_frames * audior->frame_size;
            if (read_ptr == buf_end)
                read_ptr = buf_start;

            frame_count -= write_frames;
            written_frames += write_frames;
        }

        audior->ptr_read = read_ptr;
        spin_lock(&audior->lock);
        audior->avail_size -= written_frames * audior->frame_size;
        spin_unlock(&audior->lock);

        audior->playback_blank = 0;
    }
}

static void mirror_audior_process_sample(const struct apple_mirror_sample *sample,
                                         const struct apple_mirror_audio_format *format,
                                         void *user)
{
    struct mirror_audior *audior = user;
    if (!audior || !sample)
        return;

    if (audior->format_change) {
        if (audior->playback_thread) {
            thread_waiter_wakeup(&audior->play_waiter);
            thread_waiter_wait(&audior->update_waiter);
        }

        mirror_audior_init_adapter(audior);
        mirror_audior_reserve_data_buffer(audior);
        audior->format_change = 0;

        if (audior->playback_thread) {
            thread_waiter_wakeup(&audior->finish_waiter);
        }
    }

    size_t writeable_size = 0, write_size = 0, written = 0;

    spin_lock(&audior->lock);
    writeable_size = audior->buf_size - audior->avail_size;
    spin_unlock(&audior->lock);

    write_size = (writeable_size < sample->data_len) ? writeable_size : sample->data_len;
    write_size = write_size - write_size % audior->frame_size;
    if (write_size <= 0)
        goto full_exit;

    uint8_t *buf_start = audior->buf;
    uint8_t *buf_end = buf_start + audior->buf_size;
    uint8_t *write_ptr = audior->ptr_write;
    size_t tail_space = (size_t)(buf_end - write_ptr);

    size_t first_chunk = (tail_space < write_size) ? tail_space : write_size;
    if (first_chunk) {
        memcpy(write_ptr, sample->data, first_chunk);
        write_ptr += first_chunk;
        written += first_chunk;
        if (write_ptr == buf_end)
            write_ptr = buf_start;
    }

    size_t remaining = write_size - written;
    if (remaining) {
        memcpy(write_ptr, sample->data + written, remaining);
        write_ptr += remaining;
        if (write_ptr == buf_end)
            write_ptr = buf_start;
    }

    audior->ptr_write = write_ptr;
    spin_lock(&audior->lock);
    audior->avail_size += write_size;
    spin_unlock(&audior->lock);

full_exit:
    thread_waiter_wakeup(&audior->play_waiter);
}

static void mirror_audior_init_playback(struct mirror_audior *audior,
                                        struct pcm_params *params)
{
    audior->playback_dai = pcm_get("aic-playback");
    assert(audior->playback_dai);
    audior->playback_codec = pcm_get("icodec-playback");
    assert(audior->playback_codec);
    audior->params = *params;

    spin_lock_init(&audior->lock);
    thread_waiter_init(&audior->play_waiter);
    thread_waiter_init(&audior->update_waiter);
    thread_waiter_init(&audior->finish_waiter);

    mirror_audior_init_adapter(audior);

    pcm_enable(audior->playback_codec, &audior->params);
    pcm_enable(audior->playback_dai, &audior->params);

    pcm_start(audior->playback_codec);
    pcm_start(audior->playback_dai);

    mirror_audior_prepare_blank_buffer(audior);

    mirror_audior_reserve_data_buffer(audior);

    audior->alive = 1;
    audior->playback_thread = thread_create("mirror_audior_playback_thread",
                                            16*1024,
                                            mirror_audior_playback_thread,
                                            audior);
    assert(audior->playback_thread);
}

static void mirror_audior_deinit_playback(struct mirror_audior *audior)
{
    audior->alive = 0;
    if (audior->playback_thread) {
        thread_waiter_wakeup(&audior->play_waiter);
        thread_join(audior->playback_thread, NULL);
    }

    mirror_audior_release_data_buffer(audior);

    mirror_audior_release_blank_buffer(audior);

    pcm_stop(audior->playback_codec);
    pcm_stop(audior->playback_dai);

    pcm_disable(audior->playback_dai);
    pcm_disable(audior->playback_codec);

    if (audior->adapter)
        mirror_audior_deinit_adapter(audior);
}

static void mirror_audio_format_cb(const struct apple_mirror_audio_format *format, void *user)
{
    if (!format || !user)
        return;

    struct mirror_audior *audior = user;

    printf("[AUDIO_FORMAT] Received format update:\n");
    printf("  - channels_per_frame: %u\n", format->channels_per_frame);
    printf("  - sample_rate: %u\n", format->sample_rate);
    printf("  - bits_per_channel: %u\n", format->bits_per_channel);
    printf("  - bytes_per_frame: %u\n", format->bytes_per_frame);

    if (format->bits_per_channel) {
        if (audior->sample_size != format->bits_per_channel / 8) {
            printf("mirror warning: sample size %d changed to %u\n",
                    audior->sample_size, format->bits_per_channel / 8);
            audior->sample_size = format->bits_per_channel / 8;
            audior->format_change = 1;
        }
    }

    if (format->channels_per_frame) {
        if (audior->channels != format->channels_per_frame) {
            printf("mirror warning: channels %d changed to %u\n",
                    audior->channels, format->channels_per_frame);
            audior->channels = format->channels_per_frame;
            audior->format_change = 1;
        }
    }

    if (format->sample_rate) {
        if (audior->sample_rate != format->sample_rate) {
            printf("mirror warning: sample rate %d changed to %u\n",
                    audior->sample_rate, format->sample_rate);
            audior->sample_rate = format->sample_rate;
            audior->format_change = 1;
        }
    }

    if (format->bytes_per_frame) {
        if (audior->frame_size != format->bytes_per_frame) {
            printf("mirror warning: frame size %d changed to %u\n",
                    audior->frame_size, format->bytes_per_frame);
            audior->frame_size = format->bytes_per_frame;
            audior->format_change = 1;
        }
    }
}

static void mirror_audio_cb(const struct apple_mirror_sample *sample,
                            const struct apple_mirror_audio_format *format,
                            void *user)
{
    mirror_audior_process_sample(sample, format, user);
}

/* ------------------------------ main -------------------------------------- */

static int mirror_send_cb(const uint8_t *buf, size_t len, void *user)
{
    (void)user;
    if (len > UINT32_MAX)
        return -1;

    int ret = gadget_nero_write(buf, (uint32_t)len, -1);
    return ret < 0 ? ret : 0;
}

static void apple_connect_callback(int connect)
{
    printf("apple_connect_callback %d\n", connect);
}

void ios_wired_screen_projection_test(void)
{
    int ret;
    struct nero_buffer nero_buffer;
    struct apple_mirror *mirror = NULL;

    memset(&g_decoder, 0, sizeof(g_decoder));
    memset(&g_audior, 0, sizeof(g_audior));

    struct apple_mirror_callbacks callbacks = {
        .get_time_ns = os_get_ns,
        .send = mirror_send_cb,
        .send_user = NULL,
        /* video */
        .on_video_format = mirror_video_format_cb,
        .video_format_user = &g_decoder,
        .on_video_sample = mirror_video_cb,
        .video_user = &g_decoder,
        /* audio */
        .on_audio_format = mirror_audio_format_cb,
        .audio_format_user = &g_audior,
        .on_audio_sample = mirror_audio_cb,
        .audio_user = &g_audior,
    };

    struct apple_mirror_config config = {
        /* usb stream */
        .capacity = 4*1024*1024,
        .max_frame_size = 1*1024*1024,
        /* video */
        .preferred_width = 1280,
        .preferred_height = 720,
        .max_width = 1920,
        .max_height = 1080,
        .hevc_supported = 0,
        /* audio */
        .audio_supported = 1,
        .channels = 1,
        .format_bits = 16,
        .sample_rate = 48000,
    };

    mirror = apple_mirror_init(&callbacks, &config);
    if (!mirror) {
        printf("mirror: failed to allocate apple_mirror context\n");
        return;
    }

    g_decoder.fb_enabled = -1;
    g_decoder.backlight_level = -1;
    g_decoder.rotation_angle = -1;
    mirror_decoder_prepare_display(&g_decoder);

    struct pcm_params playback_params = {
        .channels = 1,
        .pcm_data_fmt = pcm_fmt_S16LE,
        .pcm_sample_rate = pcm_rate_48000,
        .pcm_interface = pcm_interface_i2s,
        .i2s_frame_mode = i2s_LR_mode,
        .i2s_bclk_direction = i2s_bclk_codec_master,
        .i2s_frame_direction = i2s_frame_codec_master,
    };

    g_audior.channels = config.channels;
    g_audior.sample_rate = config.sample_rate;
    g_audior.sample_size = config.format_bits / 8;
    g_audior.frame_size = g_audior.sample_size * g_audior.channels;

    if (config.audio_supported) {
        mirror_audior_init_playback(&g_audior, &playback_params);
    }

    gpio_direction_output(GPIO_SPK_EN, 1);

    gadget_apple_init(&usb_id, apple_connect_callback);

    while (1) {
        ret = gadget_nero_wait_connect(-1);
        if (ret < 0) {
            printf("mirror: wait connect error %d\n", ret);
            continue;
        }

        printf("mirror: device connected\n");
        apple_mirror_reset(mirror);
        mirror_decoder_reset_stream(&g_decoder);

        while (1) {
            ret = gadget_nero_get_buffer(&nero_buffer, 5 * 1000);
            if (ret == 0) {
                if (nero_buffer.actual) {
                    if (apple_mirror_append_data(mirror,
                                                 nero_buffer.buf,
                                                 nero_buffer.actual) < 0) {
                        MIR_USB_LOG("[mirror_usb] append failed, dropping %u bytes",
                                    nero_buffer.actual);
                    }
                }

                gadget_nero_put_buffer(&nero_buffer);
            } else if (ret == -ETIMEDOUT) {
                mirror_decoder_blank_display(&g_decoder);
                continue;
            } else if (ret == -EAGAIN) {
                continue;
            } else if (ret == -EIO) {
                msleep(1000);
                break;
            } else {
                break;
            }
        }

        /* Ensure display is blanked and decoder resources are released */
        mirror_decoder_blank_display(&g_decoder);
        mirror_decoder_release_decoder(&g_decoder);
        mirror_decoder_release_static_buffers(&g_decoder);

        /* Recreate apple_mirror context to free internal buffers (e.g. AVCC converter) */
        apple_mirror_deinit(mirror);

        mirror = apple_mirror_init(&callbacks, &config);
        if (!mirror) {
            printf("mirror: failed to reinitialize apple_mirror context\n");
            break;
        }
    }

    /* Release playback resources */
    if (config.audio_supported) {
        mirror_audior_deinit_playback(&g_audior);
    }

    apple_mirror_deinit(mirror);
}
