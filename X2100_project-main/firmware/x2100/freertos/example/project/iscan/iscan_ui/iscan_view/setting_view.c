#include <stdio.h>
#include "iscan_view.h"
#include <devices/gpio_keyboard.h>
#include <driver/input.h>
#include <common.h>

#define SETTING_SRC  UI_IMGDIR "ICON_SUB_SETUP.bmp"
#define OK_SRC       UI_IMGDIR "ICON_OK.bmp"
#define UP_SRC       UI_IMGDIR "ICON_DIR_UP.bmp"
#define DOWN_SRC     UI_IMGDIR "ICON_DIR_DOWN.bmp"

static lv_obj_t *setting;
static lv_obj_t *setting_list;

static lv_group_t *key_group;
static lv_obj_t *msgbox;


static int en_mode_src[] = {
    [ISCAN_ENCODER_JPEG] = STRID_JPG_FILE,
    [ISCAN_ENCODER_PDF]  = STRID_PDF_A4_SIZE,
};

static int dpi_mode_src[] = {
    [ISCAN_DPI_LO] = STRID_LOW_300DPI,
    [ISCAN_DPI_HI] = STRID_HIGH_600DPI,
    [ISCAN_DPI_FI] = STRID_FINE_1200DPI,
};

static int color_mode_src[] = {
    [ISCAN_COLOR_COLOR] = STRID_COLOR_COLOR,
    [ISCAN_COLOR_MONO] = STRID_COLOR_MONO,
};

static int languages_src[] = {
    [LANG_EN] = STRID_LANG_EN,
    [LANG_ES] = STRID_LANG_ES,
    [LANG_FR] = STRID_LANG_FR,
    [LANG_SC] = STRID_LANG_SC,
    [LANG_TC] = STRID_LANG_TC,
    [LANG_DE] = STRID_LANG_DE,
    [LANG_IT] = STRID_LANG_IT,
    [LANG_RU] = STRID_LANG_RU,
    [LANG_JP] = STRID_LANG_JP,
    [LANG_DU] = STRID_LANG_DU,
};

struct {
    lv_obj_t *dpi;
    lv_obj_t *color;
    lv_obj_t *en_mode;
    lv_obj_t *format;
    lv_obj_t *language;
}options;

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

static void lv_obj_clean_style(lv_obj_t *obj)
{
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

}

static lv_obj_t *get_dropdown(lv_obj_t *option)
{
    return lv_obj_get_child(option, -1);
}

static void setting_option_update(lv_obj_t *opt, int languages, int *options, int option_cnt, int index)
{
    if (!opt)
        return;

    int opt_strid = (int)lv_obj_get_user_data(opt);

    lv_obj_t *label = lv_obj_get_child(opt, 0);
    if (!label)
        return;

    lv_label_set_text(label, STRING_TABLE[languages][opt_strid]);

    lv_obj_t *drop = lv_obj_get_child(opt, 1);
    if (!drop)
        return;
    lv_dropdown_clear_options(drop);

    int i;
    for (i = 0; i < option_cnt; i++) {
        int strid = options[i];
        lv_dropdown_add_option(drop, STRING_TABLE[languages][strid], i);
    }

    lv_dropdown_set_selected(drop, index);
    lv_dropdown_close(drop);

}

