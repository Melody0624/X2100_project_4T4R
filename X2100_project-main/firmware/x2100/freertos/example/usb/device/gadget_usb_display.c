#include <common.h>
#include <os.h>
#include <soc/fb_layer_mixer.h>
#include <driver/cache.h>
#include <driver/backlight.h>
#include <devices/gpio_backlight.h>
#include <usb/gadget_display.h>
#include <jpegd_decoder.h>
#include <felix/felix_h264_decoder.h>
#include <little_things.h>
#include <errno.h>

static const struct gadget_id display_id = {
    .vendor_id = 0xa108,
    .product_id = 0xad10,
};

static u8 edid_buf[128] = {
    // 1920 * 1080 * 30
    // 1280 * 800 * 30
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x31, 0xD8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2A, 0x22, 0x01, 0x03, 0x80, 0x00, 0x00, 0x00, 0x08, 0x5E, 0xC4, 0xA4, 0x59, 0x4A, 0x98, 0x25,
    0x20, 0x50, 0x54, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x64, 0x19, 0x80, 0x40, 0x70, 0x38, 0x12, 0x40, 0x10, 0x10,
    0x35, 0x00, 0x4D, 0xD0, 0x10, 0x00, 0x00, 0x10, 0xE4, 0x0C, 0x00, 0x40, 0x50, 0x20, 0x12, 0x30,
    0x10, 0x10, 0x35, 0x00, 0x4D, 0xD0, 0x10, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCE,
};

static struct display_edid edid = {
    .size = sizeof(edid_buf),
    .buf = edid_buf,
};

#define DISPLAY_COMPRESSION_FORMAT_MJPEG 1
#define DISPLAY_COMPRESSION_FORMAT_H264 2

static u8 specific_descriptor[] = {
    0x38, /* length */
    0x5F, /* descriptor type */
    0x02, 0x00, /* version 2.0 */
    0x36, /* length after type */
    0x01, 0x00, /* key min_width_height */
    0x04, /* key length */
    0xF0, 0x00, 0xF0, 0x00, /* key valule min_width 240 min_height 240 */
    0x02, 0x00, /* key max_width_height */
    0x04, /* key length */
    0xFF, 0x07, 0xFF, 0x07, /* key valule max_width 2047 max_height 2047 */
    0x03, 0x00, /* key max_transfer */
    0x04, /* key length */
    0xE0, 0xFF, 0x01, 0x00, /* key valule max_transfer 128*1024-32 */
    0x00, 0x01, /* key compression */
    0x04, /* key length */
    0x01, 0x00, 0x01, 0xC0, /* key valule: byte3: bit1: mjpeg pandisplay byte4: mjpeg compression_64, bit7 : enable change to rlx*/
    0x00, 0x01, /* key compression */
    0x04, /* key length */
    0x02, 0x00, 0x00, 0xC0, /* key valule h264 compression_64, bit7 : version 1.0: disable nv12 to rgb color adjust; version 2.0: enable change to rlx */
    0x00, 0x02, /* key resolution */
    0x05, /* key length */
    0x80, 0x07, 0x38, 0x04, 0x1E,/* key valule width 1920 height 1080 sync 30 */
    0x00, 0x02, /* key resolution */
    0x05, /* key length */
    0x00, 0x05, 0x20, 0x03, 0x1E,/* key valule width 1280 height 800 sync 30 */
};

static struct display_specific_des specific_des = {
    .size = sizeof(specific_descriptor),
    .buf = specific_descriptor,
};

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef FLOOR_2
#define FLOOR_2(x) ((x >> 1) << 1)
#endif

static void display_connect_callback(int connect)
{
    printf("generic_display_connect_callback %d\n", connect);
}

static u8 display_bpp_byte = 2;
static u16 display_width;
static u16 display_height;
static struct backlight *lcd_pwm;
static u8 display_backlight;
static struct fb_handle *fb_handle;
static u8 display_enable;
static u32 fb_mem_size;
static void *fb_mem;
static struct lcdc_layer layer_cfg;
static enum fb_fmt fb_fmt;

static u8 mjpeg_overflow = 0;
static u8 *mjpeg_src_mem;
static u32 mjpeg_src_size;
static u32 mjpeg_size_total;

static struct lcdc_layer direct_layer_cfg;

static struct fb_layer_mixer_dev *fb_mixer;
static struct fb_layer_mixer_output_cfg mixer_output;

static struct jpegd_decoder *jpegd_decoder;
static struct jpegd_decoder_param jpegd_param;
static struct jpegd_decoder_output *jpegd_out[2];

static struct felix_h264_decoder *h264_decoder;
static struct felix_h264_decoder_param h264_param;
static struct felix_h264_output *h264_out[2];

static u8 h264_overflow;
static u8 *h264_src_mem;
static u32 h264_src_size;
static u32 h264_size_total;

static u32 abnormal_offset;
static u8 abnormal_data[128*2*1024];

static u8 display_status;

static u8 display_autorlx = 0;
static u8 display_decode_func = 0; // 1: MJPEG, 2: H264, 0: rlx / none

// #define GADGET_DEBUG 1


