#include <common.h>
#include <os.h>
#include <usb/gadget_uvc.h>

#include <stdio.h>
#include <os.h>
#include <driver/camera.h>
#include <common.h>
#include <driver/cache.h>

#define WEBCAM_VENDOR_ID		0x1d6b	/* Linux Foundation */
#define WEBCAM_PRODUCT_ID		0x0102	/* Webcam A/V gadget */

static struct gadget_id usb_id = {
    .vendor_id = WEBCAM_VENDOR_ID,
    .product_id = WEBCAM_PRODUCT_ID,
};

static const unsigned int frame_fps[] = {
    20,
};

static struct uvc_frame_config uvc_frames[] = {
    {
        // .width = 1280,
        // .height = 720,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
};

static struct uvc_format_config uvc_frame_format[]= {
    {
        .fcc = V4L2_PIX_FMT_YUYV, // V4L2_PIX_FMT_GREY
        .bpp = 16, // 8
        .frames_num = ARRAY_SIZE(uvc_frames),
        .frames = uvc_frames,
    },
};

static struct uvc_device_config uvc_config = {
    .format_num = ARRAY_SIZE(uvc_frame_format),
    .formats = uvc_frame_format,
};

void bayer16_to_yuyv(void *_dst, void *_src, int xres, int yres)
{
    int len = xres * yres;
    unsigned char *src = _src + 1;
    unsigned char *dst = _dst;

    int i = len / 16;
    while (i--) {
        unsigned char s0 = src[0*2];
        unsigned char s1 = src[1*2];
        unsigned char s2 = src[2*2];
        unsigned char s3 = src[3*2];
        unsigned char s4 = src[4*2];
        unsigned char s5 = src[5*2];
        unsigned char s6 = src[6*2];
        unsigned char s7 = src[7*2];
        unsigned char s8 = src[8*2];
        unsigned char s9 = src[9*2];
        unsigned char s10 = src[10*2];
        unsigned char s11 = src[11*2];
        unsigned char s12 = src[12*2];
        unsigned char s13 = src[13*2];
        unsigned char s14 = src[14*2];
        unsigned char s15 = src[15*2];

        dst[0*2] = s0;
        dst[0*2+1] = 0x80;
        dst[1*2] = s1;
        dst[1*2+1] = 0x80;
        dst[2*2] = s2;
        dst[2*2+1] = 0x80;
        dst[3*2] = s3;
        dst[3*2+1] = 0x80;
        dst[4*2] = s4;
        dst[4*2+1] = 0x80;
        dst[5*2] = s5;
        dst[5*2+1] = 0x80;
        dst[6*2] = s6;
        dst[6*2+1] = 0x80;
        dst[7*2] = s7;
        dst[7*2+1] = 0x80;
        dst[8*2] = s8;
        dst[8*2+1] = 0x80;
        dst[9*2] = s9;
        dst[9*2+1] = 0x80;
        dst[10*2] = s10;
        dst[10*2+1] = 0x80;
        dst[11*2] = s11;
        dst[11*2+1] = 0x80;
        dst[12*2] = s12;
        dst[12*2+1] = 0x80;
        dst[13*2] = s13;
        dst[13*2+1] = 0x80;
        dst[14*2] = s14;
        dst[14*2+1] = 0x80;
        dst[15*2] = s15;
        dst[15*2+1] = 0x80;

        src += 32;
        dst += 32;
    }
}

void bayer16_to_bayer8(void *_dst, void *_src, int xres, int yres)
{
    int len = xres * yres;
    unsigned char *src = _src + 1;
    unsigned char *dst = _dst;

    int i = len / 16;
    while (i--) {
        unsigned char s0 = src[0*2];
        unsigned char s1 = src[1*2];
        unsigned char s2 = src[2*2];
        unsigned char s3 = src[3*2];
        unsigned char s4 = src[4*2];
        unsigned char s5 = src[5*2];
        unsigned char s6 = src[6*2];
        unsigned char s7 = src[7*2];
        unsigned char s8 = src[8*2];
        unsigned char s9 = src[9*2];
        unsigned char s10 = src[10*2];
        unsigned char s11 = src[11*2];
        unsigned char s12 = src[12*2];
        unsigned char s13 = src[13*2];
        unsigned char s14 = src[14*2];
        unsigned char s15 = src[15*2];

        dst[0] = s0;
        dst[1] = s1;
        dst[2] = s2;
        dst[3] = s3;
        dst[4] = s4;
        dst[5] = s5;
        dst[6] = s6;
        dst[7] = s7;
        dst[8] = s8;
        dst[9] = s9;
        dst[10] = s10;
        dst[11] = s11;
        dst[12] = s12;
        dst[13] = s13;
        dst[14] = s14;
        dst[15] = s15;

        src += 32;
        dst += 16;
    }
}

void grey_to_yuyv(void *_dst, void *_src, int xres, int yres)
{
    int len = xres * yres;
    unsigned char *src = _src + 1;
    unsigned char *dst = _dst;

    int i = len / 16;
    while (i--) {
        unsigned char s0 = src[0];
        unsigned char s1 = src[1];
        unsigned char s2 = src[2];
        unsigned char s3 = src[3];
        unsigned char s4 = src[4];
        unsigned char s5 = src[5];
        unsigned char s6 = src[6];
        unsigned char s7 = src[7];
        unsigned char s8 = src[8];
        unsigned char s9 = src[9];
        unsigned char s10 = src[10];
        unsigned char s11 = src[11];
        unsigned char s12 = src[12];
        unsigned char s13 = src[13];
        unsigned char s14 = src[14];
        unsigned char s15 = src[15];

        dst[0 * 2 + 0] = s0;
        dst[0 * 2 + 1] = 128;
        dst[1 * 2 + 0] = s1;
        dst[1 * 2 + 1] = 128;
        dst[2 * 2 + 0] = s2;
        dst[2 * 2 + 1] = 128;
        dst[3 * 2 + 0] = s3;
        dst[3 * 2 + 1] = 128;
        dst[4 * 2 + 0] = s4;
        dst[4 * 2 + 1] = 128;
        dst[5 * 2 + 0] = s5;
        dst[5 * 2 + 1] = 128;
        dst[6 * 2 + 0] = s6;
        dst[6 * 2 + 1] = 128;
        dst[7 * 2 + 0] = s7;
        dst[7 * 2 + 1] = 128;
        dst[8 * 2 + 0] = s8;
        dst[8 * 2 + 1] = 128;
        dst[9 * 2 + 0] = s9;
        dst[9 * 2 + 1] = 128;
        dst[10 * 2 + 0] = s10;
        dst[10 * 2 + 1] = 128;
        dst[11 * 2 + 0] = s11;
        dst[11 * 2 + 1] = 128;
        dst[12 * 2 + 0] = s12;
        dst[12 * 2 + 1] = 128;
        dst[13 * 2 + 0] = s13;
        dst[13 * 2 + 1] = 128;
        dst[14 * 2 + 0] = s14;
        dst[14 * 2 + 1] = 128;
        dst[15 * 2 + 0] = s15;
        dst[15 * 2 + 1] = 128;

        src += 16;
        dst += 32;
    }
}

static unsigned char uvc_stream_on;
static unsigned char uvc_buf_use;
static thread_ptr_t uvc_thread;

struct camera_device *camera;
struct camera_info *camera_info;
static volatile int format_width;
static int camera_width;
static int camera_height;
static int src_is_bayer16;
static int dest_is_bayer16;
static int src_is_grey;
static int dest_is_grey;
static int dest_is_yuyv;

static void uvc_connect_callback(int connect)
{
    printf("uvc_connect_callback %d\n", connect);
}

static void uvc_format_callback(const struct uvc_video_format *format)
{
    char *data = (char *)&format->fcc;
    format_width = format->width;
    printf("uvc format: %c%c%c%c, width %d, height %d, fps %d\n", data[0], data[1], data[2], data[3], format->width,  format->height,  format->fps);
}

static int uvc_stream_callback(int enable)
{
    printf("uvc_stream_callback %d\n", enable);
    uvc_stream_on = enable;
    if (enable)
        thread_wakeup(uvc_thread);

    return 0;
}

static void uvc_buf_complete(struct uvc_buffer *buf)
{
    if (buf->state != UVC_BUF_STATE_DONE)
        printf("%s: data not transmitted\n", __func__);

    uvc_buf_use = 0;
    thread_wakeup(uvc_thread);
}

static int camera_is_on = 0;

static int power_on_m_camera(void)
{
    int ret;

    if (camera_is_on)
        return 0;

    ret = camera_power_on(camera);
    if (ret < 0) {
        printf("camera failed to power on\n");
        return ret;
    }

    ret = camera_stream_on(camera);
    if (ret < 0) {
        printf("camera failed to stream on\n");
        camera_power_off(camera);
        return ret;
    }

    camera_is_on = 1;

    return 0;
}

static void power_off_m_camera(void)
{
    if (!camera_is_on)
        return;

    camera_stream_off(camera);
    camera_power_off(camera);
    camera_is_on = 0;
}

static void usb_gadget_uvc_thread(void *data)
{
    int ret;
    void *mem;
    struct uvc_buffer uvc_buf;
    uvc_buf.complete = uvc_buf_complete;
    uvc_buf.mem = NULL;

    void *yuyv_mem = NULL;

    if (src_is_grey && dest_is_yuyv) {
        yuyv_mem = malloc(camera_width*camera_height*2);
        assert(yuyv_mem);
    }

    while (1) {
        thread_wait();

        if (uvc_stream_on) {
            if (power_on_m_camera())
                continue;

            while (uvc_stream_on)
            {
                while (uvc_buf_use)
                    thread_wait();

                if (uvc_buf.mem && !yuyv_mem) {
                    camera_put_frame(camera, (void *)uvc_buf.mem);
                    uvc_buf.mem = NULL;
                }

                if (!uvc_stream_on)
                    break;

                mem = camera_wait_frame(camera);
                if (mem == NULL) {
                    camera_frame_error_type err = camera_get_frame_error(camera);
                    printf("failed to get frame: %d\n", err);
                    if (err == camera_error_dma_error) {
                        camera_stream_off(camera);
                        camera_stream_on(camera);
                    } else {
                        camera_power_off(camera);
                        camera_power_on(camera);
                    }
                    continue;
                }
                if (src_is_bayer16 && dest_is_yuyv)
                    bayer16_to_yuyv(mem, mem, camera_width, camera_height);

                if (src_is_bayer16 && dest_is_grey)
                    bayer16_to_bayer8(mem, mem, camera_width, camera_height);

                if (src_is_grey && dest_is_yuyv)
                    grey_to_yuyv(yuyv_mem, mem, camera_width, camera_height);

                if (!uvc_stream_on) {
                    camera_put_frame(camera, mem);
                    break;
                }

                int bpp = dest_is_yuyv ? 16 : 8;
                uvc_buf.mem = yuyv_mem ? yuyv_mem : mem;
                uvc_buf.length = format_width * camera_height * bpp/8;
                uvc_buf_use = 1;
                ret = gadget_uvc_write(&uvc_buf, 0, 0);
                if (ret < 0) {
                    uvc_buf_use = 0;
                    uvc_buf.mem = NULL;
                    camera_put_frame(camera, mem);
                }

                if (yuyv_mem)
                    camera_put_frame(camera, mem);
            }

            power_off_m_camera();
        }
    }
}

struct uvc_callback callback = {
    .format_cb = uvc_format_callback,
    .stream_cb = uvc_stream_callback,
    .connect_cb = uvc_connect_callback,
};

int gadget_usb_uvc_with_camera(int index)
{
    camera = camera_detect(index);
    if (!camera) {
        printf("camera not found\n");
        return 0;
    }

    camera_info = camera_get_info(camera);
    assert(camera_info);

    printf("camera found %s (%dx%d) %d\n",
         camera_info->name, camera_info->width, camera_info->height, camera_info->frame_align_size);

    camera_width = camera_info->width;
    camera_height = camera_info->height;

    switch (camera_info->data_fmt) {
    case CAMERA_PIX_FMT_SBGGR16:
    case CAMERA_PIX_FMT_SGBRG16:
    case CAMERA_PIX_FMT_SGRBG16:
    case CAMERA_PIX_FMT_SRGGB16:
        src_is_bayer16 = 1;
        dest_is_yuyv = 1; // 或者 dest_is_grey = 1, 查看 grey 数据, 注意windows工具不能看grey
        break;
    case CAMERA_PIX_FMT_YUYV:
    case CAMERA_PIX_FMT_YYUV:
    case CAMERA_PIX_FMT_YVYU:
    case CAMERA_PIX_FMT_UYVY:
    case CAMERA_PIX_FMT_VYUY:
        dest_is_yuyv = 1;
        break;
    case CAMERA_PIX_FMT_GREY:
        src_is_grey = 1;
        dest_is_yuyv = 1; // 或者 dest_is_grey = 1, 查看 grey 数据, 注意windows工具不能看grey
        break;
    default:
        panic("this format currently not support: 0x%x\n", camera_info->data_fmt);
    }

    uvc_frames[0].width = camera_width;
    uvc_frames[0].height = camera_height;

    if (!dest_is_yuyv) {
        uvc_frame_format[0].fcc = V4L2_PIX_FMT_GREY;
        uvc_frame_format[0].bpp = 8;
    } else {
        uvc_frame_format[0].fcc = V4L2_PIX_FMT_YUYV;
        uvc_frame_format[0].bpp = 16;
    }

    gadget_uvc_init(&usb_id, &uvc_config, &callback);

    uvc_thread = thread_create("usb gadget uvc thread", 8192, usb_gadget_uvc_thread, NULL);

    return 0;
}
