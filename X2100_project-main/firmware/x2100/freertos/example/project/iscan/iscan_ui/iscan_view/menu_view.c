#include "iscan_view.h"
#include <driver/input_key.h>
#include <common.h>


#define SET_SRC(value, src) \
    [value] = UI_IMGDIR src

#define SETTING_SRC     (UI_IMGDIR "ICON_MAIN_SETUP.bmp")
#define PLAYBACK_SRC    (UI_IMGDIR "ICON_PLAYBACK.bmp")
#define SDCARD_SRC      (UI_IMGDIR "ICON_SD.bmp")

static char bat_st_src[][64] = {
    SET_SRC(ISCAN_BAT_EMPTY, "ICON_BATTERY_EMPTY.bmp"),
    SET_SRC(ISCAN_BAT_LOW, "ICON_BATTERY_LOW.bmp"),
    SET_SRC(ISCAN_BAT_MED, "ICON_BATTERY_MED.bmp"),
    SET_SRC(ISCAN_BAT_FULL, "ICON_BATTERY_FULL.bmp"),
    SET_SRC(ISCAN_BAT_CHARGER, "ICON_BATTERY_USB_CHARGER.bmp"),
};

static char en_mode_src[][64] = {
    SET_SRC(ISCAN_ENCODER_JPEG, "ICON_FILEFORAMT_JPG.bmp"),
    SET_SRC(ISCAN_ENCODER_PDF, "ICON_FILEFORMAT_PDF.bmp"),
};

static char dpi_mode_src[][64] = {
    SET_SRC(ISCAN_DPI_LO, "ICON_DPI_LOW.bmp"),
    SET_SRC(ISCAN_DPI_HI, "ICON_DPI_HI.bmp"),
    SET_SRC(ISCAN_DPI_FI, "ICON_DPI_FI.bmp"),
};

static char color_mode_src[][64] = {
    SET_SRC(ISCAN_COLOR_COLOR, "ICON_COLOR_COLOR.bmp"),
    SET_SRC(ISCAN_COLOR_MONO, "ICON_COLOR_MONO.bmp"),
};

static lv_obj_t *menu;
static lv_obj_t *setting;
static lv_obj_t *enc_mode;
static lv_obj_t *preview;
static lv_obj_t *sdcard_status;
static lv_obj_t *dpi_mode;
static lv_obj_t *battery_st;
static lv_obj_t *color_mode;
static lv_obj_t *nums_file;


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

static int menu_view_update(struct iscan_config_data *data)
{
    if (!menu) {
        printf("iscan main view is not init\n");
        return -1;
    }

    lv_img_set_src(battery_st, bat_st_src[data->bat_status]);

    lv_img_set_src(color_mode, color_mode_src[data->color_mode]);

    lv_obj_set_style_bg_img_src(enc_mode, en_mode_src[data->encoder_mode], 0);

    lv_label_set_text_fmt(nums_file, "#ffffff %04d#", data->num_files);

    lv_obj_set_style_bg_img_src(dpi_mode, dpi_mode_src[data->dpi_mode], 0);

    return 0;
}

static void menu_event_cb(lv_event_t *e)
{
    struct iscan_config_data *data = lv_obj_get_user_data(menu);
    if (!data)
        return;

    if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOAD_START) {
        data->num_files = iscan_dec_get_nums(data->dec);
        menu_view_update(data);
    }
}

