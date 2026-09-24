#include <common.h>
#include <os.h>
#include <errno.h>
#include <usb/gadget_uvc_n.h>

#include <include_bin.h>

#define MY_UVC_COUNT    4

INCBIN(yuyv_640_360, "example/usb/device/logo_640_360_yuyv.yuv");
INCBIN(yuyv_1280_720, "example/usb/device/logo_1280_720_yuyv.yuv");
INCBIN(nv12_640_360, "example/usb/device/logo_640_360_nv12.yuv");
INCBIN(bgr_640_360, "example/usb/device/logo_640_360_bgr24.rgb");
INCBIN(y8_640_360, "example/usb/device/logo_640_360_y8.yuv");

#define WEBCAM_VENDOR_ID        0x1d6b    /* Linux Foundation */
#define WEBCAM_PRODUCT_ID       0x0102    /* Webcam A/V gadget */

/*  Set GADGET_UVC_TX_FRAME_COUNT >= 2 for best uvc frame rate performance. */
#ifndef GADGET_UVC_TX_FRAME_COUNT
#define GADGET_UVC_TX_FRAME_COUNT       2
#endif

#if (GADGET_UVC_TX_FRAME_COUNT < 2) || (GADGET_UVC_TX_FRAME_COUNT > 8)
#error "GADGET_UVC_TX_FRAME_COUNT must be between 2 and 8 for best uvc frame rate performance."
#endif

struct gadget_uvc_frame_source {
    const void *src_mem;
    uint32_t src_len;
    uint32_t fcc;
    uint32_t width;
    uint32_t height;
};

struct gadget_uvc_tx_frame {
    struct list_head list;
    struct uvc_buffer uvc_buf;
    void *frame_mem;
    uint32_t capacity;
    uint32_t frame_len;
    uint32_t tx_frame_id;
    uint32_t uvc_id;
};

static void uvc_buf_complete(struct uvc_buffer *buf);

static unsigned char uvc_stream_flag[MY_UVC_COUNT];
static thread_ptr_t uvc_thread[MY_UVC_COUNT];
static uint32_t uvc_tx_frame_capacity;
static unsigned char uvc_tx_frames_initialized;
static struct gadget_uvc_frame_source uvc_frame_source[MY_UVC_COUNT];
static struct gadget_uvc_tx_frame uvc_tx_frames[MY_UVC_COUNT][GADGET_UVC_TX_FRAME_COUNT];
static struct list_head uvc_free_tx_frames[MY_UVC_COUNT];
static struct list_head uvc_busy_tx_frames[MY_UVC_COUNT];

static struct gadget_id usb_id = {
    .vendor_id = WEBCAM_VENDOR_ID,
    .product_id = WEBCAM_PRODUCT_ID,
};

/* fps large to small Sort*/
static const unsigned int frame_fps[] = {
    10,
};

