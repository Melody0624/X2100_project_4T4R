#ifndef __ISCAN_VIEW_H__
#define __ISCAN_VIEW_H__

#include "languages_str.h"
#include <list.h>
#include "../iscan_ui.h"
#include "../iscan_thread_decode.h"
#include "third_party/lvgl/lvgl_ingenic.h"
#include "third_party/lvgl/lvgl/lvgl.h"
#include "third_party/lvgl/ilv_color.h"

extern lv_style_t *font_languages;

struct iscan_config_data {
    int num_files;
    enum iscan_bat_status bat_status;
    enum iscan_encoder_mode encoder_mode;
    enum iscan_dpi_mode dpi_mode;
    enum iscan_color_mode color_mode;
    enum iscan_languags language;
    iscan_dec_t *dec;

    struct iscan_ui_callback *scan_cb;
};


lv_obj_t *iscan_menu_view_init(struct iscan_config_data *data);
void iscan_menu_view_key_event(int key, int value);
lv_obj_t *iscan_menu_view_get(void);


lv_obj_t *iscan_setting_view_init(struct iscan_config_data *data);
void iscan_setting_view_key_event(int key, int key_value);
lv_obj_t *iscan_setting_view_get(void);

lv_obj_t *iscan_preview_view_init(struct iscan_config_data *data);
void iscan_preview_view_key_event(int key, int key_value);
lv_obj_t *iscan_preview_view_get(void);

lv_obj_t *iscan_scan_view_init(struct iscan_config_data *data);
void iscan_scan_view_key_event(int key, int value);
lv_obj_t *iscan_scan_view_get(void);

lv_obj_t *iscan_calibrate_view_init(struct iscan_config_data *data);


lv_obj_t *iscan_msgbox_create(void);
void iscan_msgbox_set(lv_obj_t *msgbox, const char *title, const char *opt1, const char *opt2);
int iscan_msgbox_get_focused(lv_obj_t *msgbox);


lv_obj_t *iscan_usbins_view_init(struct iscan_config_data *data);

#endif
