
#include <common.h>
#include <os.h>
#include <errno.h>
#include <usb/gadget_uvc.h>
#include <driver/camera_isp.h>
#include <driver/isp_tuning.h>

#define WEBCAM_VENDOR_ID        0x1d6b    /* Linux Foundation */
#define WEBCAM_PRODUCT_ID        0x0102    /* Webcam A/V gadget */

#define UVC_POWER_LINE_DISABLE  0
#define UVC_POWER_LINE_50HZ     1
#define UVC_POWER_LINE_60HZ     2

#define UVC_MANUAL_EXPOSURE_MODE    BIT(0)
#define UVC_AUTO_EXPOSURE_MODE      BIT(1)

#define UVC_EXPOSURE_TIME_DEF   0
#define UVC_EXPOSURE_TIME_ADD   1
#define UVC_EXPOSURE_TIME_SUB   0xFF

#define UVC_GENERAL_DATA_LEN    2

struct uvc_tiny_data {
    unsigned char res_value;
    unsigned char def_value;
    unsigned char cur_value;
};

struct uvc_general_data {
    const char *name;
    short res_value;
    short max_value;
    short min_value;
    short def_value;
    short cur_value;
    int (*get_general_data)(short *value);
    int (*set_general_data)(short value);
};

union white_balance_data {
    struct {
        unsigned short blue;
        unsigned short red;
    } other;
    unsigned int all;
};

struct uvc_white_balance_data {
    union white_balance_data res_value;
    union white_balance_data max_value;
    union white_balance_data min_value;
    union white_balance_data def_value;
    union white_balance_data cur_value;
};

static unsigned char uvc_stream_flag;
static struct uvc_buffer uvc_buf;
static unsigned char uvc_buf_use;
static thread_ptr_t uvc_thread;

static isp_tuning_hd_t *isp_handle;
static camera_hd_t *camera_handle;
static int isp_index;
static int isp_channel;

#define DECLARE_UVC_GENERAL_DATA(attr_name, _max, _min, _def)   \
    static int get_##attr_name##_data(short *value) {           \
        int ret;                                                \
        unsigned char data = 0;                                 \
        if (!isp_handle)                                        \
            return -ENODEV;                                     \
        ret = isp_tuning_get_##attr_name(isp_handle, &data);    \
        *value = data;                                          \
        return ret;                                             \
    }                                                           \
    static int set_##attr_name##_data(short value) {            \
        unsigned char data = value;                             \
        if (!isp_handle)                                        \
            return -ENODEV;                                     \
        return isp_tuning_set_##attr_name(isp_handle, data);    \
    }                                                           \
    static struct uvc_general_data attr_name##_data = {         \
        .name = #attr_name,                                     \
        .res_value = 1,                                         \
        .max_value = _max,                                      \
        .min_value = _min,                                      \
        .def_value = _def,                                      \
        .cur_value = _def,                                      \
        .get_general_data = get_##attr_name##_data,             \
        .set_general_data = set_##attr_name##_data,             \
    }

DECLARE_UVC_GENERAL_DATA(brightness, 255, 0, 128);
DECLARE_UVC_GENERAL_DATA(contrast, 255, 0, 128);
DECLARE_UVC_GENERAL_DATA(saturation, 255, 0, 128);
DECLARE_UVC_GENERAL_DATA(sharpness, 255, 0, 128);

static struct uvc_tiny_data power_line_data = {
    .def_value = 0,
    .cur_value = 0,
};

static struct uvc_white_balance_data white_balance_data = {
    .res_value = {
        .other = {
            .blue = 1,
            .red = 1,
        }
    },
    .max_value = {
        .other = {
            .blue = 4095,
            .red = 4095,
        }
    },
    .min_value = {
        .other = {
            .blue = 0,
            .red = 0,
        }
    },
    .def_value = {
        .other = {
            .blue = 0,
            .red = 0,
        }
    },
    .cur_value = {
        .other = {
            .blue = 0,
            .red = 0,
        }
    },
};

static struct uvc_tiny_data auto_white_balance_data = {
    .def_value = 1,
    .cur_value = 1,
};

