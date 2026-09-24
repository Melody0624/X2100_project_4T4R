
#include <common.h>
#include <os.h>
#include <errno.h>
#include <usb/gadget_uvc_hid.h>

#include <include_bin.h>

INCBIN(yuyv_640_360, "example/usb/device/logo_640_360_yuyv.yuv");
INCBIN(yuyv_1280_720, "example/usb/device/logo_1280_720_yuyv.yuv");
INCBIN(nv12_640_360, "example/usb/device/logo_640_360_nv12.yuv");
INCBIN(bgr_640_360, "example/usb/device/logo_640_360_bgr24.rgb");
INCBIN(y8_640_360, "example/usb/device/logo_640_360_y8.yuv");

#define WEBCAM_VENDOR_ID                0x1d6b    /* Linux Foundation */
#define WEBCAM_PRODUCT_ID               0x0102    /* Webcam A/V gadget */

/*  Set GADGET_UVC_TX_FRAME_COUNT >= 2 for best uvc frame rate performance. */
#ifndef GADGET_UVC_TX_FRAME_COUNT
#define GADGET_UVC_TX_FRAME_COUNT       2
#endif

#if (GADGET_UVC_TX_FRAME_COUNT < 2) || (GADGET_UVC_TX_FRAME_COUNT > 8)
#error "GADGET_UVC_TX_FRAME_COUNT must be between 2 and 8 for best uvc frame rate performance."
#endif

struct uvc_brightness_data {
    short res_value;
    short max_value;
    short min_value;
    short def_value;
    short cur_value;
};

struct uvc_contrast_data {
    short res_value;
    short max_value;
    short min_value;
    short def_value;
    short cur_value;
};

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
};

static void uvc_buf_complete(struct uvc_buffer *buf);

static unsigned char uvc_stream_flag;
static thread_ptr_t uvc_thread;
static uint32_t uvc_tx_frame_capacity;
static unsigned char uvc_tx_frames_initialized;
static struct gadget_uvc_frame_source uvc_frame_source;
static struct gadget_uvc_tx_frame uvc_tx_frames[GADGET_UVC_TX_FRAME_COUNT];
static LIST_HEAD(uvc_free_tx_frames);
static LIST_HEAD(uvc_busy_tx_frames);

static char uvc_auto_exposure_priority_status = 0;

static struct uvc_brightness_data brightness_data = {
    .res_value = 1,
    .max_value = 64,
    .min_value = -64,
    .def_value = 0,
    .cur_value = 0,
};

static struct uvc_contrast_data contrast_data = {
    .res_value = 1,
    .max_value = 64,
    .min_value = 0,
    .def_value = 32,
    .cur_value = 32,
};

static struct gadget_id usb_id = {
    .vendor_id = WEBCAM_VENDOR_ID,
    .product_id = WEBCAM_PRODUCT_ID,
};