static void usb_display_enable_mjpeg(void)
{
    struct fb_info fb_info;
    u32 output_size = ALIGN(display_width, 16) * ALIGN(display_height, 16);
    fb_get_info(fb_handle, &fb_info);

    if (jpegd_decoder)
        jpegd_decoder_deinit(jpegd_decoder);
    jpegd_param.width = display_width;
    jpegd_param.height = display_height;
    jpegd_param.out_fmt = JPEGD_PIX_FMT_NV12;
    jpegd_decoder = jpegd_decoder_init(&jpegd_param);

    if (!jpegd_decoder) {
        display_status |= DISPLAY_STATUS_ERROR_MEMORY;
        gadget_display_set_status(display_status);
        printf("jpegd_decoder_init fail\n");
        return;
    }

    mjpeg_src_size = output_size * 2;
    if (mjpeg_src_mem)
        free(mjpeg_src_mem);
    mjpeg_src_mem = memalign(4096, ALIGN(mjpeg_src_size, 4096));
    if (!mjpeg_src_mem) {
        mjpeg_src_size = 0;
        display_status |= DISPLAY_STATUS_ERROR_MEMORY;
        gadget_display_set_status(display_status);
        printf("malloc mjpeg_src_mem fail\n");
        return;
    }

    for (int i = 0; i < ARRAY_SIZE(jpegd_out); i++) {
        if (jpegd_out[i]) {
            jpegd_decoder_free_output_buf(jpegd_out[i]);
            jpegd_out[i] = NULL;
        }
        jpegd_out[i] = jpegd_decoder_alloc_output_buf(jpegd_decoder);
        if (!jpegd_out[i]) {
            display_status |= DISPLAY_STATUS_ERROR_MEMORY;
            gadget_display_set_status(display_status);
            printf("jpegd_decoder_alloc_output_buf\n");
            return;
        }
        memset(jpegd_out[i]->data, 0x0, output_size);
        memset((u8 *)jpegd_out[i]->data + output_size, 0x80, output_size/2);
    }

    memset(&direct_layer_cfg, 0, sizeof(direct_layer_cfg));
    direct_layer_cfg.fb_fmt = fb_fmt_NV12;
    direct_layer_cfg.xres = display_width;
    direct_layer_cfg.yres = FLOOR_2(display_height);
    direct_layer_cfg.layer_order = lcdc_layer_0;
    direct_layer_cfg.layer_enable = 1;
    direct_layer_cfg.y.mem = jpegd_out[0]->data;
    direct_layer_cfg.y.stride = ALIGN(display_width, 16);
    direct_layer_cfg.uv.mem = (u8 *)jpegd_out[0]->data + output_size;
    direct_layer_cfg.uv.stride = ALIGN(display_width, 16);
    direct_layer_cfg.convert_type = FB_CSC_BT601_FULL_RANGE;
    if (display_width != fb_info.xres || display_height != fb_info.yres) {
        direct_layer_cfg.xres = direct_layer_cfg.xres > 2047 ? 2047 : direct_layer_cfg.xres;
        direct_layer_cfg.yres = direct_layer_cfg.yres > 2047 ? 2047 : direct_layer_cfg.yres;

        direct_layer_cfg.scaling.enable = 1;
        direct_layer_cfg.scaling.xres = fb_info.xres > 2047 ? 2047 : fb_info.xres;
        direct_layer_cfg.scaling.yres = fb_info.yres > 2047 ? 2047 : fb_info.yres;
    }
    fb_set_config(fb_handle, &direct_layer_cfg);
    display_decode_func = 1;
}

static void usb_display_disable_mjpeg(void)
{
    fb_set_config(fb_handle, &layer_cfg);
    display_decode_func = 0;

    if (jpegd_decoder) {
        jpegd_decoder_deinit(jpegd_decoder);
        jpegd_decoder = NULL;
    }

    mjpeg_src_size = 0;
    if (mjpeg_src_mem) {
        free(mjpeg_src_mem);
        mjpeg_src_mem = NULL;
    }

    for (int i = 0; i < ARRAY_SIZE(jpegd_out); i++) {
        if (jpegd_out[i]) {
            jpegd_decoder_free_output_buf(jpegd_out[i]);
            jpegd_out[i] = NULL;
        }
    }
}

