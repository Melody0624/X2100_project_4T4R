#include <stdio.h>
#include <driver/clk.h>
#include <driver/gpio.h>
#include <driver/pcm.h>
#include <driver/backlight.h>

#include <devices/gt9xx_touch.h>

#include "third_party/lvgl/lvgl_ingenic.h"
#include "third_party/lvgl/lvgl/lvgl.h"

#include <common.h>
#include <usb/gadget_uac1.h>
#include <errno.h>

#include <os.h>

static lv_obj_t *btn_switch;
static int agc_on;
static lv_obj_t *lb_switch;
static lv_obj_t *dd_gain_max, *dd_gain_min;
static lv_obj_t *r_max, *r_min;

static struct pcm_device *capture_dev;

static u16 c_adcl_gain = 196;
static thread_waiter_t c_volume_waiter;

#define ICODEC_BASE 0x10021000
#define ICODEC_ADDR(reg) (io_addr(ICODEC_BASE + reg))

#define ALCOGR      0x138
#define ALCOGAIN    0, 4

#define LB_VALUE_MAX    64
static lv_obj_t *lb_values[LB_VALUE_MAX];
static int lb_value_count;
static int lb_value_id;
static int ser_color;
static int old_gain;

#define BUF_SIZE_MS     100
#define PKT_SIZE_MS     10

static const struct gadget_id uac1_id = {
    .vendor_id = 0x1d6b,
    .product_id = 0x0101
};

static void uac1_connect_callback(int connect)
{
    printf("%s %d\n", __func__, connect);
}

static void c_volume_thread(void *data)
{
    while (1) {
        thread_waiter_wait(&c_volume_waiter);
        printf("adcl gain set %d\n", c_adcl_gain);
        pcm_private_ctrl(capture_dev, "adcl_gain", c_adcl_gain);
    }
}

static int c_feature_volume(u8 request, void *buf, u16 len)
{
    u16 *data = (u16 *)buf;

    if (len < 2)
        return -EOPNOTSUPP;

    len = 2;

    switch (request)
    {
        case UAC_SET_CUR:
            c_adcl_gain = 196 + ((s16)*data) / 128;
            thread_waiter_wakeup(&c_volume_waiter);
            break;
        case UAC_GET_CUR:
            *data = c_adcl_gain;
            break;
        case UAC_GET_MIN:
            *data = (u16)(-196 * 128);
            break;
        case UAC_GET_MAX:
            *data = 59 * 128;
            break;
        case UAC_GET_RES:
            *data = 128; // n * 1/256 (db) -- 0.5db
            break;
        default:
            return -EOPNOTSUPP;
    }

    return len;
}

static int c_feature_callback(u8 type, u8 request, void *buf, u16 len)
{
    int ret = -EOPNOTSUPP;

    switch (type)
    {
        case UAC_FU_VOLUME:
            ret = c_feature_volume(request, buf, len);
            break;
        default:
            break;
    }

    return ret;
}

/* uac capture */
static struct pcm_params capture_params = {
    .channels = 1,
    .pcm_data_fmt = pcm_fmt_S16LE,
    .pcm_sample_rate = pcm_rate_48000,
    .pcm_interface = pcm_interface_i2s,
    .i2s_frame_mode = i2s_LR_mode,
    .i2s_bclk_direction = i2s_bclk_codec_master,
    .i2s_frame_direction = i2s_frame_codec_master,
};

void pcm_capture_thread(void *data)
{
    struct pcm_device *c_dai = pcm_get("aic-capture");
    u32 len;
    u8 *buffer;

    assert(c_dai);

    pcm_private_ctrl(c_dai, "sysclk-set-rate", 48000 * 768);
    pcm_private_ctrl(c_dai, "sysclk-set-output", 1);

    pcm_enable(c_dai, &capture_params);
    pcm_enable(capture_dev, &capture_params);

    len = pcm_data_sample_rate(capture_params.pcm_sample_rate) * pcm_frame_size(&capture_params) / 1000 * PKT_SIZE_MS;
    buffer = malloc(len);
    assert(buffer);

    pcm_start(capture_dev);
    pcm_start(c_dai);

    while (1) {
        pcm_read_frame(c_dai, buffer, len / pcm_frame_size(&capture_params));

        gadget_uac1_capture_write(buffer, len);
    }

    pcm_disable(capture_dev);
    pcm_disable(c_dai);

    free(buffer);
}