/* fps large to small Sort*/
static const unsigned int frame_fps[] = {
    15, 10, 2,
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

static const struct uvc_device_config uvc_config = {
    .format_num = ARRAY_SIZE(uvc_frame_format),
    .formats = uvc_frame_format,
    .camera_param_config = UVC_PARAM_BRIGHTNESS | UVC_PARAM_CONTRAST,
    .camera_feature_config = UVC_FEATURE_AUTO_EXPOSURE_PRIORITY,
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

static void gadget_uvc_set_frame_source(const struct gadget_uvc_frame_source *source)
{
    os_enter_critical();
    uvc_frame_source = *source;
    os_exit_critical();
}

static void gadget_uvc_get_frame_source(struct gadget_uvc_frame_source *source)
{
    os_enter_critical();
    *source = uvc_frame_source;
    os_exit_critical();
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

static int gadget_uvc_prepare_tx_frame(struct gadget_uvc_tx_frame *tx_frame)
{
    struct gadget_uvc_frame_source source;

    gadget_uvc_get_frame_source(&source);
    if (source.src_mem == NULL || source.src_len == 0)
        return -ENODATA;

    if (source.src_len > tx_frame->capacity) {
        printf("%s: frame len %u over capacity %u\n", __func__, source.src_len, tx_frame->capacity);
        return -EOVERFLOW;
    }

    /*
    * CRITICAL: The content of 'tx_frame' and the memory pointed to by 'uvc_buf'
    * must remain unmodified until 'uvc_buf_complete' is called.
    * Premature modification will result in corrupted frame data and USB transfer errors.
    * However, the image data in this example is fixed.
    * This means that the data will not be modified during the transmission process.
    * Therefore, no actual data copying is performed here.
    */
    // memcpy(tx_frame->frame_mem, source.src_mem, source.src_len);
    // tx_frame->uvc_buf.mem = tx_frame->frame_mem;
    tx_frame->uvc_buf.mem = (void *)source.src_mem;
    tx_frame->frame_len = source.src_len;
    tx_frame->uvc_buf.length = source.src_len;
    tx_frame->uvc_buf.bytesused = 0;
    tx_frame->uvc_buf.timestamp = 0;

    return 0;
}

static int gadget_uvc_tx_frames_init(void)
{
    unsigned int i;

    if (uvc_tx_frames_initialized)
        return 0;

    uvc_tx_frame_capacity = gadget_uvc_get_max_frame_length();
    memset(&uvc_frame_source, 0, sizeof(uvc_frame_source));

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

static const unsigned char keyboard_report[] = {
    0x05, 0x01,    /* USAGE_PAGE (Generic Desktop)              */
    0x09, 0x06,    /* USAGE (Keyboard)                       */
    0xa1, 0x01,    /* COLLECTION (Application)               */
    0x05, 0x07,    /*   USAGE_PAGE (Keyboard)                */
    0x19, 0xe0,    /*   USAGE_MINIMUM (Keyboard LeftControl) */
    0x29, 0xe7,    /*   USAGE_MAXIMUM (Keyboard Right GUI)   */
    0x15, 0x00,    /*   LOGICAL_MINIMUM (0)                  */
    0x25, 0x01,    /*   LOGICAL_MAXIMUM (1)                  */
    0x75, 0x01,    /*   REPORT_SIZE (1)                      */
    0x95, 0x08,    /*   REPORT_COUNT (8)                     */
    0x81, 0x02,    /*   INPUT (Data,Var,Abs)                 */
    0x95, 0x01,    /*   REPORT_COUNT (1)                     */
    0x75, 0x08,    /*   REPORT_SIZE (8)                      */
    0x81, 0x03,    /*   INPUT (Cnst,Var,Abs)                 */
    0x95, 0x05,    /*   REPORT_COUNT (5)                     */
    0x75, 0x01,    /*   REPORT_SIZE (1)                      */
    0x05, 0x08,    /*   USAGE_PAGE (LEDs)                    */
    0x19, 0x01,    /*   USAGE_MINIMUM (Num Lock)             */
    0x29, 0x05,    /*   USAGE_MAXIMUM (Kana)                 */
    0x91, 0x02,    /*   OUTPUT (Data,Var,Abs)                */
    0x95, 0x01,    /*   REPORT_COUNT (1)                     */
    0x75, 0x03,    /*   REPORT_SIZE (3)                      */
    0x91, 0x03,    /*   OUTPUT (Cnst,Var,Abs)                */
    0x95, 0x06,    /*   REPORT_COUNT (6)                     */
    0x75, 0x08,    /*   REPORT_SIZE (8)                      */
    0x15, 0x00,    /*   LOGICAL_MINIMUM (0)                  */
    0x25, 0x65,    /*   LOGICAL_MAXIMUM (101)                */
    0x05, 0x07,    /*   USAGE_PAGE (Keyboard)                */
    0x19, 0x00,    /*   USAGE_MINIMUM (Reserved)             */
    0x29, 0x65,    /*   USAGE_MAXIMUM (Keyboard Application) */
    0x81, 0x00,    /*   INPUT (Data,Ary,Abs)                 */
    0xc0        /* END_COLLECTION                         */
};

/* hid descriptor for a keyboard */
static const struct hid_report_descriptor hid_report = {
    .subclass        = 0, /* No subclass */
    .protocol        = 1, /* Keyboard */
    .report_length        = 8,
    .report_desc_length    = sizeof(keyboard_report),
    .report_desc        = keyboard_report
};


static int uvc_control_brightness_process(const struct uvc_control_request *req)
{
    int len = -EOPNOTSUPP;
    short *data = req->buf;

    len = min_t(uint32_t, req->request_len, UVC_BRIGHTNESS_DATA_LEN);
    switch (req->request) {
        case UVC_SET_CUR:
            if (UVC_BRIGHTNESS_DATA_LEN <= req->buf_actual)
                brightness_data.cur_value = *data;
            break;
        case UVC_GET_CUR:
            *data = brightness_data.cur_value;
            break;
        case UVC_GET_MIN:
            *data = brightness_data.min_value;
            break;
        case UVC_GET_MAX:
            *data = brightness_data.max_value;
            break;
        case UVC_GET_DEF:
            *data = brightness_data.def_value;
            break;
        case UVC_GET_RES:
            *data = brightness_data.res_value;
            break;
        case UVC_GET_INFO:
            len = min_t(uint32_t, req->request_len, 1);
            ((u8 *)req->buf)[0] = 0x03;
            break;
        default:
            len = -EOPNOTSUPP;
            break;
        }

    return len;
}

static int uvc_control_contrast_process(const struct uvc_control_request *req)
{
    int len = -EOPNOTSUPP;
    short *data = req->buf;

    len = min_t(uint32_t, req->request_len, UVC_CONTRAST_DATA_LEN);
    switch (req->request) {
        case UVC_SET_CUR:
            if (UVC_CONTRAST_DATA_LEN <= req->buf_actual)
                contrast_data.cur_value = *data;
            break;
        case UVC_GET_CUR:
            *data = contrast_data.cur_value;
            break;
        case UVC_GET_MIN:
            *data = contrast_data.min_value;
            break;
        case UVC_GET_MAX:
            *data = contrast_data.max_value;
            break;
        case UVC_GET_DEF:
            *data = contrast_data.def_value;
            break;
        case UVC_GET_RES:
            *data = contrast_data.res_value;
            break;
        case UVC_GET_INFO:
            len = min_t(uint32_t, req->request_len, 1);
            ((u8 *)req->buf)[0] = 0x03;
            break;
        default:
            len = -EOPNOTSUPP;
            break;
        }


    return len;
}


static int uvc_control_auto_exposure_priority_data_process(const struct uvc_control_request *req)
{
    int len = -EOPNOTSUPP;
    char *data = req->buf;

    len = min_t(uint32_t, req->request_len, UVC_AUTO_EXPOSURE_PRIORITY_DATA_LEN);
    switch (req->request) {
        case UVC_SET_CUR:
            if (UVC_AUTO_EXPOSURE_PRIORITY_DATA_LEN <= req->buf_actual)
                uvc_auto_exposure_priority_status = *data;
            break;
        case UVC_GET_CUR:
            *data = uvc_auto_exposure_priority_status;
            break;
        case UVC_GET_DEF:
        case UVC_GET_MIN:
            *data = 0;
            break;
        case UVC_GET_RES:
        case UVC_GET_MAX:
            *data = 1;
            break;
        case UVC_GET_INFO:
            len = min_t(uint32_t, req->request_len, 1);
            ((u8 *)req->buf)[0] = 0x03;
            break;
        default:
            len = -EOPNOTSUPP;
            break;
        }

    return len;
}


static int uvc_param_callback(const struct uvc_control_request *req)
{
    int len;
    switch (req->entity_id) {
        case UVC_ENTITY_PROCESS_UNIT_ID:
            switch (req->control_selector) {
                case UVC_PU_BRIGHTNESS_CONTROL:
                    len = uvc_control_brightness_process(req);
                    break;
            case UVC_PU_CONTRAST_CONTROL:
                    len = uvc_control_contrast_process(req);
                    break;
            default:
                    len = -EOPNOTSUPP;
                    break;
            }
            break;
        case UVC_ENTITY_CAMERA_TERMINAL_ID:
            switch (req->control_selector) {
                case UVC_CT_AE_PRIORITY_CONTROL:
                    len = uvc_control_auto_exposure_priority_data_process(req);
                break;
            default:
                len = -EOPNOTSUPP;
                break;
            }
            break;
        default:
            len = -EOPNOTSUPP;
            break;
    }

    return len;
}

static void uvc_connect_callback(int connect)
{
    printf("uvc_connect_callback %d\n", connect);
}

static void uvc_format_callback(const struct uvc_video_format *format)
{
    char *data = (char *)&format->fcc;
    struct gadget_uvc_frame_source source;
    int ret;

    printf("uvc format: %c%c%c%c, width %d, height %d, fps %d\n", data[0], data[1], data[2], data[3], format->width,  format->height,  format->fps);

    ret = gadget_uvc_select_frame_source(format, &source);
    if (ret < 0) {
        printf("%s: unsupported format %c%c%c%c %ux%u\n", __func__, data[0], data[1], data[2], data[3], format->width, format->height);
        assert(0);
    }

    gadget_uvc_set_frame_source(&source);
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
    struct gadget_uvc_tx_frame *tx_frame = NULL;

    while (uvc_stream_flag) {
        tx_frame = gadget_uvc_get_free_tx_frame();
        if (tx_frame == NULL)
            break;

        ret = gadget_uvc_prepare_tx_frame(tx_frame);
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

        /*
        * Note:
        * After calling the gadget_uvc_write function, the memory pointed to by 'uvc_buf'
        * must remain unmodified until 'uvc_buf_complete' is called.
        * Premature modification will result in corrupted frame data and USB transfer errors.
        */
        ret = gadget_uvc_write(&tx_frame->uvc_buf, 0, 0);
        if (ret < 0) {
            gadget_uvc_put_tx_frame(tx_frame);
            if (ret != -EAGAIN && ret != -ENOTCONN && ret != -ENOLINK)
                printf("%s: write tx frame %u failed %d\n", __func__, tx_frame->tx_frame_id, ret);
            break;
        }
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

static void usb_gadget_hid_thread(void *data)
{
    int len, i;
    int count = 0;
    u8 led_state = 0;
    unsigned char hid_data[8];

    while (1) {
        msleep(1000);
        memset(hid_data, 0, sizeof(hid_data));
        hid_data[2] = count + 4;
        gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
        msleep(1000);
        memset(hid_data, 0, sizeof(hid_data));
        gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
        msleep(1000);
        len = gadget_hid_read(hid_data, sizeof(hid_data), 0, 0);
        if (len > 0) {
            for(i = 0; i < len; i++) {
                if (hid_data[i] & BIT(1))
                    led_state = 1;
                else
                    led_state = 0;

                printf("usb hid read data %d, led %d\n", hid_data[i], led_state);
            }
        }

        count++;
        if (count > 25)
            count = 0;
    }
}

static void hid_connect_callback(int connect)
{
    printf("hid_connect_callback %d\n", connect);
}

struct uvc_callback uvc_callback = {
    .format_cb = uvc_format_callback,
    .stream_cb = uvc_stream_callback,
    .connect_cb = uvc_connect_callback,
    .param_cb = uvc_param_callback,
};

struct uvc_hid_callback callback = {
    .uvc_cb = &uvc_callback,
    .hid_connect_cb = hid_connect_callback,
    .hid_request_cb = NULL,
};

int gadget_usb_uvc_hid_test(void)
{
    int ret;

    ret = gadget_uvc_tx_frames_init();
    if (ret < 0)
        return ret;

    gadget_uvc_hid_init(&usb_id, &uvc_config, &hid_report, &callback);
    uvc_thread = thread_create("usb gadget uvc thread", 8192, usb_gadget_uvc_thread, NULL);
    thread_create("usb gadget hid thread", 8192, usb_gadget_hid_thread, NULL);

    return 0;
}