static void usb_display_enable_h264(void)
{
    struct fb_info fb_info;

    fb_get_info(fb_handle, &fb_info);

    if (h264_decoder)
        felix_h264_decoder_deinit(h264_decoder);
    h264_param.width = display_width;
    h264_param.height = display_height;
    h264_decoder = felix_h264_decoder_init(&h264_param);
    if (!h264_decoder) {
        display_status |= DISPLAY_STATUS_ERROR_MEMORY;
        gadget_display_set_status(display_status);
        printf("felix_h264_decoder_init fail\n");
        return;
    }

    if (h264_src_mem)
        free(h264_src_mem);
    h264_src_size = ALIGN(display_width, 16) * ALIGN(display_height, 16) * 2;
    h264_src_mem = memalign(4096, ALIGN(h264_src_size, 4096));
    if (!h264_src_mem) {
        h264_src_size = 0;
        display_status |= DISPLAY_STATUS_ERROR_MEMORY;
        gadget_display_set_status(display_status);
        printf("malloc h264_src_mem fail\n");
        return;
    }

    for (int i = 0; i < ARRAY_SIZE(h264_out); i++) {
        if (h264_out[i]) {
            felix_h264_decoder_free_output_buf(h264_out[i]);
            h264_out[i] = NULL;
        }
        h264_out[i] = felix_h264_decoder_alloc_output_buf(h264_decoder);
        if (!h264_out[i]) {
            display_status |= DISPLAY_STATUS_ERROR_MEMORY;
            gadget_display_set_status(display_status);
            printf("felix_h264_decoder_alloc_output_buf fail\n");
            return;
        }
        memset(h264_out[i]->y_mem, 0, h264_out[i]->y_size);
        memset(h264_out[i]->uv_mem, 0x80, h264_out[i]->uv_size);
    }

    memset(&direct_layer_cfg, 0, sizeof(direct_layer_cfg));
    direct_layer_cfg.fb_fmt = fb_fmt_NV12;
    direct_layer_cfg.xres = display_width;
    direct_layer_cfg.yres = display_height;
    direct_layer_cfg.layer_order = lcdc_layer_0;
    direct_layer_cfg.layer_enable = 1;
    direct_layer_cfg.y.mem = h264_out[0]->y_mem;
    direct_layer_cfg.y.stride = ALIGN(display_width, 16);
    direct_layer_cfg.uv.mem = h264_out[0]->uv_mem;
    direct_layer_cfg.uv.stride = ALIGN(display_width, 16);
    direct_layer_cfg.convert_type = FB_CSC_BT709_FULL_RANGE;
    if (display_width != fb_info.xres || display_height != fb_info.yres) {
        direct_layer_cfg.xres = direct_layer_cfg.xres > 2047 ? 2047 : direct_layer_cfg.xres;
        direct_layer_cfg.yres = direct_layer_cfg.yres > 2047 ? 2047 : direct_layer_cfg.yres;

        direct_layer_cfg.scaling.enable = 1;
        direct_layer_cfg.scaling.xres = fb_info.xres > 2047 ? 2047 : fb_info.xres;
        direct_layer_cfg.scaling.yres = fb_info.yres > 2047 ? 2047 : fb_info.yres;
    }
    fb_set_config(fb_handle, &direct_layer_cfg);
    display_decode_func = 2;
}

static void usb_display_disable_h264(void)
{
    fb_set_config(fb_handle, &layer_cfg);
    display_decode_func = 0;

    if (h264_decoder) {
        felix_h264_decoder_deinit(h264_decoder);
        h264_decoder = NULL;
    }

    for (int i = 0; i < ARRAY_SIZE(h264_out); i++) {
        if (h264_out[i]) {
            felix_h264_decoder_free_output_buf(h264_out[i]);
            h264_out[i] = NULL;
        }
    }

    h264_src_size = 0;
    if (h264_src_mem) {
        free(h264_src_mem);
        h264_src_mem = NULL;
    }
}

static void usb_display_enable_fb(void)
{
    struct fb_info fb_info;
    fb_get_info(fb_handle, &fb_info);

    if (display_enable)
        fb_disable(fb_handle);

    fb_mem_size = ALIGN(display_width, 16) * ALIGN(display_height, 16) * display_bpp_byte;
    if (fb_mem)
        free(fb_mem);

    fb_mem = memalign(4096, ALIGN(fb_mem_size + max(512, display_width * display_bpp_byte), 4096));
    if (!fb_mem) {
        fb_mem_size = 0;
        display_status |= DISPLAY_STATUS_ERROR_MEMORY;
        gadget_display_set_status(display_status);
        printf("malloc fb_mem fail\n");
        return;
    }

    memset(fb_mem, 0, fb_mem_size);
    memset(&layer_cfg, 0, sizeof(layer_cfg));
    layer_cfg.fb_fmt = fb_fmt;
    layer_cfg.xres = display_width;
    layer_cfg.yres = display_height;
    layer_cfg.layer_order = lcdc_layer_0;
    layer_cfg.layer_enable = 1;
    layer_cfg.rgb.mem = fb_mem;
    layer_cfg.rgb.stride = display_width * display_bpp_byte;
    layer_cfg.convert_type = FB_CSC_BT709_FULL_RANGE;
    if (display_width != fb_info.xres || display_height != fb_info.yres) {
        layer_cfg.xres = layer_cfg.xres > 2047 ? 2047 : layer_cfg.xres;
        layer_cfg.yres = layer_cfg.yres > 2047 ? 2047 : layer_cfg.yres;

        layer_cfg.scaling.enable = 1;
        layer_cfg.scaling.xres = fb_info.xres > 2047 ? 2047 : fb_info.xres;
        layer_cfg.scaling.yres = fb_info.yres > 2047 ? 2047 : fb_info.yres;
    }

    if (display_autorlx && (jpegd_decoder || h264_decoder)) {
        mixer_output.xres = display_width;
        mixer_output.yres = display_height;
        mixer_output.format = fb_fmt;
        mixer_output.dst_mem = fb_mem;

        if (fb_mixer)
            fb_layer_mixer_delete(fb_mixer);

        fb_mixer = fb_layer_mixer_create();
        struct lcdc_layer mixer_input_cfg = direct_layer_cfg;
        mixer_input_cfg.scaling.enable = 0;
        fb_layer_mixer_config_layer(fb_mixer, 0, &mixer_input_cfg);
        fb_layer_mixer_set_output_frame(fb_mixer, &mixer_output);
        fb_layer_mixer_enable_layer(fb_mixer, 0, 1);
    }

    if (!display_decode_func)
        fb_set_config(fb_handle, &layer_cfg);

    fb_enable_config(fb_handle);
    fb_enable(fb_handle);
    fb_pan_display(fb_handle, 0);
}

static void usb_display_disable_fb(void)
{
    display_enable = 0;

    if (fb_mixer) {
        fb_layer_mixer_delete(fb_mixer);
        fb_mixer = NULL;
    }

    fb_disable(fb_handle);

    fb_mem_size = 0;
    if (fb_mem) {
        free(fb_mem);
        fb_mem = NULL;
    }
}

