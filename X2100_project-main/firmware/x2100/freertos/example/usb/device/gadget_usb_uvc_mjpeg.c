#include <common.h>
#include <os.h>
#include <errno.h>
#include <usb/gadget_uvc.h>

#include <include_bin.h>

INCBIN(jpg_640_360_1, "example/usb/device/jpg/1_640_360.jpg");
INCBIN(jpg_640_480_1, "example/usb/device/jpg/1_640_480.jpg");
INCBIN(jpg_1280_720_1, "example/usb/device/jpg/1_1280_720.jpg");
INCBIN(jpg_1920_1080_1, "example/usb/device/jpg/1_1920_1080.jpg");
INCBIN(jpg_640_360_2, "example/usb/device/jpg/2_640_360.jpg");
INCBIN(jpg_640_480_2, "example/usb/device/jpg/2_640_480.jpg");
INCBIN(jpg_1280_720_2, "example/usb/device/jpg/2_1280_720.jpg");
INCBIN(jpg_1920_1080_2, "example/usb/device/jpg/2_1920_1080.jpg");
INCBIN(jpg_640_360_3, "example/usb/device/jpg/3_640_360.jpg");
INCBIN(jpg_640_480_3, "example/usb/device/jpg/3_640_480.jpg");
INCBIN(jpg_1280_720_3, "example/usb/device/jpg/3_1280_720.jpg");
INCBIN(jpg_1920_1080_3, "example/usb/device/jpg/3_1920_1080.jpg");
INCBIN(jpg_640_360_4, "example/usb/device/jpg/4_640_360.jpg");
INCBIN(jpg_640_480_4, "example/usb/device/jpg/4_640_480.jpg");
INCBIN(jpg_1280_720_4, "example/usb/device/jpg/4_1280_720.jpg");
INCBIN(jpg_1920_1080_4, "example/usb/device/jpg/4_1920_1080.jpg");

#define WEBCAM_VENDOR_ID                0x1d6b    /* Linux Foundation */
#define WEBCAM_PRODUCT_ID               0x0102    /* Webcam A/V gadget */

/*  Set GADGET_UVC_TX_FRAME_COUNT >= 2 for best uvc frame rate performance. */
#ifndef GADGET_UVC_TX_FRAME_COUNT
#define GADGET_UVC_TX_FRAME_COUNT       2
#endif

#if (GADGET_UVC_TX_FRAME_COUNT < 2) || (GADGET_UVC_TX_FRAME_COUNT > 8)
#error "GADGET_UVC_TX_FRAME_COUNT must be between 2 and 8 for best uvc frame rate performance."
#endif

struct mjpeg_buf {
    const void *mem;
    unsigned int length;
};

struct gadget_uvc_frame_source {
    const void *src_mem;
    uint32_t src_len;
    uint32_t width;
    uint32_t height;
};

struct gadget_uvc_mjpeg_state {
    uint32_t width;
    uint32_t height;
    uint32_t fps_delay;
    uint32_t jpg_offset;
};

struct gadget_uvc_tx_frame {
    struct list_head list;
    struct uvc_buffer uvc_buf;
    void *frame_mem;
    uint32_t capacity;
    uint32_t frame_len;
    uint32_t tx_frame_id;
};

static void uvc_buf_complete(struct uvc_buffer *buf);

struct mjpeg_buf mjpeg_640_360[4];
struct mjpeg_buf mjpeg_640_480[4];
struct mjpeg_buf mjpeg_1280_720[4];
struct mjpeg_buf mjpeg_1920_1080[4];

static unsigned char uvc_stream_flag;
static thread_ptr_t uvc_thread;
static uint32_t uvc_tx_frame_capacity;
static unsigned char uvc_tx_frames_initialized;
static struct gadget_uvc_mjpeg_state uvc_mjpeg_state;
static struct gadget_uvc_tx_frame uvc_tx_frames[GADGET_UVC_TX_FRAME_COUNT];
static LIST_HEAD(uvc_free_tx_frames);
static LIST_HEAD(uvc_busy_tx_frames);

static struct gadget_id usb_id = {
    .vendor_id = WEBCAM_VENDOR_ID,
    .product_id = WEBCAM_PRODUCT_ID,
};

/* fps large to small Sort*/
static const unsigned int frame_fps[] = {
    15, 10, 2,
};

