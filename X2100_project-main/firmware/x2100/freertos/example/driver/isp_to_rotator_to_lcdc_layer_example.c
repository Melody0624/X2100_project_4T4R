#include <os.h>
#include <stdio.h>
#include <common.h>
#include <driver/fb.h>
#include <driver/backlight.h>
#include <driver/camera_isp.h>
#include <driver/rotator.h>

#define ALIGN_DOWN(x, n) ((((x) + (n) - 1) - ((x) + (n) - 1) % (n)) - 16)

static struct fb_info fb_info;
static int frame_index;
static struct fb_handle *fb0_handle;

static struct frame_image_format output_fmt = {
    .pixel_format       = CAMERA_PIX_FMT_NV12,

    .scaler.enable      = 0,
    .crop.enable        = 0,

    .frame_nums         = 2,
};

/*  初始化fb
*/
static void test_lcd_display_init_thread(void *data)
{
    fb0_handle = fb_open("fb0");
    if(fb0_handle == NULL)
        printf("fb0 = NULL\n");

    fb_enable(fb0_handle);
    fb_get_info(fb0_handle, &fb_info);
    assert(fb_info.frame_count >= 2);

    fb_enable(fb0_handle);
}

static void recalc_output_fmt_by_fb(camera_hd_t *camera_hd)
{
    struct fb_info fb;
    struct camera_info *sensor_info;
    int scaler_enable = 1;
    int crop_enable = 1;
    int width = 0;
    int height = 0;
    int align_size;
    int fb_width;
    int fb_height;
    int scaler_width;
    int scaler_height;
    float sensor_ratio;
    float fb_ratio;

    fb_get_info(fb0_handle, &fb);
    assert(fb.frame_count >= 2);

    isp_get_line_align_size(camera_hd, &align_size);
    isp_get_max_scaler_size(camera_hd, &scaler_width, &scaler_height);
    sensor_info = isp_get_sensor_info(camera_hd);
    assert(sensor_info != NULL);

    /* scaler所支持的宽高与fb的宽高比较，取二者相交的区域作为cam可显示的最大区域P  */
    fb_width = scaler_width;
    if (fb_width > fb.xres)
        fb_width = fb.xres;

    fb_height = scaler_height;
    if (fb_height > fb.yres)
        fb_height = fb.yres;

    /* 计算sensor的宽高比(宽：高)、fb的宽高比(宽：高) */
    sensor_ratio = (float)sensor_info->width / sensor_info->height;
    fb_ratio = (float)fb_width / fb_height;

    /* 当sensor的宽高比(宽：高)等于fb的宽高比(宽：高)，则sensor出图尺寸不变 */
    if (sensor_ratio == fb_ratio) {
        width = fb_width;
        height = fb_height;
    }

    /*
     * 当sensor的宽高比(宽：高)大于fb的宽高比(宽：高)，
     * 先缩放sensor出图的高与fb的高相等，再根据高换算出对应比例的宽作为sensor出图的宽。
     */
    if (sensor_ratio > fb_ratio) {
        height = fb_height;
        width = height * sensor_info->width / sensor_info->height;

        /* 当宽超过scaler最大宽度时，需要重新计算高 */
        if (width > scaler_width) {
            width = scaler_width;
            height = 0;
        }
    }

    /*
     * 当sensor的宽高比(宽：高)小于fb的宽高比(宽：高)，
     * 先缩放sensor出图的宽与fb的宽相等，再根据宽换算出对应比例的高作为sensor出图的高。
     */
    if (sensor_ratio < fb_ratio) {
        width = fb_width;
        height = width * sensor_info->height / sensor_info->width;

        /* 当高超过scaler最大高度时，需要重新计算宽 */
        if (height > scaler_height) {
            height = scaler_height;
            width = height * sensor_info->width / sensor_info->height;
            height = 0;
        }
    }

    /* sensor的宽须与align_size对齐，对齐后再重新计算出合适的出图尺寸 */
    if (width % align_size) {
        width = ALIGN_DOWN(width, align_size);
        height = 0;
    }

    /* 当出现sensor宽没有16位对齐或者超出scaler范围的时候需要重新计算高 */
    if (height == 0)
        height = width * sensor_info->height / sensor_info->width;

    if (width == sensor_info->width) {
        scaler_enable = 0;
        crop_enable = 0;
    }

    height = ALIGN_DOWN(height, 2);

    output_fmt.pixel_format = CAMERA_PIX_FMT_NV12;
    output_fmt.frame_nums = 2;
    output_fmt.scaler.enable = scaler_enable;
    output_fmt.scaler.width = width;
    output_fmt.scaler.height = height;
    output_fmt.crop.enable = crop_enable;
    output_fmt.crop.top = height > fb.yres ? (height - fb.yres) / 2 : 0;
    output_fmt.crop.top = output_fmt.crop.top % 2 ? output_fmt.crop.top + 1 : output_fmt.crop.top;
    output_fmt.crop.left = (width - fb.xres) / 2;
    output_fmt.crop.left = output_fmt.crop.left % 2 ? output_fmt.crop.left + 1 : output_fmt.crop.left;
    output_fmt.crop.width = fb.xres;
    output_fmt.crop.height = fb.yres > height ? height : fb.yres;
    output_fmt.width = fb.xres;
    output_fmt.height = fb.yres > height ? height : fb.yres;

}

