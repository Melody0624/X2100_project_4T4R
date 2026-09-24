

#include <stdio.h>
#include <os.h>
#include <devices/gpio_keyboard.h>
#include <driver/input.h>
#include <errno.h>
#include <common.h>
#include <time.h>
#include <stdint.h>
#include "iscan_view/iscan_view.h"
#include "iscan_utils.h"
#include "third_party/lvgl/lv_libjpeg_turbo.h"

#define ISCAN_DEC_CACHE_TOTAL 20

static lv_obj_t *menu;
static lv_obj_t *setting;
static lv_obj_t *preview;
static lv_obj_t *scaning;
static lv_obj_t *usb_inserted;
static lv_obj_t *calibrate;

static lv_timer_t *devices_st;

lv_style_t *font_languages;

static struct iscan_config_data iscan_data;


static void iscan_devices_st_timecb(lv_timer_t *timer)
{
    enum iscan_bat_status bat_status = iscan_data.bat_status;
    if (iscan_data.scan_cb->get_battery)
        iscan_data.scan_cb->get_battery(&bat_status);

    iscan_data.bat_status = bat_status;
    if (lv_disp_get_scr_act(NULL) == menu)
        lv_scr_load(menu);

    int is_insert = 0;
    if (iscan_data.scan_cb->get_usb_insert)
        iscan_data.scan_cb->get_usb_insert(&is_insert);

    if (is_insert && lv_disp_get_scr_act(NULL) != usb_inserted)
        lv_scr_load(usb_inserted);

    if (!is_insert && lv_disp_get_scr_act(NULL) == usb_inserted)
        lv_scr_load(menu);
}

static void iscan_ui_thread(void *data)
{

    menu = iscan_menu_view_init(&iscan_data);
    setting = iscan_setting_view_init(&iscan_data);
    preview = iscan_preview_view_init(&iscan_data);
    scaning = iscan_scan_view_init(&iscan_data);
    usb_inserted = iscan_usbins_view_init(&iscan_data);
    calibrate = iscan_calibrate_view_init(&iscan_data);

    devices_st = lv_timer_create(iscan_devices_st_timecb, 1*1000, NULL);

    lv_scr_load(menu);
    lvgl_start(10 * 1000);
}



void iscan_ui_init(struct iscan_ui_callback *iscan_ui_callback, char *img_path)
{
    iscan_data.bat_status = ISCAN_BAT_FULL;
    iscan_data.color_mode = ISCAN_COLOR_COLOR;
    iscan_data.dpi_mode = ISCAN_DPI_FI;
    iscan_data.num_files = 0;
    iscan_data.encoder_mode = ISCAN_ENCODER_JPEG;
    iscan_data.language = LANG_SC;
    iscan_data.scan_cb = iscan_ui_callback;

    if (iscan_ui_callback->get_battery)
        iscan_ui_callback->get_battery(&iscan_data.bat_status);


    lv_init();

    lv_libjpeg_turbo_init();

    int ret = lvgl_init_fb_display("fb0");
    assert(!ret);

    font_languages = ilv_load_font("/userdata/StringTable.ttf", 16);
    assert(font_languages);

    iscan_data.dec = iscan_dec_create(lv_disp_get_hor_res(NULL),
                                      lv_disp_get_ver_res(NULL),
                                      img_path,
                                      ISCAN_DEC_CACHE_TOTAL);

    iscan_data.num_files = iscan_dec_get_nums(iscan_data.dec);
    if (iscan_data.dec)
        iscan_dec_set_center(iscan_data.dec, ISCAN_DEC_TAIL);


    thread_create("iscan_ui_thread", 32 * 1024, iscan_ui_thread, NULL);
}


void iscan_ui_key_clicked(int key, int value)
{

    static int key_up_value;
    lvgl_lock();

    lv_obj_t * current = lv_disp_get_scr_act(NULL);

    if (key == KEY_UP)
        key_up_value = value;

    if (key_up_value && key == KEY_POWER && value == 0) {
        if (current != calibrate) {
            lv_scr_load(calibrate);
            current = calibrate;
        }
    }


    if (current == menu) {
        iscan_menu_view_key_event(key, value);
    } else if (current == setting) {
        iscan_setting_view_key_event(key, value);
    } else if (current == preview) {
        iscan_preview_view_key_event(key, value);
    } else if (current == scaning) {
        iscan_scan_view_key_event(key, value);
    }

    lvgl_unlock();
}
