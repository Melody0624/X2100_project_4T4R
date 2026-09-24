#include "iscan_view.h"
#include <driver/input_key.h>
#include <common.h>
#include "languages_str.h"


struct msgbox_handle {
    lv_obj_t *msgobx;
    lv_obj_t *title_lab;
    lv_obj_t *opt1_lab;
    lv_obj_t *opt2_lab;
    lv_group_t *key_group;
};


#define RETURN_SRC      UI_IMGDIR "ICON_RETURN.bmp"
#define OK_SRC          UI_IMGDIR "ICON_OK.bmp"
#define UP_SRC          UI_IMGDIR "ICON_DIR_UP.bmp"
#define DOWN_SRC        UI_IMGDIR "ICON_DIR_DOWN.bmp"


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

static void message_event_key(lv_event_t *e)
{
    int key = lv_event_get_key(e);
    lv_obj_t *msgbox = lv_event_get_target(e);
    struct msgbox_handle *handle = lv_obj_get_user_data(msgbox);

    if (key == LV_KEY_UP)
        lv_group_focus_prev(handle->key_group);

    if (key == LV_KEY_DOWN)
        lv_group_focus_next(handle->key_group);
}

lv_obj_t *iscan_msgbox_create(void)
{
    struct msgbox_handle *handle = malloc(sizeof(struct msgbox_handle));
    if (!handle) {
        printf("malloc msgbox handle err\n");
        return NULL;
    }

    lv_obj_t *message = ilv_obj_create(lv_layer_top());
    lv_obj_set_size(message, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(message, lv_color_make(0x00,0x00,0x00), 0);
    lv_obj_set_style_bg_opa(message, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(message, message_event_key, LV_EVENT_KEY,NULL);
    lv_obj_set_user_data(message, handle);

    lv_obj_t *icon_obj = ilv_obj_create(message);
    lv_obj_set_size(icon_obj, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(icon_obj, 0, 0);


    lv_obj_set_layout(icon_obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(icon_obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(icon_obj, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);


    lv_obj_t *left = ilv_obj_create(icon_obj);
    lv_obj_set_size(left, LV_SIZE_CONTENT, lv_pct(100));
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(left, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_set_style_bg_color(left, lv_color_make(0,0,0), 0);
    lv_obj_set_style_bg_opa(left, 0, 0);


    lv_obj_t *right = ilv_obj_create(icon_obj);
    lv_obj_set_size(right, LV_SIZE_CONTENT, lv_pct(100));
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(right, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_set_style_bg_color(right, lv_color_make(0,0,0), 0);
    lv_obj_set_style_bg_opa(right, 0, 0);


    lv_obj_t *top_left = lv_img_create(left);
    lv_obj_t *bottom_left = lv_img_create(left);

    lv_obj_t *top_right = lv_img_create(right);
    lv_obj_t *bottom_right = lv_img_create(right);

    lv_img_set_src(top_left, RETURN_SRC);
    lv_img_set_src(top_right, UP_SRC);
    lv_img_set_src(bottom_left, OK_SRC);
    lv_img_set_src(bottom_right, DOWN_SRC);

    lv_obj_t *msg_label = lv_obj_create(message);
    lv_obj_set_style_pad_all(msg_label, 0, 0);
    lv_obj_set_flex_flow(msg_label, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(msg_label, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_set_size(msg_label, lv_pct(60), lv_pct(40));
    lv_obj_add_style(msg_label, font_languages, 0);
    lv_obj_center(msg_label);

    lv_obj_t *title = ilv_obj_create(msg_label);
    lv_obj_t *opt1 = ilv_obj_create(msg_label);
    lv_obj_t *opt2 = ilv_obj_create(msg_label);

    lv_obj_set_size(title, lv_pct(100), lv_pct(50));
    lv_obj_set_size(opt1, lv_pct(100), lv_pct(25));
    lv_obj_set_size(opt2, lv_pct(100), lv_pct(25));

    lv_obj_t *title_lab = lv_label_create(title);
    lv_obj_t *opt1_lab = lv_label_create(opt1);
    lv_obj_t *opt2_lab = lv_label_create(opt2);

    lv_obj_center(title_lab);
    lv_obj_center(opt1_lab);
    lv_obj_center(opt2_lab);

    lv_obj_set_style_bg_color(opt1, lv_color_from_int(LV_COLOR_GREY_lighten1), 0);
    lv_obj_set_style_bg_color(opt2, lv_color_from_int(LV_COLOR_GREY_lighten1), 0);

    lv_obj_set_style_bg_color(opt1, lv_color_from_int(LV_COLOR_BLUE_darken1), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(opt2, lv_color_from_int(LV_COLOR_BLUE_darken1), LV_STATE_FOCUSED);

    lv_group_t *key_group = lv_group_create();

    lv_group_add_obj(key_group, opt1);
    lv_group_add_obj(key_group, opt2);

    lv_group_set_wrap(key_group, false);

    lv_obj_add_flag(message, LV_OBJ_FLAG_HIDDEN);

    handle->msgobx = message;
    handle->opt1_lab = opt1_lab;
    handle->opt2_lab = opt2_lab;
    handle->title_lab = title_lab;
    handle->key_group = key_group;

    return handle->msgobx;

}

void iscan_msgbox_set(lv_obj_t *msgbox, const char *title, const char *opt1, const char *opt2)
{
    struct msgbox_handle *handle = lv_obj_get_user_data(msgbox);

    lv_label_set_text(handle->title_lab, title);
    lv_label_set_text(handle->opt1_lab, opt1);
    lv_label_set_text(handle->opt2_lab, opt2);

    lv_group_focus_obj(lv_obj_get_parent(handle->opt2_lab));
}

int iscan_msgbox_get_focused(lv_obj_t *msgbox)
{
    struct msgbox_handle *handle = lv_obj_get_user_data(msgbox);
    lv_obj_t *focused = lv_group_get_focused(handle->key_group);
    if (focused == lv_obj_get_parent(handle->opt1_lab))
        return 1;
    else if (focused == lv_obj_get_parent(handle->opt2_lab))
        return 2;
    else
        return -1;
}