static lv_obj_t *setting_option_create(int option_strid)
{
    lv_obj_t *opt = ilv_obj_create(setting_list);
    lv_obj_set_size(opt, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(opt, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(opt, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(opt , LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_set_style_bg_color(opt, lv_color_from_int(LV_COLOR_LIGHT_BLUE_lighten1), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(opt, 1, 0);
    lv_obj_set_style_border_side(opt, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_user_data(opt, (void *)option_strid);

    lv_style_value_t lv_test_style;
    lv_style_get_prop(font_languages, LV_STYLE_TEXT_FONT, &lv_test_style);
    lv_font_t *font = (void *)lv_test_style.ptr;

    lv_obj_t *label = lv_label_create(opt);
    lv_obj_set_style_pad_all(label, 4, 0);
    lv_obj_add_style(label, font_languages, 0);

    /*创建选项（下拉列表）*/
    lv_obj_t *drop = lv_dropdown_create(opt);
    lv_dropdown_clear_options(drop);
    lv_dropdown_set_symbol(drop, NULL);

    lv_obj_set_style_bg_opa(drop, 0, 0);
    lv_obj_clean_style(drop);
    lv_obj_set_size(drop, lv_pct(60), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(drop, 4, 0);
    lv_obj_set_style_text_align(drop, LV_TEXT_ALIGN_CENTER, LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(drop, lv_color_from_int(LV_COLOR_GREY_darken3), 0);

    lv_obj_set_style_text_font(drop, font, 0);

    lv_obj_t *droplist = lv_dropdown_get_list(drop);
    lv_obj_set_style_text_align(droplist, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(droplist, lv_color_from_int(LV_COLOR_GREY_darken3), 0);
    lv_obj_set_style_max_height(droplist, lv_pct(50), 0);
    lv_obj_set_style_text_font(droplist, font, 0);

    lv_dropdown_set_symbol(drop, NULL);

    return opt;
}


static void options_update(struct iscan_config_data *data)
{
    setting_option_update(options.dpi, data->language, dpi_mode_src, ARRAY_SIZE(dpi_mode_src), data->dpi_mode);
    setting_option_update(options.color, data->language,  color_mode_src, ARRAY_SIZE(color_mode_src), data->color_mode);
    setting_option_update(options.en_mode, data->language ,en_mode_src, ARRAY_SIZE(en_mode_src) ,data->encoder_mode);
    setting_option_update(options.format, data->language, NULL, 0, 0);
    setting_option_update(options.language, data->language, languages_src, ARRAY_SIZE(languages_src), data->language);
}

static void setting_event_cb(lv_event_t * e)
{
    struct iscan_config_data *data = lv_obj_get_user_data(setting);
    if (!data)
        return;

    if (lv_event_get_code(e) == LV_EVENT_SCREEN_UNLOAD_START) {
        data->dpi_mode = lv_dropdown_get_selected(get_dropdown(options.dpi));
        data->color_mode = lv_dropdown_get_selected(get_dropdown(options.color));
        data->encoder_mode = lv_dropdown_get_selected(get_dropdown(options.en_mode));
        data->language = lv_dropdown_get_selected(get_dropdown(options.language));
        return;
    }

    if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOAD_START) {
        options_update(data);
        lv_group_focus_obj(options.dpi);
    }
}

static void langeuages_event_cb(lv_event_t *e)
{
    struct iscan_config_data *data = lv_obj_get_user_data(setting);
    data->language = lv_dropdown_get_selected(get_dropdown(options.language));

    options_update(data);
}


lv_obj_t *iscan_setting_view_init(struct iscan_config_data *data)
{
    setting = ilv_obj_create(NULL);
    lv_obj_set_layout(setting, LV_LAYOUT_FLEX);
    lv_obj_set_size(setting, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(setting, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(setting, lv_color_make(0x00,0x00,0x00), 0);
    lv_obj_clear_flag(setting, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(setting, (void *)data);
    lv_obj_add_event_cb(setting, setting_event_cb, LV_EVENT_SCREEN_UNLOAD_START, NULL);
    lv_obj_add_event_cb(setting, setting_event_cb, LV_EVENT_SCREEN_LOAD_START, NULL);

    lv_obj_t *top = ilv_obj_create(setting);
    lv_obj_set_size(top, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(top, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(top, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_set_style_bg_color(top, lv_color_from_int(LV_COLOR_GREY), 0);


    lv_obj_t *img = lv_img_create(top);
    lv_img_set_src(img, SETTING_SRC);

    img = lv_img_create(top);
    lv_img_set_src(img, UP_SRC);

    setting_list = lv_list_create(setting);
    lv_obj_clean_style(setting_list);
    lv_obj_set_size(setting_list,lv_pct(100), LV_SIZE_CONTENT);
    // lv_obj_set_flex_grow(setting_list, 1);
    lv_obj_set_style_flex_main_place(setting_list, LV_FLEX_ALIGN_SPACE_AROUND, 0);

    lv_obj_t *bottom = ilv_obj_create(setting);
    lv_obj_set_size(bottom, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(bottom, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(bottom, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_set_style_bg_color(bottom, lv_color_from_int(LV_COLOR_GREY), 0);

    img = lv_img_create(bottom);
    lv_img_set_src(img, OK_SRC);

    img = lv_img_create(bottom);
    lv_img_set_src(img, DOWN_SRC);

    key_group = lv_group_create();

    options.dpi = setting_option_create(STRID_QUALITY);
    lv_group_add_obj(key_group, options.dpi);

    options.color = setting_option_create(STRID_COLOR_MODE);
    lv_group_add_obj(key_group, options.color);

    options.en_mode = setting_option_create(STRID_FILE_FORMAT);
    lv_group_add_obj(key_group, options.en_mode);

    options.format = setting_option_create(STRID_FORMAT);
    lv_group_add_obj(key_group, options.format);

    options.language = setting_option_create(STRID_LANGUAGE);
    lv_group_add_obj(key_group, options.language);

    lv_obj_add_event_cb(get_dropdown(options.language), langeuages_event_cb, LV_EVENT_VALUE_CHANGED, 0);

    options_update(data);

    msgbox = iscan_msgbox_create();

    return setting;
}


static void dropdown_key_event(lv_obj_t *dropdown, int key)
{
    char param;
    switch(key) {
        case KEY_UP: param = LV_KEY_UP; break;
        case KEY_DOWN: param = LV_KEY_DOWN; break;
        case KEY_SETUP: param = LV_KEY_ESC;break;
        case KEY_REPLY: param = LV_KEY_ENTER; break;
        default: return;
    }

    lv_event_send(dropdown, LV_EVENT_KEY, &param);
}

static void message_box_key_event(lv_obj_t *msgbox, int key)
{
    int param;
    struct iscan_config_data *data = lv_obj_get_user_data(setting);
    switch(key) {
        case KEY_UP: {
            param = LV_KEY_UP;
            lv_event_send(msgbox, LV_EVENT_KEY, &param);
            break;
        }
        case KEY_DOWN: {
            param = LV_KEY_DOWN;
            lv_event_send(msgbox, LV_EVENT_KEY, &param);
            break;
        }

        case KEY_SETUP:{
            lv_obj_add_flag(msgbox, LV_OBJ_FLAG_HIDDEN);
            break;
        }

        case KEY_REPLY: {
            /*调用回调函数-->格式化t卡*/
            if (iscan_msgbox_get_focused(msgbox)) {
                iscan_dec_clear(data->dec);

                if (data->scan_cb->format_sdcard) {
                    data->scan_cb->format_sdcard();
                }
            }

            lv_obj_add_flag(msgbox, LV_OBJ_FLAG_HIDDEN);
            break;
        }
        default:
            return;
    }
}

static void setting_key_event(lv_obj_t *setup, int key)
{
    char param;
    lv_obj_t *focused_obj = lv_group_get_focused(key_group);
    struct iscan_config_data *data = lv_obj_get_user_data(setup);

    switch (key) {
        case KEY_SETUP: {
            lv_obj_t *view = iscan_menu_view_get();
            if (view)
                lv_scr_load(view);
            break;
        }

        case KEY_UP: {
            lv_group_focus_prev(key_group);
            lv_obj_scroll_to_view(focused_obj, 1);
            break;
        }

        case KEY_DOWN : {
            lv_group_focus_next(key_group);
            lv_obj_scroll_to_view(focused_obj, 1);
            break;
        }

        case KEY_REPLY : {
            lv_obj_t *dropdown = get_dropdown(focused_obj);
            if (lv_dropdown_get_option_cnt(dropdown)) {
                param = LV_KEY_ENTER;
                lv_event_send(dropdown, LV_EVENT_KEY, &param);
            }

            if (focused_obj == options.format) {
                iscan_msgbox_set(msgbox, STRING_TABLE[data->language][STRID_FORMAT_MEMORY],
                                           STRING_TABLE[data->language][STRID_YES],
                                           STRING_TABLE[data->language][STRID_NO]);

                lv_obj_clear_flag(msgbox, LV_OBJ_FLAG_HIDDEN);
            }
        }

    default:
        break;
    }
}

void iscan_setting_view_key_event(int key, int key_value)
{

    if (!setting)
        return;

    if (!lv_obj_is_valid(setting))
        return;

    if (key_value != 0)
        return;

    if (key == KEY_POWER) {
        lv_obj_add_flag(msgbox, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *menu = iscan_menu_view_get();
        lv_scr_load(menu);
        return;
    }

    if (!lv_obj_has_flag(msgbox, LV_OBJ_FLAG_HIDDEN)) {
        message_box_key_event(msgbox, key);
        return;
    }

    lv_obj_t *focused_obj = lv_group_get_focused(key_group);
    lv_obj_t *dropdown = get_dropdown(focused_obj);
    if (lv_dropdown_is_open(dropdown)) {
        dropdown_key_event(dropdown, key);
        return;
    }

    setting_key_event(setting, key);

    return;
}

lv_obj_t *iscan_setting_view_get(void)
{
    return setting;
}