struct uac1_params uac1_param = {
    /* capture */
    .c_chmask = UAC_CH_LAYOUT_MONO,
    .c_ssize = 2,
    .c_srate = 48000,
    .c_feature = UAC_CONTROL_BIT(UAC_FU_VOLUME),
    .c_feature_callback = c_feature_callback,

    .buffer_size_ms = BUF_SIZE_MS,
    .connect_cb = uac1_connect_callback,
};

static inline unsigned int icodec_get_bit(unsigned int reg, int start, int end)
{
    return get_bit_field_v(ICODEC_ADDR(reg), start, end);
}

void backlight_use(void)
{
    struct backlight *lcd_pwm;

#if defined(CONFIG_PWM_BACKLIGHT0_NAME)
    lcd_pwm = backlight_open(CONFIG_PWM_BACKLIGHT0_NAME);
#elif defined(CONFIG_GPIO_BACKLIGHT0_NAME)
    lcd_pwm = backlight_open(CONFIG_GPIO_BACKLIGHT0_NAME);
#else
    lcd_pwm = backlight_open("lcd_pwm");
#endif

    if (lcd_pwm != NULL)
        backlight_set_brightness(lcd_pwm, backlight_get_maxbrightness(lcd_pwm));

}

static void update_config(struct pcm_device *capture_dev)
{
    int gain_max = lv_dropdown_get_selected(dd_gain_max) - 1;
    int gain_min = lv_dropdown_get_selected(dd_gain_min) - 1;
    int range_max = atoi(lv_textarea_get_text(r_max));
    int range_min = atoi(lv_textarea_get_text(r_min));

    range_max = !range_max && lv_textarea_get_text(r_max)[0] != '0' ? -1 : range_max;
    range_max = range_max ? range_max : 1;
    range_min = !range_min && lv_textarea_get_text(r_min)[0] != '0' ? -1 : range_min;
    range_min = range_min ? range_min : 1;

    // default value
    if(gain_max == -1)
        gain_max = 7;
    if(gain_min == -1)
        gain_min = 0;
    if(range_max == -1)
        range_max = 16422;
    if(range_min == -1)
        range_min = 8294;

    pcm_private_ctrl(capture_dev, "pgain_max", gain_max);
    pcm_private_ctrl(capture_dev, "pgain_min", gain_min);
    pcm_private_ctrl(capture_dev, "range_max", range_max);
    pcm_private_ctrl(capture_dev, "range_min", range_min);

    printf("gain_max  = %6d  db\n", gain_max * 6 - 13);   // -13.5 acturlly
    printf("gain_min  = %6d  db\n", gain_min * 6 - 18);
    printf("range_max = %6d    \n", range_max);
    printf("range_min = %6d    \n", range_min);

    pcm_private_ctrl(capture_dev, "agc_on", 1);
    lv_label_set_text(lb_switch, "OFF");
}

static void update_chart(lv_timer_t * t)
{
    lv_obj_t * chart = t->user_data;
    lv_chart_series_t * ser = lv_chart_get_series_next(chart, NULL);

    int gain = icodec_get_bit(ALCOGR, ALCOGAIN);

    lv_chart_set_next_value(chart, ser, 90 / 30 * gain + 5);

    lv_chart_refresh(chart);

    if (old_gain != gain && lb_value_count != 20) {
        lv_point_t point;
        lv_chart_get_point_pos_by_id(chart, ser, lb_value_id, &point);

        char buf[20];
        sprintf(buf, "%d", gain);
        lv_label_set_text(lb_values[lb_value_count], buf);
        lv_obj_set_pos(lb_values[lb_value_count], point.x, point.y - (LV_FONT_DEFAULT)->line_height);
        lv_obj_clear_flag(lb_values[lb_value_count], LV_OBJ_FLAG_HIDDEN);
        old_gain = gain;

        lb_value_count ++;
    }

    lb_value_id = (lb_value_id + 1) % lv_chart_get_point_count(chart);
    if (lb_value_id == 0) {
        for (int i = 0; i < lb_value_count; i ++)
            lv_obj_add_flag(lb_values[i], LV_OBJ_FLAG_HIDDEN);
        lb_value_count = 0;
        ser_color = (ser_color + 1) % _LV_PALETTE_LAST;
        lv_chart_remove_series(chart, ser);
        lv_chart_add_series(chart, lv_palette_main(ser_color), LV_CHART_AXIS_SECONDARY_Y);
        old_gain = 0;
    }
}

/**
 * Circular line chart with gap
 */