static struct uvc_tiny_data auto_exposure_data = {
    .res_value = UVC_MANUAL_EXPOSURE_MODE | UVC_AUTO_EXPOSURE_MODE,
    .def_value = UVC_AUTO_EXPOSURE_MODE,
    .cur_value = UVC_AUTO_EXPOSURE_MODE,
};

static struct gadget_id usb_id = {
    .vendor_id = WEBCAM_VENDOR_ID,
    .product_id = WEBCAM_PRODUCT_ID,
};

/* fps large to small Sort*/
static const unsigned int frame_fps[] = {
    60, 45, 30, 25,
};

/* Frame size small to large sort*/
static struct uvc_frame_config uvc_nv12_frames[] = {
    {
        // .width = 640,
        // .height = 360,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
};

/* Format small to large sort according to pixel size */
static const struct uvc_format_config uvc_frame_format[]= {
    {
        .fcc = V4L2_PIX_FMT_NV12,
        .bpp = 12,
        .frames_num = ARRAY_SIZE(uvc_nv12_frames),
        .frames = uvc_nv12_frames,
    },
};


static const struct uvc_device_config uvc_config = {
    .format_num = ARRAY_SIZE(uvc_frame_format),
    .formats = uvc_frame_format,
    .camera_param_config = UVC_PARAM_BRIGHTNESS | UVC_PARAM_CONTRAST | UVC_PARAM_SATURATION | UVC_PARAM_SHARPNESS | \
    UVC_PARAM_POWER_LINE_FREQUEBCY | UVC_PARAM_WHITE_BALANCE_COMPONENT | UVC_PARAM_WHITE_BALANCE_COMPONENT_AUTO,
    .camera_feature_config = UVC_FEATURE_AUTO_EXPOSURE_MODE | UVC_FEATURE_EXPOSURE_TIME_RELATIVE,
};

static int uvc_control_general_process(const struct uvc_control_request *req, struct uvc_general_data *general_data)
{
    int ret;
    int len = -EOPNOTSUPP;
    short *data = req->buf;

    len = min_t(uint32_t, req->request_len, UVC_GENERAL_DATA_LEN);
    switch (req->request) {
        case UVC_SET_CUR:
            if (UVC_GENERAL_DATA_LEN <= req->buf_actual) {
                general_data->cur_value = *data;
                ret = general_data->set_general_data(general_data->cur_value);
                if (ret)
                    printf("%s: %s set current error\n", __func__, general_data->name);
            }
            break;
        case UVC_GET_CUR:
            ret = general_data->get_general_data(&general_data->cur_value);
            if (ret)
                printf("%s: %s get current error\n", __func__, general_data->name);
            *data = general_data->cur_value;
            break;
        case UVC_GET_MIN:
            *data = general_data->min_value;
            break;
        case UVC_GET_MAX:
            *data = general_data->max_value;
            break;
        case UVC_GET_DEF:
            *data = general_data->def_value;
            break;
        case UVC_GET_RES:
            *data = general_data->res_value;
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

static int get_power_line_data(unsigned char *value)
{
    int ret;
    isp_anti_flicker_attr flicker_attr;

    if (!isp_handle)
        return -ENODEV;

    ret = isp_tuning_get_anti_flicker_attr(isp_handle, &flicker_attr);
    if (ret)
        return ret;

    switch (flicker_attr) {
        case ISP_ANTIFLICKER_DISABLE:
            *value = UVC_POWER_LINE_DISABLE;
            break;
        case ISP_ANTIFLICKER_50HZ:
            *value = UVC_POWER_LINE_50HZ;
            break;
        case ISP_ANTIFLICKER_60HZ:
            *value = UVC_POWER_LINE_60HZ;
            break;
    }

    return 0;
}

static int set_power_line_data(unsigned char value)
{
    isp_anti_flicker_attr flicker_attr;

    switch (value) {
        case UVC_POWER_LINE_DISABLE:
            flicker_attr = ISP_ANTIFLICKER_DISABLE;
            break;
        case UVC_POWER_LINE_50HZ:
            flicker_attr = ISP_ANTIFLICKER_50HZ;
            break;
        case UVC_POWER_LINE_60HZ:
            flicker_attr = ISP_ANTIFLICKER_60HZ;
            break;
        default:
            flicker_attr = ISP_ANTIFLICKER_50HZ;
            break;
    }

    if (!isp_handle)
        return -ENODEV;

    return isp_tuning_set_anti_flicker_attr(isp_handle, flicker_attr);
}

static int uvc_control_power_line_process(const struct uvc_control_request *req)
{
    int ret;
    int len = -EOPNOTSUPP;
    unsigned char *data = req->buf;

    len = min_t(uint32_t, req->request_len, UVC_POWER_LINE_FREQUEBCY_DATA_LEN);
    switch (req->request) {
        case UVC_SET_CUR:
            if (UVC_POWER_LINE_FREQUEBCY_DATA_LEN <= req->buf_actual) {
                power_line_data.cur_value = *data;
                ret = set_power_line_data(power_line_data.cur_value);
                if (ret)
                    printf("%s: set current error\n", __func__);
            }
            break;
        case UVC_GET_CUR:
            ret = get_power_line_data(&power_line_data.cur_value);
            if (ret)
                printf("%s: get current error\n", __func__);
            *data = power_line_data.cur_value;
            break;
        case UVC_GET_DEF:
            *data = power_line_data.def_value;
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

static int get_control_white_balance_data(unsigned char *auto_mode, unsigned short *rgain, unsigned short *bgain)
{
    int ret;
    struct isp_core_wb_attr wb_attr;

    if (!isp_handle)
        return -ENODEV;

    ret = isp_tuning_get_wb(isp_handle, &wb_attr);
    if (ret)
        return ret;

    if (wb_attr.mode == ISP_CORE_WB_MODE_AUTO)
        *auto_mode = 1;
    else
        *auto_mode = 0;

    *rgain = wb_attr.rgain;
    *bgain = wb_attr.bgain;

    return 0;
}

static int set_control_white_balance_data(unsigned char auto_mode, unsigned short rgain, unsigned short bgain)
{
    struct isp_core_wb_attr wb_attr;

    if (auto_mode)
        wb_attr.mode = ISP_CORE_WB_MODE_AUTO;
    else
        wb_attr.mode = ISP_CORE_WB_MODE_MANUAL;

    wb_attr.rgain = rgain;
    wb_attr.bgain = bgain;

    if (!isp_handle)
        return -ENODEV;

    return isp_tuning_set_wb(isp_handle, &wb_attr);
}

static int uvc_control_white_balance_process(const struct uvc_control_request *req)
{
    int ret;
    int len = -EOPNOTSUPP;
    unsigned int *data = req->buf;

    len = min_t(uint32_t, req->request_len, UVC_WHITE_BALANCE_COMPONENT_DATA_LEN);
    switch (req->request) {
        case UVC_SET_CUR:
            if (UVC_WHITE_BALANCE_COMPONENT_DATA_LEN <= req->buf_actual) {
                white_balance_data.cur_value.all = *data;
                ret = set_control_white_balance_data(auto_white_balance_data.cur_value, white_balance_data.cur_value.other.red, white_balance_data.cur_value.other.blue);
                if (ret)
                    printf("%s: set current error\n", __func__);
            }
            break;
        case UVC_GET_CUR:
            ret = get_control_white_balance_data(&auto_white_balance_data.cur_value, &white_balance_data.cur_value.other.red, &white_balance_data.cur_value.other.blue);
            if (ret)
                printf("%s: get current error\n", __func__);
            *data = white_balance_data.cur_value.all;
            break;
        case UVC_GET_MIN:
            *data = white_balance_data.min_value.all;
            break;
        case UVC_GET_MAX:
            *data = white_balance_data.max_value.all;
            break;
        case UVC_GET_DEF:
            *data = white_balance_data.def_value.all;
            break;
        case UVC_GET_RES:
            *data = white_balance_data.res_value.all;
            break;
        case UVC_GET_INFO:
            len = min_t(uint32_t, req->request_len, 1);
            if (auto_white_balance_data.cur_value)
                ((u8 *)req->buf)[0] = 0x07;
            else
                ((u8 *)req->buf)[0] = 0x03;
            break;
        default:
            len = -EOPNOTSUPP;
            break;
        }

    return len;
}

static int uvc_control_white_balance_auto_process(const struct uvc_control_request *req)
{
    int ret;
    int len = -EOPNOTSUPP;
    unsigned char *data = req->buf;

    len = min_t(uint32_t, req->request_len, UVC_WHITE_BALANCE_COMPONENT_AUTO_DATA_LEN);
    switch (req->request) {
        case UVC_SET_CUR:
            if (UVC_WHITE_BALANCE_COMPONENT_AUTO_DATA_LEN <= req->buf_actual) {
                auto_white_balance_data.cur_value = *data;
                ret = set_control_white_balance_data(auto_white_balance_data.cur_value, white_balance_data.cur_value.other.red, white_balance_data.cur_value.other.blue);
                if (ret)
                    printf("%s: set current error\n", __func__);
            }
            break;
        case UVC_GET_CUR:
            ret = get_control_white_balance_data(&auto_white_balance_data.cur_value, &white_balance_data.cur_value.other.red, &white_balance_data.cur_value.other.blue);
            if (ret)
                printf("%s: get current error\n", __func__);
            *data = auto_white_balance_data.cur_value;
            break;
        case UVC_GET_DEF:
            *data = auto_white_balance_data.def_value;
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

static int get_exposure_data(unsigned char *auto_mode, unsigned short *exposure_time,
        unsigned short *exposure_max, unsigned short *exposure_min)
{
    int ret;
    isp_expr expr;


    if (!isp_handle)
        return -ENODEV;

    ret = isp_tuning_get_expr(isp_handle, &expr);
    if (ret)
        return ret;

    if (expr.g_attr.mode == ISP_CORE_EXPR_MODE_AUTO)
        *auto_mode = UVC_AUTO_EXPOSURE_MODE;
    else
        *auto_mode = UVC_MANUAL_EXPOSURE_MODE;

    *exposure_time = expr.g_attr.integration_time;

    if (exposure_max)
        *exposure_max = expr.g_attr.integration_time_max;

    if (exposure_min)
        *exposure_min = expr.g_attr.integration_time_min;

    return ret;
}

static int set_exposure_data(unsigned char auto_mode, unsigned short exposure_time)
{
    isp_expr expr;

    if (auto_mode == UVC_MANUAL_EXPOSURE_MODE)
        expr.s_attr.mode = ISP_CORE_EXPR_MODE_MANUAL;
    else
        expr.s_attr.mode = ISP_CORE_EXPR_MODE_AUTO;

    expr.s_attr.unit = ISP_CORE_EXPR_UNIT_LINE;

    expr.s_attr.time = exposure_time;

    if (!isp_handle)
        return -ENODEV;

    return isp_tuning_set_expr(isp_handle, &expr);
}


static int uvc_control_auto_exposure_mode_process(const struct uvc_control_request *req)
{
    int ret;
    int len = -EOPNOTSUPP;
    unsigned char *data = req->buf;
    unsigned short exposure_time = 0;
    ret = get_exposure_data(&auto_exposure_data.cur_value, &exposure_time, NULL, NULL);
    if (ret)
        printf("%s: get exposure current error\n", __func__);

    len = min_t(uint32_t, req->request_len, UVC_AUTO_EXPOSURE_MODE_DATA_LEN);
    switch (req->request) {
        case UVC_SET_CUR:
            if (UVC_AUTO_EXPOSURE_MODE_DATA_LEN <= req->buf_actual)
                auto_exposure_data.cur_value = *data;
                ret = set_exposure_data(auto_exposure_data.cur_value, exposure_time);
                if (ret)
                    printf("%s: set current error\n", __func__);
            break;
        case UVC_GET_CUR:
            *data = auto_exposure_data.cur_value;
            break;
        case UVC_GET_RES:
            *data = auto_exposure_data.res_value;
            break;
        case UVC_GET_DEF:
            *data = auto_exposure_data.def_value;
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

static int uvc_control_exposure_time_rel_process(const struct uvc_control_request *req)
{
    int ret;
    int len = -EOPNOTSUPP;
    unsigned int *data = req->buf;
    unsigned short exposure_time = 0;
    unsigned short exposure_max = 0;
    unsigned short exposure_min = 0;
    ret = get_exposure_data(&auto_exposure_data.cur_value, &exposure_time, &exposure_max, &exposure_min);
    if (ret)
        printf("%s: get exposure time error\n", __func__);

    len = min_t(uint32_t, req->request_len, UVC_EXPOSURE_TIME_RELATIVE_DATA_LEN);
    switch (req->request) {
        case UVC_SET_CUR:
            if (UVC_EXPOSURE_TIME_RELATIVE_DATA_LEN <= req->buf_actual) {
                switch (*data)
                {
                case UVC_EXPOSURE_TIME_DEF:
                    auto_exposure_data.cur_value = UVC_AUTO_EXPOSURE_MODE;
                    break;
                case UVC_EXPOSURE_TIME_ADD:
                    if (exposure_time < exposure_max)
                        exposure_time++;
                    break;
                case UVC_EXPOSURE_TIME_SUB:
                    if (exposure_time > exposure_min)
                        exposure_time--;
                    break;
                default:
                    break;
                }

                ret = set_exposure_data(auto_exposure_data.cur_value, exposure_time);
                if (ret)
                    printf("%s: set current error\n", __func__);
            }
            break;
        case UVC_GET_CUR:
            *data = UVC_EXPOSURE_TIME_DEF;
            break;
        case UVC_GET_INFO:
            len = min_t(uint32_t, req->request_len, 1);
            if (auto_exposure_data.cur_value == UVC_AUTO_EXPOSURE_MODE)
                ((u8 *)req->buf)[0] = 0x07;
            else
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
                    len = uvc_control_general_process(req, &brightness_data);
                    break;
                case UVC_PU_CONTRAST_CONTROL:
                    len = uvc_control_general_process(req, &contrast_data);
                    break;
                case UVC_PU_SATURATION_CONTROL:
                    len = uvc_control_general_process(req, &saturation_data);
                    break;
                case UVC_PU_SHARPNESS_CONTROL:
                    len = uvc_control_general_process(req, &sharpness_data);
                    break;
                case UVC_PU_POWER_LINE_FREQUENCY_CONTROL:
                    len = uvc_control_power_line_process(req);
                    break;
                case UVC_PU_WHITE_BALANCE_COMPONENT_CONTROL:
                    len = uvc_control_white_balance_process(req);
                    break;
                case UVC_PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL:
                    len = uvc_control_white_balance_auto_process(req);
                    break;
                default:
                    len = -EOPNOTSUPP;
                    break;
            }
            break;
        case UVC_ENTITY_CAMERA_TERMINAL_ID:
            switch (req->control_selector) {
                case UVC_CT_AE_MODE_CONTROL:
                    len = uvc_control_auto_exposure_mode_process(req);
                    break;
                case UVC_CT_EXPOSURE_TIME_RELATIVE_CONTROL:
                    len = uvc_control_exposure_time_rel_process(req);
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

    printf("uvc format: %c%c%c%c, width %d, height %d, fps %d\n", data[0], data[1], data[2], data[3], format->width,  format->height,  format->fps);
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
    uvc_buf_use = 0;
    if (buf->state != UVC_BUF_STATE_DONE)
        printf("%s: data not transmitted\n", __func__);

    isp_put_frame(camera_handle, (void *)buf->mem);
    thread_wakeup(uvc_thread);
}

static void usb_gadget_uvc_thread(void *data)
{
    int ret;
    struct frame_image_format fmt;
    ret = isp_get_format(camera_handle, &fmt);
    assert(!ret);

    uvc_buf_use = 0;
    uvc_buf.complete = uvc_buf_complete;
    uvc_buf.length = fmt.frame_size;

    while (1) {
        while (!uvc_stream_flag || uvc_buf_use)
            thread_wait();

        if (!isp_handle) {
            ret = isp_power_on(camera_handle);
            assert(!ret);

            ret = isp_stream_on(camera_handle);
            assert(!ret);

            isp_handle = isp_tuning_detect(isp_index);
            assert(isp_handle);
        }

        if (uvc_stream_flag) {
            uvc_buf.mem = isp_wait_frame(camera_handle);
            if (uvc_buf.mem) {
                uvc_buf_use = 1;
                ret = gadget_uvc_write(&uvc_buf, 1, -1);
                if (ret) {
                    isp_put_frame(camera_handle, (void *)uvc_buf.mem);
                    uvc_buf_use = 0;
                }
            }
        }

        if (!uvc_stream_flag) {
            os_enter_critical();
            isp_handle = NULL;
            os_exit_critical();

            isp_stream_off(camera_handle);
            isp_power_off(camera_handle);
        }
    }
}

struct uvc_callback callback = {
    .format_cb = uvc_format_callback,
    .stream_cb = uvc_stream_callback,
    .connect_cb = uvc_connect_callback,
    .param_cb = uvc_param_callback,
};

static struct frame_image_format output_fmt = {
    .width              = 1920,
    .height             = 1080,
    .pixel_format       = CAMERA_PIX_FMT_NV12,

    .scaler.enable      = 0,
    .scaler.width       = 1920,
    .scaler.height      = 1200,

    .crop.enable        = 0,
    .crop.top           = 0,
    .crop.left          = 0,
    .crop.width         = 1920,
    .crop.height        = 1080,

    .frame_nums         = 2,
};

int gadget_usb_uvc_with_isp(int index, int channel)
{
    struct camera_info *sensor_info;
    struct camera_info *info;
    char *device_name;
    int ret;

    isp_index = index;
    isp_channel = channel;

    camera_handle = isp_detect(index, channel);
    if (!camera_handle) {
        printf("mscaler%d-ch%d not found camera\n",index , channel);
        goto isp_detect_error;
    }
    device_name = (char *)camera_handle->ptr;

    if (!output_fmt.scaler.enable && !output_fmt.crop.enable) {
        /* 输出设置为Sensor分辨率 */
        sensor_info = isp_get_sensor_info(camera_handle);
        if (sensor_info) {
            output_fmt.width = sensor_info->width;
            output_fmt.height = sensor_info->height;
        }
    }

    uvc_nv12_frames[0].width = output_fmt.width;
    uvc_nv12_frames[0].height = output_fmt.height;

    ret = isp_set_format(camera_handle, &output_fmt);
    if (ret < 0) {
        printf("%s set format failed\n", device_name);
        goto isp_set_fmt_error;
    }

    ret = isp_request_buffer(camera_handle, &output_fmt);
    if (ret < 0) {
        printf("%s requset buffer failed\n", device_name);
        goto isp_request_buf_error;
    }

    info = isp_get_info(camera_handle);

    char fmt_a = (char)(info->data_fmt >> 0);
    char fmt_b = (char)(info->data_fmt >> 8);
    char fmt_c = (char)(info->data_fmt >> 16);
    char fmt_d = (char)(info->data_fmt >> 24);
    printf("channel         = %s\n", device_name);
    printf("sensor_name     = %s\n", info->name);
    printf("width           = %d\n", info->width);
    printf("height          = %d\n", info->height);
    printf("fps             = %d\n", info->fps);
    printf("data_fmt        = %c%c%c%c\n", fmt_a, fmt_b, fmt_c, fmt_d);
    printf("line_length     = %d\n", info->line_length);
    printf("frame_size      = %d\n", info->frame_size);
    printf("frame_align_size= %d\n", info->frame_align_size);

    gadget_uvc_init(&usb_id, &uvc_config, &callback);
    uvc_thread = thread_create("usb gadget uvc thread", 8192, usb_gadget_uvc_thread, NULL);
    return 0;


isp_request_buf_error:
isp_set_fmt_error:
    isp_release(camera_handle);
isp_detect_error:
    return -1;
}