/* Frame size small to large sort*/
static const struct uvc_frame_config uvc_mjpeg_frames[] = {
    {
        .width = 640,
        .height = 360,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
    {
        .width = 640,
        .height = 480,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
    {
        .width = 1280,
        .height = 720,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
    {
        .width = 1920,
        .height = 1080,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
};

/* Format small to large sort according to pixel size */
static const struct uvc_format_config uvc_frame_format[]= {
    {
        .fcc = V4L2_PIX_FMT_MJPEG,
        .bpp = 8,
        .frames_num = ARRAY_SIZE(uvc_mjpeg_frames),
        .frames = uvc_mjpeg_frames,
    },
};

static const struct uvc_device_config uvc_config = {
    .format_num = ARRAY_SIZE(uvc_frame_format),
    .formats = uvc_frame_format,
};

static uint32_t gadget_uvc_get_max_frame_length(void)
{
    uint32_t max_length = jpg_640_360_1Size;

    max_length = max_t(uint32_t, max_length, jpg_640_360_2Size);
    max_length = max_t(uint32_t, max_length, jpg_640_360_3Size);
    max_length = max_t(uint32_t, max_length, jpg_640_360_4Size);
    max_length = max_t(uint32_t, max_length, jpg_640_480_1Size);
    max_length = max_t(uint32_t, max_length, jpg_640_480_2Size);
    max_length = max_t(uint32_t, max_length, jpg_640_480_3Size);
    max_length = max_t(uint32_t, max_length, jpg_640_480_4Size);
    max_length = max_t(uint32_t, max_length, jpg_1280_720_1Size);
    max_length = max_t(uint32_t, max_length, jpg_1280_720_2Size);
    max_length = max_t(uint32_t, max_length, jpg_1280_720_3Size);
    max_length = max_t(uint32_t, max_length, jpg_1280_720_4Size);
    max_length = max_t(uint32_t, max_length, jpg_1920_1080_1Size);
    max_length = max_t(uint32_t, max_length, jpg_1920_1080_2Size);
    max_length = max_t(uint32_t, max_length, jpg_1920_1080_3Size);
    max_length = max_t(uint32_t, max_length, jpg_1920_1080_4Size);

    return max_length;
}

static void gadget_uvc_set_mjpeg_state(const struct gadget_uvc_mjpeg_state *state)
{
    os_enter_critical();
    uvc_mjpeg_state = *state;
    os_exit_critical();
}

static void gadget_uvc_get_mjpeg_state(struct gadget_uvc_mjpeg_state *state)
{
    os_enter_critical();
    *state = uvc_mjpeg_state;
    os_exit_critical();
}

static void gadget_uvc_advance_mjpeg_offset(void)
{
    os_enter_critical();
    uvc_mjpeg_state.jpg_offset++;
    os_exit_critical();
}

static int gadget_uvc_select_mjpeg_source(const struct gadget_uvc_mjpeg_state *state,
        struct gadget_uvc_frame_source *source)
{
    const struct mjpeg_buf *mjpeg = NULL;
    unsigned int mjpeg_num = 0;
    unsigned int offset;

    memset(source, 0, sizeof(*source));
    source->width = state->width;
    source->height = state->height;

    if (state->width == 640 && state->height == 360) {
        mjpeg = mjpeg_640_360;
        mjpeg_num = ARRAY_SIZE(mjpeg_640_360);
    } else if (state->width == 640 && state->height == 480) {
        mjpeg = mjpeg_640_480;
        mjpeg_num = ARRAY_SIZE(mjpeg_640_480);
    } else if (state->width == 1280 && state->height == 720) {
        mjpeg = mjpeg_1280_720;
        mjpeg_num = ARRAY_SIZE(mjpeg_1280_720);
    } else if (state->width == 1920 && state->height == 1080) {
        mjpeg = mjpeg_1920_1080;
        mjpeg_num = ARRAY_SIZE(mjpeg_1920_1080);
    } else {
        return -EINVAL;
    }

    if (mjpeg_num == 0)
        return -ENODATA;

    offset = state->jpg_offset % mjpeg_num;
    source->src_mem = mjpeg[offset].mem;
    source->src_len = mjpeg[offset].length;

    if (source->src_mem == NULL || source->src_len == 0)
        return -ENODATA;

    return 0;
}

static struct gadget_uvc_tx_frame *gadget_uvc_get_free_tx_frame(void)
{
    struct gadget_uvc_tx_frame *tx_frame = NULL;

    os_enter_critical();
    if (!list_empty(&uvc_free_tx_frames)) {
        tx_frame = list_first_entry(&uvc_free_tx_frames, struct gadget_uvc_tx_frame, list);
        list_move_tail(&tx_frame->list, &uvc_busy_tx_frames);
    }
    os_exit_critical();

    return tx_frame;
}

static void gadget_uvc_put_tx_frame(struct gadget_uvc_tx_frame *tx_frame)
{
    os_enter_critical();
    list_move_tail(&tx_frame->list, &uvc_free_tx_frames);
    os_exit_critical();
}

static int gadget_uvc_prepare_tx_frame(struct gadget_uvc_tx_frame *tx_frame, unsigned int *fps_delay)
{
    struct gadget_uvc_frame_source source;
    struct gadget_uvc_mjpeg_state state;
    int ret;

    gadget_uvc_get_mjpeg_state(&state);
    if (state.width == 0 || state.height == 0)
        return -ENODATA;

    ret = gadget_uvc_select_mjpeg_source(&state, &source);
    if (ret < 0)
        return ret;

    if (source.src_len > tx_frame->capacity) {
        printf("%s: frame len %u over capacity %u\n", __func__, source.src_len, tx_frame->capacity);
        return -EOVERFLOW;
    }

    /*
    * The MJPEG test data is immutable, so each tx frame only needs an independent
    * uvc_buffer carrier. There is no need to copy the JPEG payload for every send.
    */
    tx_frame->uvc_buf.mem = (void *)source.src_mem;
    tx_frame->frame_len = source.src_len;
    tx_frame->uvc_buf.length = source.src_len;
    tx_frame->uvc_buf.bytesused = 0;
    tx_frame->uvc_buf.timestamp = 0;
    *fps_delay = state.fps_delay;

    return 0;
}

static int gadget_uvc_tx_frames_init(void)
{
    unsigned int i;

    if (uvc_tx_frames_initialized)
        return 0;

    uvc_tx_frame_capacity = gadget_uvc_get_max_frame_length();
    memset(&uvc_mjpeg_state, 0, sizeof(uvc_mjpeg_state));

    INIT_LIST_HEAD(&uvc_free_tx_frames);
    INIT_LIST_HEAD(&uvc_busy_tx_frames);

    for (i = 0; i < GADGET_UVC_TX_FRAME_COUNT; i++) {
        struct gadget_uvc_tx_frame *tx_frame = &uvc_tx_frames[i];

        memset(tx_frame, 0, sizeof(*tx_frame));
        INIT_LIST_HEAD(&tx_frame->list);

        tx_frame->frame_mem = malloc(uvc_tx_frame_capacity);
        if (tx_frame->frame_mem == NULL) {
            printf("%s: malloc %u failed at tx frame %u\n", __func__, uvc_tx_frame_capacity, i);
            goto err;
        }

        tx_frame->uvc_buf.mem = tx_frame->frame_mem;
        tx_frame->uvc_buf.complete = uvc_buf_complete;
        tx_frame->uvc_buf.private_data = tx_frame;
        tx_frame->capacity = uvc_tx_frame_capacity;
        tx_frame->tx_frame_id = i;

        list_add_tail(&tx_frame->list, &uvc_free_tx_frames);
    }

    uvc_tx_frames_initialized = 1;
    return 0;

err:
    while (i--) {
        free(uvc_tx_frames[i].frame_mem);
        uvc_tx_frames[i].frame_mem = NULL;
        INIT_LIST_HEAD(&uvc_tx_frames[i].list);
    }

    return -ENOMEM;
}

static void uvc_connect_callback(int connect)
{
    printf("uvc_connect_callback %d\n", connect);
}

static void uvc_format_callback(const struct uvc_video_format *format)
{
    char *data = (char *)&format->fcc;
    struct gadget_uvc_mjpeg_state state;

    printf("uvc format: %c%c%c%c, width %d, height %d, fps %d\n",
            data[0], data[1], data[2], data[3], format->width, format->height, format->fps);

    if (format->fcc != V4L2_PIX_FMT_MJPEG)
        assert(0);

    memset(&state, 0, sizeof(state));
    state.width = format->width;
    state.height = format->height;
    state.fps_delay = 1000 / format->fps;
    state.jpg_offset = 0;
    gadget_uvc_set_mjpeg_state(&state);

    if (uvc_thread)
        thread_wakeup(uvc_thread);
}

static int uvc_stream_callback(int enable)
{
    printf("uvc_stream_callback %d\n", enable);
    uvc_stream_flag = enable;
    if (enable)
        thread_wakeup(uvc_thread);

    return 0;
}

static void uvc_buf_complete(struct uvc_buffer *buf)
{
    struct gadget_uvc_tx_frame *tx_frame = buf->private_data;

    if (buf->state != UVC_BUF_STATE_DONE)
        printf("%s: tx frame %d data not transmitted\n", __func__, tx_frame ? tx_frame->tx_frame_id : -1);

    if (tx_frame)
        gadget_uvc_put_tx_frame(tx_frame);

    if (uvc_thread)
        thread_wakeup(uvc_thread);
}

static void gadget_uvc_send_frames(void)
{
    int ret;
    unsigned int fps_delay;
    struct gadget_uvc_tx_frame *tx_frame = NULL;

    while (uvc_stream_flag) {
        tx_frame = gadget_uvc_get_free_tx_frame();
        if (tx_frame == NULL)
            break;

        ret = gadget_uvc_prepare_tx_frame(tx_frame, &fps_delay);
        if (ret < 0) {
            gadget_uvc_put_tx_frame(tx_frame);
            if (ret != -ENODATA)
                printf("%s: prepare tx frame %u failed %d\n", __func__, tx_frame->tx_frame_id, ret);
            break;
        }

        if (!uvc_stream_flag) {
            gadget_uvc_put_tx_frame(tx_frame);
            break;
        }

        tx_frame->uvc_buf.timestamp = systick_get_time_us();
        ret = gadget_uvc_write(&tx_frame->uvc_buf, 0, 0);
        if (ret < 0) {
            gadget_uvc_put_tx_frame(tx_frame);
            if (ret != -EAGAIN && ret != -ENOTCONN && ret != -ENOLINK)
                printf("%s: write tx frame %u failed %d\n", __func__, tx_frame->tx_frame_id, ret);
            break;
        }

        gadget_uvc_advance_mjpeg_offset();
        if (fps_delay)
            mdelay(fps_delay);
    }
}

static void usb_gadget_uvc_thread(void *data)
{
    while (1) {
        thread_wait();
        if (!uvc_stream_flag)
            continue;

        gadget_uvc_send_frames();
    }
}

struct uvc_callback callback = {
    .format_cb = uvc_format_callback,
    .stream_cb = uvc_stream_callback,
    .connect_cb = uvc_connect_callback,
};

int gadget_usb_uvc_mjpeg_test(void)
{
    int ret;

    mjpeg_640_360[0].length = jpg_640_360_1Size;
    mjpeg_640_360[0].mem = jpg_640_360_1Data;
    mjpeg_640_360[1].length = jpg_640_360_2Size;
    mjpeg_640_360[1].mem = jpg_640_360_2Data;
    mjpeg_640_360[2].length = jpg_640_360_3Size;
    mjpeg_640_360[2].mem = jpg_640_360_3Data;
    mjpeg_640_360[3].length = jpg_640_360_4Size;
    mjpeg_640_360[3].mem = jpg_640_360_4Data;

    mjpeg_640_480[0].length = jpg_640_480_1Size;
    mjpeg_640_480[0].mem = jpg_640_480_1Data;
    mjpeg_640_480[1].length = jpg_640_480_2Size;
    mjpeg_640_480[1].mem = jpg_640_480_2Data;
    mjpeg_640_480[2].length = jpg_640_480_3Size;
    mjpeg_640_480[2].mem = jpg_640_480_3Data;
    mjpeg_640_480[3].length = jpg_640_480_4Size;
    mjpeg_640_480[3].mem = jpg_640_480_4Data;

    mjpeg_1280_720[0].length = jpg_1280_720_1Size;
    mjpeg_1280_720[0].mem = jpg_1280_720_1Data;
    mjpeg_1280_720[1].length = jpg_1280_720_2Size;
    mjpeg_1280_720[1].mem = jpg_1280_720_2Data;
    mjpeg_1280_720[2].length = jpg_1280_720_3Size;
    mjpeg_1280_720[2].mem = jpg_1280_720_3Data;
    mjpeg_1280_720[3].length = jpg_1280_720_4Size;
    mjpeg_1280_720[3].mem = jpg_1280_720_4Data;

    mjpeg_1920_1080[0].length = jpg_1920_1080_1Size;
    mjpeg_1920_1080[0].mem = jpg_1920_1080_1Data;
    mjpeg_1920_1080[1].length = jpg_1920_1080_2Size;
    mjpeg_1920_1080[1].mem = jpg_1920_1080_2Data;
    mjpeg_1920_1080[2].length = jpg_1920_1080_3Size;
    mjpeg_1920_1080[2].mem = jpg_1920_1080_3Data;
    mjpeg_1920_1080[3].length = jpg_1920_1080_4Size;
    mjpeg_1920_1080[3].mem = jpg_1920_1080_4Data;

    ret = gadget_uvc_tx_frames_init();
    if (ret < 0)
        return ret;

    gadget_uvc_init(&usb_id, &uvc_config, &callback);
    uvc_thread = thread_create("usb gadget uvc thread", 8192, usb_gadget_uvc_thread, NULL);
    return 0;
}