void gain_chart_observer(void)
{
    /*Create a stacked_area_chart.obj*/
    lv_obj_t *chart = lv_chart_create(lv_scr_act());
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_set_size(chart, LV_HOR_RES - 40, min(LV_HOR_RES - 40, LV_VER_RES - 60));
    lv_obj_align(chart, LV_ALIGN_CENTER, 0, 50);

    lv_chart_set_point_count(chart, 500);
    lv_chart_add_series(chart, lv_palette_main(ser_color), LV_CHART_AXIS_SECONDARY_Y);

    for (int i = 0; i < LB_VALUE_MAX; i ++) {
        lb_values[i] = lv_label_create(chart);
        lv_obj_add_flag(lb_values[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_create(update_chart, 40, chart);
}

static void ta_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);
    lv_obj_t * kb = lv_event_get_user_data(e);
    if(code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }

    if(code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(kb, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

        update_config(capture_dev);
    }
}

static void event_cb(lv_event_t * e)
{
    if (agc_on) {
        pcm_private_ctrl(capture_dev, "agc_on", 0);
        lv_label_set_text(lb_switch, "ON");
    } else {
        update_config(capture_dev);
        pcm_private_ctrl(capture_dev, "agc_on", 1);
        lv_label_set_text(lb_switch, "OFF");
    }
    agc_on = !agc_on;
}

static void event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    if(code == LV_EVENT_VALUE_CHANGED) {
        char buf[32];
        lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
        printf("Option: %s\n", buf);

        update_config(capture_dev);
    }
}

void parameter_configuration(void)
{
    static const char *max = "max\n" "0\n" "1\n" "2\n" "3\n" "4\n" "5\n" "6\n" "7";
    static const char *min = "min\n" "0\n" "1\n" "2\n" "3\n" "4\n" "5\n" "6\n" "7";

    dd_gain_max = lv_dropdown_create(lv_scr_act());
    lv_dropdown_set_options_static(dd_gain_max, max);
    lv_obj_align(dd_gain_max, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_obj_add_event_cb(dd_gain_max, event_handler, LV_EVENT_ALL, NULL);

    dd_gain_min = lv_dropdown_create(lv_scr_act());
    lv_dropdown_set_options_static(dd_gain_min, min);
    lv_obj_align_to(dd_gain_min, dd_gain_max, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    lv_obj_add_event_cb(dd_gain_min, event_handler, LV_EVENT_ALL, NULL);

    lv_obj_t * kb = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    r_max = lv_textarea_create(lv_scr_act());
    lv_textarea_set_placeholder_text(r_max, "max");
    lv_obj_set_size(r_max, 130, LV_SIZE_CONTENT);
    lv_obj_align_to(r_max, dd_gain_min, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    lv_obj_add_event_cb(r_max, ta_event_cb, LV_EVENT_ALL, kb);

    r_min = lv_textarea_create(lv_scr_act());
    lv_textarea_set_placeholder_text(r_min, "min");
    lv_obj_set_size(r_min, 130, LV_SIZE_CONTENT);
    lv_obj_align_to(r_min, r_max, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    lv_obj_add_event_cb(r_min, ta_event_cb, LV_EVENT_ALL, kb);

    btn_switch = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_switch, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(btn_switch, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_event_cb(btn_switch, event_cb, LV_EVENT_CLICKED, NULL);
    lb_switch = lv_label_create(btn_switch);
    lv_label_set_text(lb_switch, "ON");
    agc_on = 0;
    lv_obj_center(lb_switch);
}

void lvgl_agc_observer(void)
{
    const char *fb_path = "fb0";

    char *device_name = "gt9xx";
    /* 该demo以gt9xx触摸屏作为lvgl input */
    struct goodix_ts_data gt9xx;
    goodix_touch_init(&gt9xx);
    backlight_use();

    lv_init();

    int ret = lvgl_init_fb_display(fb_path);
    assert(!ret);
    ret = lvgl_init_tp_input(device_name);
    assert(!ret);

    gain_chart_observer();
    parameter_configuration();

    lvgl_start(10 * 1000);
}

int gadget_usb_uac1_observer(void)
{
    thread_waiter_init(&c_volume_waiter);
    thread_create("pcm_capture_thread", 4096, pcm_capture_thread, NULL);
    thread_create("c_volume_thread", 1024, c_volume_thread, NULL);

    gadget_uac1_init(&uac1_id, &uac1_param);

    return 0;
}

void agc_test(void)
{
    capture_dev = pcm_get("icodec-capture");
    assert(capture_dev);

    pcm_private_ctrl(capture_dev, "alcl_gain", 6);
    gadget_usb_uac1_observer();
    lvgl_agc_observer();
}