lv_obj_t *iscan_menu_view_init(struct iscan_config_data *data)
{
    menu = ilv_obj_create(NULL);
    lv_obj_set_size(menu, lv_pct(100), lv_pct(100));
    lv_obj_set_layout(menu, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(menu, LV_FLEX_ALIGN_SPACE_AROUND, 0);
    lv_obj_set_style_bg_color(menu, lv_color_make(0xff,0xff,0xff), 0);
    lv_obj_clear_flag(menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(menu, data);
    lv_obj_add_event_cb(menu, menu_event_cb, LV_EVENT_SCREEN_UNLOAD_START, NULL);
    lv_obj_add_event_cb(menu, menu_event_cb, LV_EVENT_SCREEN_LOAD_START, NULL);


    lv_obj_t *row1 = ilv_obj_create(menu);
    lv_obj_set_layout(row1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(row1, lv_pct(100), lv_pct(48));
    lv_obj_set_style_flex_main_place(row1, LV_FLEX_ALIGN_SPACE_AROUND, 0);

    lv_obj_t *row2 = ilv_obj_create(menu);
    lv_obj_set_layout(row2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(row2, lv_pct(100), lv_pct(48));
    lv_obj_set_style_flex_main_place(row2, LV_FLEX_ALIGN_SPACE_AROUND, 0);


    setting = ilv_obj_create(row1);
    lv_obj_set_size(setting, lv_pct(65) , lv_pct(100));
    lv_obj_set_style_bg_color(setting, lv_color_make(0xff, 0, 0), 0);
    lv_obj_set_style_bg_img_src(setting, SETTING_SRC, 0);

    battery_st = lv_img_create(setting);
    lv_obj_align(battery_st, LV_ALIGN_TOP_LEFT, 2 , 2);

    color_mode = lv_img_create(setting);
    lv_obj_align(color_mode, LV_ALIGN_TOP_RIGHT, -2 , 2);

    enc_mode = ilv_obj_create(row1);
    lv_obj_set_size(enc_mode, lv_pct(32), lv_pct(100));
    lv_obj_set_style_bg_color(enc_mode, lv_color_make(0, 0, 0xff), 0);



    preview = ilv_obj_create(row2);
    lv_obj_set_size(preview, lv_pct(32), lv_pct(100));
    lv_obj_set_style_bg_color(preview, lv_color_make(0x80, 0x00, 0x80), 0);
    lv_obj_set_style_bg_img_src(preview, PLAYBACK_SRC, 0);

    sdcard_status = ilv_obj_create(row2);
    lv_obj_set_size(sdcard_status, lv_pct(32), lv_pct(100));
    lv_obj_set_style_bg_color(sdcard_status, lv_color_from_int(LV_COLOR_BLUE_darken1), 0);
    lv_obj_set_style_bg_img_src(sdcard_status, SDCARD_SRC, 0);

    nums_file = lv_label_create(sdcard_status);
    lv_obj_set_style_text_font(nums_file, &lv_font_montserrat_24, 0);
    lv_label_set_recolor(nums_file,  1);
    lv_obj_align(nums_file, LV_ALIGN_BOTTOM_MID, 0, 0);

    dpi_mode = ilv_obj_create(row2);
    lv_obj_set_size(dpi_mode, lv_pct(32), lv_pct(100));
    lv_obj_set_style_bg_color(dpi_mode, lv_color_make(0x00, 0xff, 0x00), 0);


    menu_view_update(data);

    return menu;
}

lv_obj_t *iscan_menu_view_get(void)
{
    return menu;
}

void iscan_menu_view_key_event(int key, int value)
{
    if (!menu)
        return;

    if (!lv_obj_is_valid(menu)) {
        return;
    }

    struct iscan_config_data *data = lv_obj_get_user_data(menu);

    if (key == KEY_DOWN && value == 0)
        data->dpi_mode = (data->dpi_mode + 1) %  ARRAY_SIZE(dpi_mode_src);

    if (key == KEY_UP && value == 0)
        data->encoder_mode = (data->encoder_mode + 1) % ARRAY_SIZE(en_mode_src);;

    if (key == KEY_SETUP && value == 0) {
        lv_obj_t *setting = iscan_setting_view_get();
        if (setting)
            lv_scr_load(setting);

        return;
    }

    if (key == KEY_REPLY && value == 0) {
        lv_obj_t *preview = iscan_preview_view_get();
        if (preview)
            lv_scr_load(preview);

        return;
    }

    if (key == KEY_POWER && value == 0) {
        lv_obj_t *scanning = iscan_scan_view_get();
        if (scanning)
            lv_scr_load(scanning);
    }

    menu_view_update(data);


    return;
}
