#include <stdio.h>
#include "iscan_view.h"
#include <devices/gpio_keyboard.h>
#include <driver/input.h>
#include <common.h>

#define CAL_start 0
#define CAL_complete 1

static lv_obj_t *calibrate_view;
static lv_obj_t *label;
static lv_obj_t *spinner;
static lv_timer_t *timer;

static int calibrate_st;

static lv_obj_t *ilv_obj_create(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);

    lv_obj_set_style_pad_top(obj, 0, 0);
    lv_obj_set_style_pad_bottom(obj, 0, 0);
    lv_obj_set_style_pad_left(obj, 0, 0);
    lv_obj_set_style_pad_right(obj, 0, 0);
    lv_obj_set_style_pad_row(obj, 0, 0);
    lv_obj_set_style_pad_column(obj, 0, 0);

    return obj;
}

static void calibrate_view_event(lv_event_t *e)
{
    struct iscan_config_data *data = lv_obj_get_user_data(calibrate_view);

    if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOAD_START) {
        lv_label_set_text(label, STRING_TABLE[data->language][STRID_CAL_SHEET]);
        lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);

        if (data->scan_cb->start_cal)
            data->scan_cb->start_cal();

        calibrate_st = CAL_start;

        lv_timer_set_period(timer, 500);
        lv_timer_resume(timer);
    }

    if (lv_event_get_code(e) == LV_EVENT_SCREEN_UNLOAD_START) {
        lv_timer_pause(timer);
    }
}

static void get_calibrate_status_timer(lv_timer_t *timer)
{
    struct iscan_config_data *data = lv_obj_get_user_data(calibrate_view);
    int status = 0;

    if (calibrate_st == CAL_start) {
        if (data->scan_cb->get_cal_status)
            data->scan_cb->get_cal_status(&status);
    } else {
        lv_obj_t *menu = iscan_menu_view_get();
        if (menu)
            lv_scr_load(menu);
    }

    if (status) {
        lv_label_set_text(label, STRING_TABLE[data->language][STRID_CAL_COMPLETE]);
        lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        calibrate_st = CAL_complete;
        lv_timer_set_period(timer, 3*1000);
    }
}

lv_obj_t *iscan_calibrate_view_init(struct iscan_config_data *data)
{
    calibrate_view = ilv_obj_create(NULL);
    lv_obj_set_size(calibrate_view, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(calibrate_view, lv_color_from_int(LV_COLOR_GREY_lighten1), 0);
    lv_obj_set_user_data(calibrate_view, data);
    lv_obj_add_event_cb(calibrate_view, calibrate_view_event, LV_EVENT_SCREEN_LOAD_START, NULL);
    lv_obj_add_event_cb(calibrate_view, calibrate_view_event, LV_EVENT_SCREEN_UNLOAD_START, NULL);

    lv_obj_t *info = ilv_obj_create(calibrate_view);
    lv_obj_set_style_bg_opa(info, 0, 0);
    lv_obj_set_size(info, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(info, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(info, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(info, font_languages, 0);

    spinner = lv_spinner_create(info, 1000, 60);
    lv_obj_set_size(spinner, 40, 40);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_INDICATOR);

    label = lv_label_create(info);
    lv_label_set_text(label, STRING_TABLE[data->language][STRID_CAL_SHEET]);

    timer = lv_timer_create(get_calibrate_status_timer, 500, NULL);
    lv_timer_pause(timer);

    lv_obj_center(info);

    return calibrate_view;
}