static void display_cmd_data_process(u8 cmd, u8 val)
{
    switch (cmd) {
        case DISPLAY_CMD_COLOR_FORMAT:
            if (val == DISPLAY_COLOR_FORMAT_RGB565) {
                display_bpp_byte = 2;
                fb_fmt = fb_fmt_RGB565;
            } else if (val == DISPLAY_COLOR_FORMAT_XRGB888) {
                display_bpp_byte = 4;
                fb_fmt = fb_fmt_RGB888;
            } else {
                printf("not supported color format 0x%02x\n", val);
            }
            printf("bpp_byte %d, fb_fmt %d\n", display_bpp_byte, fb_fmt);
            break;
        case DISPLAY_CMD_WIDTH_HIGH:
            display_width = (display_width & 0xFF) | val << 8;
            break;
        case DISPLAY_CMD_WIDTH_LOW:
            display_width = (display_width & (0xFF << 8)) | val;
            break;
        case DISPLAY_CMD_HEIGHT_HIGH:
            display_height = (display_height & 0xFF) | val << 8;
            break;
        case DISPLAY_CMD_HEIGHT_LOW:
            display_height = (display_height & (0xFF << 8)) | val;
            break;
        case DISPLAY_CMD_BACKLIGHT:
            display_backlight = val;
            if (lcd_pwm)
                backlight_set_brightness(lcd_pwm, display_backlight);
            gadget_display_set_backlight(display_backlight);
            break;
        case DISPLAY_CMD_AUTORLX:
            display_autorlx = val;
            break;
        case DISPLAY_CMD_MJPEG:
            if (val) {
                printf("Enable mjpeg function\n");
                usb_display_enable_mjpeg();
            } else {
                printf("Disable mjpeg function\n");
                usb_display_disable_mjpeg();
            }
            break;
        case DISPLAY_CMD_H264:
            if (val) {
                printf("Enable h264 function\n");
                usb_display_enable_h264();
            } else {
                printf("Disable h264 function\n");
                usb_display_disable_h264();
            }
            break;
        case DISPLAY_CMD_BLANK_MODE:
            if (val == DISPLAY_BLANK_MODE_ON) {
                printf("width %d height %d\n", display_width, display_height);

                usb_display_enable_fb();

                if (lcd_pwm)
                    backlight_set_brightness(lcd_pwm, display_backlight);
                display_enable = 1;
                display_status |= DISPLAY_STATUS_INIT_COMPLETE;
                gadget_display_set_status(display_status);
                printf("display_enable %d\n", display_enable);
            } else if (val == DISPLAY_BLANK_MODE_POWERDOWN) {
                display_status &= 0xF0;
                gadget_display_set_status(display_status);

                if (lcd_pwm)
                    backlight_set_brightness(lcd_pwm, 0);

                usb_display_disable_fb();
                usb_display_disable_h264();
                usb_display_disable_mjpeg();

                printf("display_enable %d\n", display_enable);
            } else {
                printf("not supported blank mode 0x%02x\n", val);
            }
            break;
        default:
            break;
    }
}

static const u8 *display_raw_data_process(const u8 *pbuf, const u8 *pbuf_end)
{
    u32 pixel_offset;
    u32 raw_size;

    if (pbuf + 6 <= pbuf_end) {
        pixel_offset = *pbuf++ << 16;
        pixel_offset |= *pbuf++ << 8;
        pixel_offset |= *pbuf++;
        pixel_offset *= display_bpp_byte;
        raw_size = *pbuf++ << 16;
        raw_size |= *pbuf++ << 8;
        raw_size |= *pbuf++;
        raw_size *= display_bpp_byte;

        if (pbuf + raw_size <= pbuf_end) {
            if (fb_mem && (pixel_offset + raw_size) <= fb_mem_size) {
                memcpy(fb_mem + pixel_offset, pbuf, raw_size);
            }
            pbuf += raw_size;
        } else {
            printf("raw data len abnormal raw_size %d pbuf_size %ld\n", raw_size, pbuf_end - pbuf);
            abnormal_offset = pbuf_end - (pbuf - 8);
            memmove(abnormal_data, (pbuf - 8), abnormal_offset);
            pbuf = pbuf_end;
        }
    } else {
        printf("raw data len abnormal %ld\n", pbuf_end - pbuf);
    }

    return pbuf;
}

static const u8 *display_copy_data_process(const u8 *pbuf, const u8 *pbuf_end)
{
    u32 fb_src_offset;
    u32 fb_dst_offset;
    u32 copy_size;

    if (pbuf + 9 <= pbuf_end) {
        fb_src_offset = *pbuf++ << 16;
        fb_src_offset |= *pbuf++ << 8;
        fb_src_offset |= *pbuf++;
        fb_src_offset *= display_bpp_byte;
        copy_size = *pbuf++ << 16;
        copy_size |= *pbuf++ << 8;
        copy_size |= *pbuf++;
        copy_size *= display_bpp_byte;
        fb_dst_offset = *pbuf++ << 16;
        fb_dst_offset |= *pbuf++ << 8;
        fb_dst_offset |= *pbuf++;
        fb_dst_offset *= display_bpp_byte;
        if (fb_mem && (fb_dst_offset + copy_size) <= fb_mem_size)
            memmove(fb_mem + fb_dst_offset, fb_mem + fb_src_offset, copy_size);
    } else {
        printf("copy data len abnormal %ld\n", pbuf_end - pbuf);
    }

    return pbuf;
}