/*  index: 表示初始化sensor的index; 初始化sensor0,index=0; 初始化sensor1, index=1;
 *  channel: isp输出通道, 一般选择0 ~ 2;
*/
void isp_to_rotator_to_lcdc_layer_test(int index, int channel)
{
    camera_hd_t *camera_hd;
    struct camera_info *info;
    struct fb_info fb_info;
    char *device_name;
    int ret;

    //thread_create("lcd_display_init", 1024, test_lcd_display_init_thread, NULL);
    test_lcd_display_init_thread(NULL);

    camera_hd = isp_detect(index, channel);
    if (!camera_hd) {
        printf("mscaler%d-ch%d not found camera\n",index , channel);
        goto isp_detect_error;
    }
    device_name = (char *)camera_hd->ptr;

    recalc_output_fmt_by_fb(camera_hd);

    ret = isp_set_format(camera_hd, &output_fmt);
    if (ret < 0) {
        printf("%s set format failed\n", device_name);
        goto isp_set_fmt_error;
    }

    ret = isp_request_buffer(camera_hd, &output_fmt);
    if (ret < 0) {
        printf("%s requset buffer failed\n", device_name);
        goto isp_request_buf_error;
    }

    info = isp_get_info(camera_hd);

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

    ret = isp_power_on(camera_hd);
    if (ret) {
        printf("%s power on failed\n", device_name);
        goto isp_power_on_error;
    }

    ret = isp_stream_on(camera_hd);
    if (ret) {
        printf("%s stream on failed\n", device_name);
        goto isp_stream_on_error;
    }

    struct frame_image_format fmt;
    isp_get_format(camera_hd, &fmt);

    while(!fb_is_enable(fb0_handle))
        usleep(1);
    fb_get_info(fb0_handle, &fb_info);

#ifdef CONFIG_BACKLIGHT
    struct backlight *m_backlight = backlight_open("backlight_gpio0");
    if (m_backlight) {
        int level = backlight_get_maxbrightness(m_backlight);
        printf("backlight level: %d\n", level);
        backlight_set_brightness(m_backlight, level);
    }
#endif

    /* 申请旋转使用的buf */
    void *rotator_vaddr = malloc(info->frame_size);
    assert(rotator_vaddr);
    unsigned long rotator_paddr = virt_to_phys(rotator_vaddr);
    /* 旋转角度 */
    enum rotator_angle rotator_angle = ROTATOR_ANGLE_90;
    int rotator_w, rotator_h;

    struct frame_info frame;
    while (1) {
        ret = isp_dqbuf_wait(camera_hd, &frame);
        if (ret) {
            fprintf(stderr, "isp get frame failed\n");
            continue;
        }

        struct rotator_config_data rotator_config = {
            .src_buf = frame.vaddr,
            .dst_buf = rotator_vaddr,

            .frame_width = info->width,
            .frame_height = info->height,

            .src_stride = info->width,
            .dst_stride = info->width,

            .src_fmt = ROTATOR_NV12,
            .dst_fmt = ROTATOR_NV12,

            .horizontal_mirror = ROTATOR_NO_MIRROR,
            .vertical_mirror = ROTATOR_NO_MIRROR,
            .rotate_angle = ROTATOR_ANGLE_0,
        };
        rotator_config.rotate_angle = rotator_angle;
        if (rotator_config.rotate_angle == ROTATOR_ANGLE_90 ||
            rotator_config.rotate_angle == ROTATOR_ANGLE_270) {
            rotator_config.dst_stride = info->height;
            rotator_w = info->height;
            rotator_h = info->width;
        } else {
            rotator_config.dst_stride = info->width;
            rotator_w = info->width;
            rotator_h = info->height;
        }
        rotator_conversion(&rotator_config);

        /* display */
        struct lcdc_layer layer_cfg = {
            .fb_fmt = fb_fmt_NV12,
            .xres = fb_info.xres < rotator_w ? fb_info.xres : rotator_w,
            .yres = fb_info.yres < rotator_h ? fb_info.yres : rotator_h,
            .xpos = 0,
            .ypos = 0,

            .layer_order = lcdc_layer_bottom,
            .layer_enable = 1,

            .y = {
                .mem = (void *)rotator_paddr,
                .stride = rotator_w,
            },

            .uv = {
                .mem = (void *)rotator_paddr + rotator_w * rotator_h,
                .stride = rotator_w,
            },

            .alpha = {
                .enable = 0,
                .value = 0xff,
            },
        };

        frame_index++;
        if (!fb_set_config(fb0_handle, &layer_cfg)) {
            fb_enable_config(fb0_handle);
            fb_pan_display(fb0_handle, 0);
        }

        isp_qbuf(camera_hd, &frame);
    } /* end of while(1) */

    isp_stream_off(camera_hd);

isp_stream_on_error:
    isp_power_off(camera_hd);
isp_power_on_error:
    isp_free_buffer(camera_hd);
isp_request_buf_error:
isp_set_fmt_error:
    isp_release(camera_hd);
isp_detect_error:
    while(!fb_is_enable(fb0_handle));  /* 等待lcd初始化完成 */
    fb_disable(fb0_handle);
    return ;
}