/* Frame size small to large sort*/
static const struct uvc_frame_config uvc_yuyv_frames[] = {
    {
        .width = 640,
        .height = 360,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
    {
        .width = 1280,
        .height = 720,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
};

/* Frame size small to large sort*/
static const struct uvc_frame_config uvc_nv12_frames[] = {
    {
        .width = 640,
        .height = 360,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
};

/* Frame size small to large sort*/
static const struct uvc_frame_config uvc_y8_frames[] = {
    {
        .width = 640,
        .height = 360,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
};

/* Frame size small to large sort*/
static const struct uvc_frame_config uvc_bgr_frames[] = {
    {
        .width = 640,
        .height = 360,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
};

/* Format small to large sort according to pixel size */
static const struct uvc_format_config uvc_frame_format[]= {
    {
        .fcc = V4L2_PIX_FMT_GREY,
        .bpp = 8,
        .frames_num = ARRAY_SIZE(uvc_y8_frames),
        .frames = uvc_y8_frames,
    },
    {
        .fcc = V4L2_PIX_FMT_NV12,
        .bpp = 12,
        .frames_num = ARRAY_SIZE(uvc_nv12_frames),
        .frames = uvc_nv12_frames,
    },
    {
        .fcc = V4L2_PIX_FMT_YUYV,
        .bpp = 16,
        .frames_num = ARRAY_SIZE(uvc_yuyv_frames),
        .frames = uvc_yuyv_frames,
    },
    {
        .fcc = V4L2_PIX_FMT_BGR24,
        .bpp = 24,
        .frames_num = ARRAY_SIZE(uvc_bgr_frames),
        .frames = uvc_bgr_frames,
    },
};

static uint32_t gadget_uvc_get_max_frame_length(void)
{
    uint32_t max_length = y8_640_360Size;

    max_length = max_t(uint32_t, max_length, nv12_640_360Size);
    max_length = max_t(uint32_t, max_length, yuyv_640_360Size);
    max_length = max_t(uint32_t, max_length, yuyv_1280_720Size);
    max_length = max_t(uint32_t, max_length, bgr_640_360Size);

    return max_length;
}

static int gadget_uvc_select_frame_source(const struct uvc_video_format *format,
        struct gadget_uvc_frame_source *source)
{
    memset(source, 0, sizeof(*source));
    source->fcc = format->fcc;
    source->width = format->width;
    source->height = format->height;

    if (format->fcc == V4L2_PIX_FMT_YUYV) {
        if (format->width == 640 && format->height == 360) {
            source->src_mem = yuyv_640_360Data;
            source->src_len = yuyv_640_360Size;
        } else if (format->width == 1280 && format->height == 720) {
            source->src_mem = yuyv_1280_720Data;
            source->src_len = yuyv_1280_720Size;
        } else {
            return -EINVAL;
        }
    } else if (format->fcc == V4L2_PIX_FMT_NV12) {
        if (format->width == 640 && format->height == 360) {
            source->src_mem = nv12_640_360Data;
            source->src_len = nv12_640_360Size;
        } else {
            return -EINVAL;
        }
    } else if (format->fcc == V4L2_PIX_FMT_GREY) {
        if (format->width == 640 && format->height == 360) {
            source->src_mem = y8_640_360Data;
            source->src_len = y8_640_360Size;
        } else {
            return -EINVAL;
        }
    } else if (format->fcc == V4L2_PIX_FMT_BGR24) {
        if (format->width == 640 && format->height == 360) {
            source->src_mem = bgr_640_360Data;
            source->src_len = bgr_640_360Size;
        } else {
            return -EINVAL;
        }
    } else {
        return -EINVAL;
    }

    return 0;
}

static void gadget_uvc_set_frame_source(uint32_t uvc_id,
        const struct gadget_uvc_frame_source *source)
{
    os_enter_critical();
    uvc_frame_source[uvc_id] = *source;
    os_exit_critical();
}

static void gadget_uvc_get_frame_source(uint32_t uvc_id,
        struct gadget_uvc_frame_source *source)
{
    os_enter_critical();
    *source = uvc_frame_source[uvc_id];
    os_exit_critical();
}

static struct gadget_uvc_tx_frame *gadget_uvc_get_free_tx_frame(uint32_t uvc_id)
{
    struct gadget_uvc_tx_frame *tx_frame = NULL;

    os_enter_critical();
    if (!list_empty(&uvc_free_tx_frames[uvc_id])) {
        tx_frame = list_first_entry(&uvc_free_tx_frames[uvc_id], struct gadget_uvc_tx_frame, list);
        list_move_tail(&tx_frame->list, &uvc_busy_tx_frames[uvc_id]);
    }
    os_exit_critical();

    return tx_frame;
}

static void gadget_uvc_put_tx_frame(struct gadget_uvc_tx_frame *tx_frame)
{
    uint32_t uvc_id = tx_frame->uvc_id;

    os_enter_critical();
    list_move_tail(&tx_frame->list, &uvc_free_tx_frames[uvc_id]);
    os_exit_critical();
}

static int gadget_uvc_prepare_tx_frame(struct gadget_uvc_tx_frame *tx_frame)
{
    struct gadget_uvc_frame_source source;

    gadget_uvc_get_frame_source(tx_frame->uvc_id, &source);
    if (source.src_mem == NULL || source.src_len == 0)
        return -ENODATA;

    if (source.src_len > tx_frame->capacity) {
        printf("%s: uvc %u frame len %u over capacity %u\n",
                __func__, tx_frame->uvc_id, source.src_len, tx_frame->capacity);
        return -EOVERFLOW;
    }

    /*
    * The source frames in this example are immutable test assets, so each tx frame
    * only needs an independent uvc_buffer carrier and does not need to copy payload.
    */
    tx_frame->uvc_buf.mem = (void *)source.src_mem;
    tx_frame->frame_len = source.src_len;
    tx_frame->uvc_buf.length = source.src_len;
    tx_frame->uvc_buf.bytesused = 0;
    tx_frame->uvc_buf.timestamp = 0;

    return 0;
}

static int gadget_uvc_tx_frames_init(void)
{
    unsigned int uvc_id;
    unsigned int i;

    if (uvc_tx_frames_initialized)
        return 0;

    uvc_tx_frame_capacity = gadget_uvc_get_max_frame_length();
    memset(uvc_frame_source, 0, sizeof(uvc_frame_source));

    for (uvc_id = 0; uvc_id < MY_UVC_COUNT; uvc_id++) {
        INIT_LIST_HEAD(&uvc_free_tx_frames[uvc_id]);
        INIT_LIST_HEAD(&uvc_busy_tx_frames[uvc_id]);

        for (i = 0; i < GADGET_UVC_TX_FRAME_COUNT; i++) {
            struct gadget_uvc_tx_frame *tx_frame = &uvc_tx_frames[uvc_id][i];

            memset(tx_frame, 0, sizeof(*tx_frame));
            INIT_LIST_HEAD(&tx_frame->list);

            tx_frame->frame_mem = malloc(uvc_tx_frame_capacity);
            if (tx_frame->frame_mem == NULL) {
                printf("%s: malloc %u failed at uvc %u tx frame %u\n",
                        __func__, uvc_tx_frame_capacity, uvc_id, i);
                goto err;
            }

            tx_frame->uvc_buf.mem = tx_frame->frame_mem;
            tx_frame->uvc_buf.complete = uvc_buf_complete;
            tx_frame->uvc_buf.private_data = tx_frame;
            tx_frame->capacity = uvc_tx_frame_capacity;
            tx_frame->tx_frame_id = i;
            tx_frame->uvc_id = uvc_id;

            list_add_tail(&tx_frame->list, &uvc_free_tx_frames[uvc_id]);
        }
    }

    uvc_tx_frames_initialized = 1;
    return 0;

err:
    for (uvc_id = 0; uvc_id < MY_UVC_COUNT; uvc_id++) {
        for (i = 0; i < GADGET_UVC_TX_FRAME_COUNT; i++) {
            free(uvc_tx_frames[uvc_id][i].frame_mem);
            uvc_tx_frames[uvc_id][i].frame_mem = NULL;
            INIT_LIST_HEAD(&uvc_tx_frames[uvc_id][i].list);
        }
    }

    return -ENOMEM;
}

static void uvc_format_callback(uint32_t uvc_id, const struct uvc_video_format *format)
{
    char *data = (char *)&format->fcc;
    struct gadget_uvc_frame_source source;
    int ret;

    printf("uvc %d format: %c%c%c%c, width %d, height %d, fps %d\n", uvc_id,
        data[0], data[1], data[2], data[3], format->width,  format->height,  format->fps);

    ret = gadget_uvc_select_frame_source(format, &source);
    if (ret < 0) {
        printf("%s: unsupported uvc %u format %c%c%c%c %ux%u\n",
                __func__, uvc_id, data[0], data[1], data[2], data[3], format->width, format->height);
        assert(0);
    }

    gadget_uvc_set_frame_source(uvc_id, &source);
    if (uvc_thread[uvc_id])
        thread_wakeup(uvc_thread[uvc_id]);
}

static int uvc_stream_callback(uint32_t uvc_id, int enable)
{
    printf("uvc_stream_callback %d %d\n", uvc_id, enable);
    uvc_stream_flag[uvc_id] = enable;
    if (enable)
        thread_wakeup(uvc_thread[uvc_id]);

    return 0;
}

static void uvc_buf_complete(struct uvc_buffer *buf)
{
    struct gadget_uvc_tx_frame *tx_frame = buf->private_data;
    int uvc_id = tx_frame ? tx_frame->uvc_id : -1;

    if (buf->state != UVC_BUF_STATE_DONE)
        printf("%s: uvc %d tx frame %d data not transmitted\n",
                __func__, uvc_id, tx_frame ? tx_frame->tx_frame_id : -1);

    if (tx_frame)
        gadget_uvc_put_tx_frame(tx_frame);

    if (uvc_thread[uvc_id])
        thread_wakeup(uvc_thread[uvc_id]);
}

static void gadget_uvc_send_frames(uint32_t uvc_id)
{
    int ret;
    struct gadget_uvc_tx_frame *tx_frame = NULL;

    while (uvc_stream_flag[uvc_id]) {
        tx_frame = gadget_uvc_get_free_tx_frame(uvc_id);
        if (tx_frame == NULL)
            break;

        ret = gadget_uvc_prepare_tx_frame(tx_frame);
        if (ret < 0) {
            gadget_uvc_put_tx_frame(tx_frame);
            if (ret != -ENODATA)
                printf("%s: uvc %u prepare tx frame %u failed %d\n",
                        __func__, uvc_id, tx_frame->tx_frame_id, ret);
            break;
        }

        if (!uvc_stream_flag[uvc_id]) {
            gadget_uvc_put_tx_frame(tx_frame);
            break;
        }

        tx_frame->uvc_buf.timestamp = systick_get_time_us();
        ret = gadget_uvc_n_write(uvc_id, &tx_frame->uvc_buf, 0, 0);
        if (ret < 0) {
            gadget_uvc_put_tx_frame(tx_frame);
            if (ret != -EAGAIN && ret != -ENOTCONN && ret != -ENOLINK)
                printf("%s: uvc %u write tx frame %u failed %d\n",
                        __func__, uvc_id, tx_frame->tx_frame_id, ret);
            break;
        }
    }
}

static void usb_gadget_uvc_thread(void *data)
{
    int uvc_id = (int)data;

    while (1) {
        thread_wait();
        if (!uvc_stream_flag[uvc_id])
            continue;

        gadget_uvc_send_frames(uvc_id);
    }
}

struct uvc_callback callback = {
    .format_cb = uvc_format_callback,
    .stream_cb = uvc_stream_callback,
};

static struct uvc_device_config uvc_config[MY_UVC_COUNT];

int gadget_usb_uvc_n_test(void)
{
    int i;
    int ret;

    ret = gadget_uvc_tx_frames_init();
    if (ret < 0)
        return ret;

    for (i = 0; i < MY_UVC_COUNT; i++) {
        uvc_config[i].formats = uvc_frame_format;
        uvc_config[i].format_num = ARRAY_SIZE(uvc_frame_format);
        uvc_config[i].callback = &callback;
        uvc_thread[i] = thread_create("usb gadget uvc thread", 4096, usb_gadget_uvc_thread, (void *)i);
    }

    gadget_uvc_n_init(&usb_id, uvc_config, MY_UVC_COUNT);
    return 0;
}