static const u8 *display_mjpeg_data_process(const u8 *pbuf, const u8 *pbuf_end)
{
    u32 src_offset;
    u32 mjpeg_size;

    if (pbuf + 6 <= pbuf_end) {
        src_offset = *pbuf++ << 16;
        src_offset |= *pbuf++ << 8;
        src_offset |= *pbuf++;
        mjpeg_size = *pbuf++ << 16;
        mjpeg_size |= *pbuf++ << 8;
        mjpeg_size |= *pbuf++;

        if (src_offset == 0 && mjpeg_size_total != 0) {
            printf("mjpeg data loss\n");
            mjpeg_size_total = 0;
            mjpeg_overflow = 0;
        }

        if (src_offset == mjpeg_size_total && pbuf + mjpeg_size <= pbuf_end) {
            if (mjpeg_src_mem && (src_offset + mjpeg_size) <= mjpeg_src_size) {
                memcpy(mjpeg_src_mem + src_offset, pbuf, mjpeg_size);
            } else {
                mjpeg_overflow = 1;
                printf("mjpeg data overflow\n");
            }
            pbuf += mjpeg_size;
            mjpeg_size_total += mjpeg_size;
        } else {
            printf("mjpeg data abnormal src_offset %d mjpeg_size_total %d mjpeg_size %d\n",
                src_offset, mjpeg_size_total, mjpeg_size);

            if (src_offset == mjpeg_size_total) {
                abnormal_offset = pbuf_end - (pbuf - 8);
                memmove(abnormal_data, (pbuf - 8), abnormal_offset);
                pbuf = pbuf_end;
            } else {
                pbuf += mjpeg_size;
            }
        }

    } else {
        printf("mjpeg data len abnormal %ld\n", pbuf_end - pbuf);
    }

    return pbuf;
}

static const u8 *display_mjpeg_data_end_process(const u8 *pbuf, const u8 *pbuf_end)
{
    static u8 decode_mjpeg_index = 0;
    u32 src_offset;
    u32 mjpeg_size;
    u32 pixel_offset;
    int full_update;
    int ret;

    if (pbuf + 9 <= pbuf_end) {
        src_offset = *pbuf++ << 16;
        src_offset |= *pbuf++ << 8;
        src_offset |= *pbuf++;
        mjpeg_size = *pbuf++ << 16;
        mjpeg_size |= *pbuf++ << 8;
        mjpeg_size |= *pbuf++;
        pixel_offset = *pbuf++ << 16;
        pixel_offset |= *pbuf++ << 8;
        pixel_offset |= *pbuf++;

        if (src_offset == 0 && mjpeg_size_total != 0) {
            printf("mjpeg data loss\n");
            mjpeg_size_total = 0;
            mjpeg_overflow = 0;
        }

        if (src_offset == mjpeg_size_total && pbuf + mjpeg_size <= pbuf_end) {
            if (mjpeg_src_mem && (src_offset + mjpeg_size) <= mjpeg_src_size) {
                memcpy(mjpeg_src_mem + src_offset, pbuf, mjpeg_size);
            } else {
                mjpeg_overflow = 1;
                printf("mjpeg data overflow\n");
            }
            pbuf += mjpeg_size;
            mjpeg_size_total += mjpeg_size;

            if (jpegd_decoder && !mjpeg_overflow && mjpeg_src_mem
                && jpegd_out[0] && jpegd_out[1]) {

                if (mjpeg_src_mem[0] == 0xFF && mjpeg_src_mem[1] == 0xD8) {

                    if (direct_layer_cfg.y.mem == jpegd_out[0]->data)
                        decode_mjpeg_index = 1;
                    else
                        decode_mjpeg_index = 0;

                    ret = jpegd_decoder_decode(jpegd_decoder, mjpeg_src_mem, mjpeg_size_total,
                                               jpegd_out[decode_mjpeg_index]);
                    if (ret) {
                        display_status |= DISPLAY_STATUS_ERROR_DECODER;
                        gadget_display_set_status(display_status);
                        printf("jpegd_decoder_decode fail\n");
                        goto mjpeg_done;
                    }

                    void *jpeg_mem = jpegd_out[decode_mjpeg_index]->data;
                    u32 out_width = jpegd_out[decode_mjpeg_index]->actual_width;
                    u32 out_height = jpegd_out[decode_mjpeg_index]->actual_height;
                    u32 align_width = jpegd_out[decode_mjpeg_index]->width;
                    u32 align_height = jpegd_out[decode_mjpeg_index]->height;
                    full_update = (out_width >= display_width &&  out_height >= display_height);

                    if (pixel_offset > 0 || !full_update) {/* 非全屏更新：局部区域 */
                        u32 src_stride = align_width;
                        u32 dst_stride = direct_layer_cfg.y.stride;
                        u32 dst_x = pixel_offset % display_width;
                        u32 dst_y = pixel_offset / display_width;

                        if(dst_x + out_width > display_width || dst_y + out_height > display_height)
                            goto mjpeg_done;

                        u8 *src_y_p = (u8 *)jpeg_mem;
                        u8 *src_uv_p = src_y_p + src_stride *  align_height;

                        u8 *dst_y_start = (u8 *)direct_layer_cfg.y.mem + dst_y * dst_stride + dst_x;
                        u8 *dst_uv_start = (u8 *)direct_layer_cfg.uv.mem + (dst_y / 2) * dst_stride + dst_x;

                        // 复制Y平面
                        for (int i = 0; i < out_height; i++) {
                            memcpy(dst_y_start + i * dst_stride, src_y_p + i * src_stride , out_width);
                        }
                        // 复制UV平面
                        for (int i = 0; i < (out_height / 2); i++) {
                            memcpy(dst_uv_start + i * dst_stride, src_uv_p + i * src_stride, out_width);
                        }
                    } else {
                        direct_layer_cfg.y.mem = jpeg_mem;
                        direct_layer_cfg.y.stride = align_width;
                        direct_layer_cfg.uv.mem = jpeg_mem + align_width * align_height;
                        direct_layer_cfg.uv.stride = align_width;

                    }
                    fb_set_config(fb_handle, &direct_layer_cfg);
                    display_decode_func = 1;
                } else {
                    printf("mjpeg data abnormal 0x%02x 0x%02x\n", mjpeg_src_mem[0], mjpeg_src_mem[1]);
                }
            } else {
                printf("mjpeg src mem abnormal\n");
            }
        } else {
            printf("mjpeg end abnormal src_offset %d mjpeg_size_total %d mjpeg_size %d\n",
                src_offset, mjpeg_size_total, mjpeg_size);
            if (src_offset == mjpeg_size_total) {
                abnormal_offset = pbuf_end - (pbuf - 11);
                memmove(abnormal_data, (pbuf - 11), abnormal_offset);
                pbuf = pbuf_end;
            } else {
                pbuf += mjpeg_size;
            }
        }

mjpeg_done:
        mjpeg_size_total = 0;
        mjpeg_overflow = 0;
    } else {
        printf("mjpeg data end len abnormal %ld\n", pbuf_end - pbuf);
    }

    return pbuf;
}

