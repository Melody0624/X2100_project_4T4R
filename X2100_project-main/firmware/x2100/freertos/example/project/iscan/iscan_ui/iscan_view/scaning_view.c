#include <stdio.h>
#include "iscan_view.h"
#include <devices/gpio_keyboard.h>
#include <driver/input.h>
#include <common.h>

#define SCANING_SRC UI_IMGDIR "ICON_SCAN_SCANNING.bmp"
#define SCANERR_SRC UI_IMGDIR "ICON_SCAN_ERR.bmp"

static lv_obj_t *scan_view;
static lv_timer_t *timer;

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

static void scanning_timer_cb(struct _lv_timer_t * timer)
{
    struct iscan_config_data *data = lv_obj_get_user_data(scan_view);
    enum iscan_scan_status status;

    lv_obj_t *img = lv_obj_get_child(scan_view, 0);
    if (data->scan_cb->get_scan_status) {
        data->scan_cb->get_scan_status(&status);

        if (status == ISCAN_SCANERR) {
            lv_img_set_src(img, SCANERR_SRC);
        }

        if (status == ISCAN_SCANING) {
            lv_img_set_src(img, SCANING_SRC);
        }

    }
}

static void scan_view_event(lv_event_t *e)
{
    struct iscan_config_data *data = lv_obj_get_user_data(scan_view);
    if (!data)
        return;

    if (lv_event_get_code(e) == LV_EVENT_SCREEN_UNLOAD_START) {
        if (data->scan_cb->stop_scan)
            data->scan_cb->stop_scan();

        lv_timer_pause(timer);
    }

    if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOAD_START) {
        lv_obj_t *img = lv_obj_get_child(scan_view, 0);
        lv_img_set_src(img, SCANING_SRC);

        if (data->scan_cb->start_scan)
            data->scan_cb->start_scan(data->dpi_mode, data->color_mode, data->encoder_mode, "test");

        lv_timer_resume(timer);
    }

}

lv_obj_t *iscan_scan_view_init(struct iscan_config_data *data)
{

    scan_view = ilv_obj_create(NULL);
    lv_obj_set_size(scan_view, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(scan_view, lv_color_from_int(LV_COLOR_BLUE_darken3), 0);
    lv_obj_set_user_data(scan_view, data);
    lv_obj_add_event_cb(scan_view, scan_view_event, LV_EVENT_SCREEN_LOAD_START, NULL);

    lv_obj_t *img_bg = lv_img_create(scan_view);

    lv_obj_center(img_bg);

    timer = lv_timer_create(scanning_timer_cb, 200, NULL);
    lv_timer_pause(timer);

    return scan_view;
}

lv_obj_t *iscan_scan_view_get(void)
{
    return scan_view;
}

void iscan_scan_view_key_event(int key, int value)
{

    if (value != 0)
        return;

    if (key != KEY_POWER)
        return;

    struct iscan_config_data *data = lv_obj_get_user_data(scan_view);
    if (!data)
        return;

    lv_obj_t *menu = iscan_menu_view_get();
    if (menu)
        lv_scr_load(menu);

}
