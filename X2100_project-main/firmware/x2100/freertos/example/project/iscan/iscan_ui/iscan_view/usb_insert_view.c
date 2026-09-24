#include <stdio.h>
#include "iscan_view.h"
#include <devices/gpio_keyboard.h>
#include <driver/input.h>
#include <common.h>

#define USB_INSERRT_SRC UI_IMGDIR "ICON_USB.bmp"

static lv_obj_t *usb_ins;


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


static void usbins_view_event(lv_event_t *e)
{
    struct iscan_config_data *data = lv_obj_get_user_data(usb_ins);
    if (!data || !data->dec)
        return;

    if (lv_event_get_code(e) == LV_EVENT_SCREEN_UNLOAD_START)
        iscan_dec_rescan(data->dec);

    return;
}

lv_obj_t *iscan_usbins_view_init(struct iscan_config_data *data)
{
    usb_ins = ilv_obj_create(NULL);
    lv_obj_set_size(usb_ins, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(usb_ins, lv_color_from_int(LV_COLOR_BLUE_darken3), 0);
    lv_obj_set_user_data(usb_ins, data);
    lv_obj_add_event_cb(usb_ins, usbins_view_event, LV_EVENT_SCREEN_UNLOAD_START, NULL);

    lv_obj_t *img_bg = lv_img_create(usb_ins);
    lv_img_set_src(img_bg, USB_INSERRT_SRC);

    lv_obj_center(img_bg);

    return usb_ins;
}