static int display_mixer_last_frame_to_fb(void)
{
    if (!display_enable || !fb_mem)
        return -1;

    if (!fb_mixer || (!jpegd_decoder && !h264_decoder))
        return 0;

    if (display_autorlx && display_decode_func != 0) {
        struct lcdc_layer mixer_input_cfg = direct_layer_cfg;
        mixer_input_cfg.scaling.enable = 0;
        fb_layer_mixer_config_layer(fb_mixer, 0, &mixer_input_cfg);
        fb_layer_mixer_work_out_one_frame(fb_mixer);
        fb_set_config(fb_handle, &layer_cfg);
        display_decode_func = 0;
#ifdef GADGET_DEBUG
        printf("mixer last frame to fb done\n");
#endif
    }
    return 0;
}

static const u8 *display_rlx_24bit_data_process(u8 abnormal_flag, const u8 *pbuf, const u8 *pbuf_end)
{
    int i;
    u32 pixel_offset;
    u16 total_count;
    u16 copy_size;
    u16 copy_start;
    u16 copy_end;
    u32 raw_data;
    u32 *raw_data_p;

    if (display_bpp_byte != 4) {
        display_status |= DISPLAY_STATUS_ERROR_DATA_FORMAT;
        gadget_display_set_status(display_status);
        printf("ERROR rlx 24bit with bpp_byte %d\n", display_bpp_byte);
    }

    if (abnormal_flag && (pbuf_end - pbuf) <= (5 + 256 * 3)) {
        printf("rlx abnormal_flag data reprocessing\n");
        abnormal_offset = pbuf_end - (pbuf - 2);
        /* The length is fixed and less than half of buf, and the data will not overlap without memmove. */
        memcpy(abnormal_data, (pbuf - 2), abnormal_offset);
        pbuf = pbuf_end;
    } else if (pbuf + 7 <= pbuf_end) {

        pixel_offset = *pbuf++ << 16;
        pixel_offset |= *pbuf++ << 8;
        pixel_offset |= *pbuf++;
        pixel_offset *= display_bpp_byte;
        total_count = *pbuf++;
        copy_start = *pbuf++;

        if (total_count == 0)
            total_count = 256;

        if (copy_start == 0)
            copy_start = 256;

        if (pixel_offset + total_count > fb_mem_size) {
            raw_data_p = NULL;
        } else {
            raw_data_p = fb_mem + pixel_offset;
        }

        raw_data = 0;
        i = 0;
        while (i < total_count) {
            if (i < copy_start) {
                raw_data = *pbuf++ << 16;
                raw_data |= *pbuf++ << 8;
                raw_data |= *pbuf++;
                if (raw_data_p) {
                    *raw_data_p = raw_data;
                    raw_data_p++;
                }
                i++;
            } else {
                copy_size = *pbuf++;
                copy_end = copy_start + copy_size;

                if (copy_end > total_count) {
                    printf("abnormal copy_start %d copy_size %d total_count %d\n", copy_start, copy_size, total_count);
                    break;
                }

                if (copy_end < total_count) {
                    copy_start = copy_end + *pbuf++;
                }

                do {
                    if (raw_data_p) {
                        *raw_data_p = raw_data;
                        raw_data_p++;
                    }
                    i++;
                } while (i < copy_end);
            }
        }

    } else {
        printf("rlx 24bit data len abnormal %ld\n", pbuf_end - pbuf);
    }

    return pbuf;
}

static const u8 *display_rlx_16bit_data_process(u8 abnormal_flag, const u8 *pbuf, const u8 *pbuf_end)
{
    int i;
    u32 pixel_offset;
    u16 total_count;
    u16 copy_size;
    u16 copy_start;
    u16 copy_end;
    u16 raw_data;
    u16 *raw_data_p;

    if (display_bpp_byte != 2) {
        display_status |= DISPLAY_STATUS_ERROR_DATA_FORMAT;
        gadget_display_set_status(display_status);
        printf("ERROR rlx 16bit with bpp_byte %d\n", display_bpp_byte);
    }

    if (abnormal_flag && (pbuf_end - pbuf) <= (5 + 256 * 2)) {
        printf("rlx abnormal_flag data reprocessing\n");
        abnormal_offset = pbuf_end - (pbuf - 2);
        /* The length is fixed and less than half of buf, and the data will not overlap without memmove. */
        memcpy(abnormal_data, (pbuf - 2), abnormal_offset);
        pbuf = pbuf_end;
    } else if (pbuf + 7 <= pbuf_end) {

        pixel_offset = *pbuf++ << 16;
        pixel_offset |= *pbuf++ << 8;
        pixel_offset |= *pbuf++;
        pixel_offset *= display_bpp_byte;
        total_count = *pbuf++;
        copy_start = *pbuf++;

        if (total_count == 0)
            total_count = 256;

        if (copy_start == 0)
            copy_start = 256;

        if (pixel_offset + total_count > fb_mem_size) {
            raw_data_p = NULL;
        } else {
            raw_data_p = fb_mem + pixel_offset;
        }

        raw_data = 0;
        i = 0;
        while (i < total_count) {
            if (i < copy_start) {
                raw_data = *pbuf++ << 8;
                raw_data |= *pbuf++;
                if (raw_data_p) {
                    *raw_data_p = raw_data;
                    raw_data_p++;
                }
                i++;
            } else {
                copy_size = *pbuf++;
                copy_end = copy_start + copy_size;

                if (copy_end > total_count) {
                    printf("abnormal copy_start %d copy_size %d total_count %d\n", copy_start, copy_size, total_count);
                    break;
                }

                if (copy_end < total_count) {
                    copy_start = copy_end + *pbuf++;
                }

                do {
                    if (raw_data_p) {
                        *raw_data_p = raw_data;
                        raw_data_p++;
                    }
                    i++;
                } while (i < copy_end);
            }
        }

    } else {
        printf("rlx 16bit data len abnormal %ld\n", pbuf_end - pbuf);
    }

    return pbuf;
}

static const u8 *display_h264_data_process(const u8 *pbuf, const u8 *pbuf_end)
{
    u32 src_offset;
    u32 h264_size;

    if (pbuf + 6 <= pbuf_end) {
        src_offset = *pbuf++ << 16;
        src_offset |= *pbuf++ << 8;
        src_offset |= *pbuf++;
        h264_size = *pbuf++ << 16;
        h264_size |= *pbuf++ << 8;
        h264_size |= *pbuf++;

        if (src_offset == 0 && h264_size_total != 0) {
            printf("h264 data loss\n");
            h264_size_total = 0;
            h264_overflow = 0;
        }

        if (src_offset == h264_size_total && pbuf + h264_size <= pbuf_end) {
            if (h264_src_mem && (src_offset + h264_size) <= h264_src_size) {
                memcpy(h264_src_mem + src_offset, pbuf, h264_size);
            } else {
                h264_overflow = 1;
                printf("h264 data overflow\n");
            }
            pbuf += h264_size;
            h264_size_total += h264_size;
        } else {
            printf("h264 data abnormal src_offset %d h264_size_total %d h264_size %d\n",
                src_offset, h264_size_total, h264_size);

            if (src_offset == h264_size_total) {
                abnormal_offset = pbuf_end - (pbuf - 8);
                memmove(abnormal_data, (pbuf - 8), abnormal_offset);
                pbuf = pbuf_end;
            } else {
                pbuf += h264_size;
            }
        }
    } else {
        printf("h264 data len abnormal %ld\n", pbuf_end - pbuf);
    }

    return pbuf;
}

static const u8 *display_h264_data_end_process(const u8 *pbuf, const u8 *pbuf_end)
{
    static u8 decode_h264_index = 0;
    u32 src_offset;
    u32 h264_size;
    int ret;

    if (pbuf + 6 <= pbuf_end) {
        src_offset = *pbuf++ << 16;
        src_offset |= *pbuf++ << 8;
        src_offset |= *pbuf++;
        h264_size = *pbuf++ << 16;
        h264_size |= *pbuf++ << 8;
        h264_size |= *pbuf++;

        if (src_offset == 0 && h264_size_total != 0) {
            printf("h264 data loss\n");
            h264_size_total = 0;
            h264_overflow = 0;
        }

        if (src_offset == h264_size_total && pbuf + h264_size <= pbuf_end) {
            if (h264_src_mem && (src_offset + h264_size) <= h264_src_size) {
                memcpy(h264_src_mem + src_offset, pbuf, h264_size);
            } else {
                h264_overflow = 1;
                printf("h264 data overflow\n");
            }
            pbuf += h264_size;
            h264_size_total += h264_size;

            if (h264_decoder && !h264_overflow && h264_src_mem
                && h264_out[0] && h264_out[1]) {
                if (direct_layer_cfg.y.mem == h264_out[0]->y_mem)
                    decode_h264_index = 1;
                else
                    decode_h264_index = 0;

                ret = felix_h264_decoder_decode(h264_decoder, h264_src_mem, h264_size_total,
                                                h264_out[decode_h264_index]);
                if (ret) {
                    display_status |= DISPLAY_STATUS_ERROR_DECODER;
                    gadget_display_set_status(display_status);
                    printf("felix_h264_decoder_decode fail\n");
                }

                direct_layer_cfg.y.mem = (void *)h264_out[decode_h264_index]->y_mem;
                direct_layer_cfg.uv.mem = (void *)h264_out[decode_h264_index]->uv_mem;
                fb_set_config(fb_handle, &direct_layer_cfg);
                display_decode_func = 2;
                if (display_enable)
                    fb_pan_display(fb_handle, 0);

            } else {
                printf("h264 src mem abnormal\n");
                printf("h264_decoder %p, h264_overflow %d, h264_src_mem %p\n",
                    h264_decoder, h264_overflow, h264_src_mem);
            }
        } else {
            printf("h264 end abnormal src_offset %d h264_size_total %d h264_size %d\n",
                src_offset, h264_size_total, h264_size);

            if (src_offset == h264_size_total) {
                abnormal_offset = pbuf_end - (pbuf - 8);
                memmove(abnormal_data, (pbuf - 8), abnormal_offset);
                pbuf = pbuf_end;
            } else {
                pbuf += h264_size;
            }
        }

        h264_size_total = 0;
        h264_overflow = 0;
    } else {
        printf("h264 data end len abnormal %ld\n", pbuf_end - pbuf);
    }

    return pbuf;
}

static void usb_gadget_display_thread(void *data)
{
    int ret;
    struct display_buffer usb_buffer;
    const u8 *pbuf, *pbuf_end;
    u8 abnormal_flag;
    u8 type;

    lcd_pwm = backlight_open(CONFIG_PWM_BACKLIGHT0_NAME);
    display_backlight = 100;
    ret = gadget_display_set_backlight(display_backlight);
    if (ret) {
        printf("gadget_display_set_backlight fail\n");
        return;
    }

    fb_handle = fb_open("fb0");
    if (fb_handle == NULL) {
        printf("open fb0 error!\n");
        return;
    }

    display_status = DISPLAY_STATUS_READY;
    ret = gadget_display_set_status(display_status);
    if (ret) {
        printf("gadget_display_set_status fail\n");
        return;
    }

    while (1) {
        gadget_display_wait_connect(-1);
        while (1) {
            ret = gadget_display_get_buffer(&usb_buffer, -1);
            if (ret < 0) {
                printf("gadget_display_get_buffer fail: %d \n", ret);
                break;
            }

            if (usb_buffer.length == usb_buffer.actual)
                abnormal_flag = 1;
            else
                abnormal_flag = 0;

            if (abnormal_offset) {
                memcpy(abnormal_data + abnormal_offset, usb_buffer.buf, usb_buffer.actual);
                pbuf = abnormal_data;
                pbuf_end = abnormal_data + abnormal_offset + usb_buffer.actual;
                abnormal_offset = 0;
            } else {
                pbuf = usb_buffer.buf;
                pbuf_end = usb_buffer.buf + usb_buffer.actual;
            }

            while (pbuf + MINI_CMD_SIZE <= pbuf_end) {
                if (*pbuf++ != 0xAF) {
                    printf("The first data must be 0xAF, 0x%02x %d, remaining %ld\n", *usb_buffer.buf, usb_buffer.actual, pbuf_end - pbuf);
                    break;
                }

            check_cmd_type:
                type = *pbuf++;
                switch (type) {
                    case 0x20: /* cmd */
                        if (pbuf + 2 <= pbuf_end) {
                            u8 cmd = *pbuf++;
                            u8 val = *pbuf++;
                            display_cmd_data_process(cmd, val);
                        } else {
                            printf("type 0x%02x len abnormal %ld\n", type, pbuf_end - pbuf);
                        }
                        break;

                    case 0x65: /* rlx 24bit data */
                        display_mixer_last_frame_to_fb();
                        pbuf = display_rlx_24bit_data_process(abnormal_flag, pbuf, pbuf_end);
                        break;
                    case 0x66: /* pandisplay */
                        if (display_enable)
                            fb_pan_display(fb_handle, 0);
                        break;
                    case 0x67: /* raw data */
                        display_mixer_last_frame_to_fb();
                        pbuf = display_raw_data_process(pbuf, pbuf_end);
                        break;
                    case 0x68: /* mjpeg data */
                        pbuf = display_mjpeg_data_process(pbuf, pbuf_end);
                        break;
                    case 0x69: /* mjpeg data end */
                        pbuf = display_mjpeg_data_end_process(pbuf, pbuf_end);
                        break;
                    case 0x6A: /* data copy */
                        display_mixer_last_frame_to_fb();
                        pbuf = display_copy_data_process(pbuf, pbuf_end);
                        break;
                    case 0x6B: /* rlx 16bit data */
                        display_mixer_last_frame_to_fb();
                        pbuf = display_rlx_16bit_data_process(abnormal_flag, pbuf, pbuf_end);
                        break;
                    case 0x6C: /* h264 data */
                        pbuf = display_h264_data_process(pbuf, pbuf_end);
                        break;
                    case 0x6D: /* h264 end data */
                        pbuf = display_h264_data_end_process(pbuf, pbuf_end);
                        break;
                    case 0xAF:
                        if (pbuf + 1 <= pbuf_end)
                            goto check_cmd_type;
                        break;
                    default:
                        printf("not supported cmd type %d\n", type);
                        break;
                }
            }

            gadget_display_put_buffer(&usb_buffer);
        }
    }
}

int gadget_usb_display_test(void)
{
    gadget_display_init(&display_id, &edid, &specific_des, display_connect_callback);
    thread_create("usb gadget display thread", 16*1024, usb_gadget_display_thread, NULL);

    return 0;
}
