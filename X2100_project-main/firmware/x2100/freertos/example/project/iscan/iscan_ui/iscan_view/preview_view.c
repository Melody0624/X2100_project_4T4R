#include "iscan_view.h"
#include <driver/input_key.h>
#include <common.h>


#define TEST_IMG "/mmcblk0p0/3.jpg"

#define ZOOM_SRC     UI_IMGDIR "ICON_ZOOM.bmp"
#define DELETE_SRC   UI_IMGDIR "ICON_DELETE.bmp"
#define UP_SRC       UI_IMGDIR "ICON_DIR_UP.bmp"
#define DOWN_SRC     UI_IMGDIR "ICON_DIR_DOWN.bmp"

#define PAN_SRC      UI_IMGDIR "ICON_PAN.bmp"
#define LEFT_SRC     UI_IMGDIR "ICON_DIR_LEFT.bmp"
#define RIGHT_SRC    UI_IMGDIR "ICON_DIR_RIGHT.bmp"

enum preview_zoom_type {
    ZOOM_NONE,
    ZOOM_4X,
    ZOOM_8X,
    ZOOM_RESVERT,
};

enum img_select_type {
    SELECT_FIRST,
    SELECT_NEXT,
    SELECT_PREV,
    SELECT_LAST,
};

int zoom_value[] = {
    [ZOOM_NONE] = 1,
    [ZOOM_4X]  = 4,
    [ZOOM_8X] = 8,
};



struct {
    lv_obj_t *main;
    lv_obj_t *lab_info;
    lv_obj_t *bg_img;
    lv_obj_t *top_left;
    lv_obj_t *top_right;

    lv_obj_t *bottom_left;
    lv_obj_t *bottom_right;

    lv_obj_t *zoom_label;
    lv_obj_t *files_label;

    lv_obj_t *msgbox;

    enum preview_zoom_type  zoom_type;

    int pan_dir;  //0: 上下，1：左右

    char *current_path;
    int current_index;
} preview;

#define PREVIEW_PAN_STEP 10

static void preview_pan_y(lv_coord_t delta_y)
{
    if (preview.bg_img == NULL)
        return;

    lv_obj_t *bg_parent = lv_obj_get_parent(preview.bg_img);
    if (!bg_parent)
        return;

    lv_coord_t screen_h = lv_obj_get_height(bg_parent);
    if (screen_h <= 0)
        screen_h = lv_disp_get_ver_res(NULL);


    lv_coord_t img_h = lv_obj_get_self_height(preview.bg_img);
    if (img_h <= screen_h)
        return;

    lv_coord_t min_y = screen_h - img_h;
    lv_coord_t max_y = 0;
    lv_coord_t y = delta_y + lv_obj_get_y(preview.bg_img);

    if (y < min_y)
        y = min_y;
    if (y > max_y)
        y = max_y;

    lv_obj_set_y(preview.bg_img, y);

    lv_obj_update_layout(preview.bg_img);
}

static void preview_pan_x(lv_coord_t delta_x)
{
    if (preview.bg_img == NULL || preview.main == NULL)
        return;

    lv_obj_t *bg_parent = lv_obj_get_parent(preview.bg_img);
    if (!bg_parent)
        return;

    lv_coord_t screen_w = lv_obj_get_width(bg_parent);
    if (screen_w <= 0)
        screen_w = lv_disp_get_ver_res(NULL);


    lv_coord_t img_w = lv_obj_get_self_width(preview.bg_img);
    if (img_w <= screen_w)
        return;

    lv_coord_t min_x = screen_w - img_w;
    lv_coord_t max_x = 0;
    lv_coord_t x = delta_x + lv_obj_get_x(preview.bg_img);

    if (x < min_x)
        x = min_x;
    if (x > max_x)
        x = max_x;

    lv_obj_set_x(preview.bg_img, x);

    lv_obj_update_layout(preview.bg_img);
}

static void preview_pan(lv_coord_t delta)
{
    if (preview.pan_dir)
        preview_pan_x(delta);
    else
        preview_pan_y(delta);
}

static int calculate_lvgl_zoom(int disp_w, int disp_h, int img_w, int img_h)
{
    uint16_t zoom = LV_IMG_ZOOM_NONE;
    float w_scale = (float)disp_w / (float)img_w;
    float h_scale = (float)disp_h / (float)img_h;

    float scale;
    scale = w_scale < h_scale ? w_scale : h_scale;

    zoom = (uint16_t)(LV_IMG_ZOOM_NONE * scale);
    if (zoom < 1)
        zoom = 1;

    return zoom;
}


static void img_zoom_event(enum preview_zoom_type zoom_type)
{
    struct iscan_config_data *data = lv_obj_get_user_data(preview.main);

    int off_x = lv_obj_get_x(preview.bg_img);
    int off_y = lv_obj_get_y(preview.bg_img);
    int current_zoom = lv_img_get_zoom(preview.bg_img);

    if (zoom_type == ZOOM_NONE) {
        lv_img_dsc_t *img = iscan_dec_get_img(data->dec, preview.current_path);
        if (img && img->data)
            lv_img_set_src(preview.bg_img, img);
        else
            lv_img_set_src(preview.bg_img, preview.current_path);
    } else {
        lv_img_set_src(preview.bg_img, preview.current_path);
    }

    lv_img_set_size_mode(preview.bg_img, LV_IMG_SIZE_MODE_VIRTUAL);

    lv_obj_t *bg_parent = lv_obj_get_parent(preview.bg_img);

    int disp_w = lv_obj_get_width(bg_parent);
    int disp_h = lv_obj_get_height(bg_parent);

    int img_w = lv_obj_get_self_width(preview.bg_img);
    int img_h = lv_obj_get_self_height(preview.bg_img);

    int scale_w = disp_w * zoom_value[zoom_type];
    int scale_h = disp_h * zoom_value[zoom_type];

    lv_img_set_size_mode(preview.bg_img, LV_IMG_SIZE_MODE_REAL);


    int zoom = calculate_lvgl_zoom(scale_w, scale_h, img_w, img_h);

    lv_img_set_zoom(preview.bg_img, zoom);
    lv_obj_refr_size(preview.bg_img);

    img_w = lv_obj_get_width(preview.bg_img);
    img_h = lv_obj_get_height(preview.bg_img);

    if (zoom_type >= ZOOM_8X) {
        off_x = (off_x - disp_w / 2) * zoom / current_zoom + disp_w / 2;
        off_y = (off_y - disp_h / 2) * zoom / current_zoom + disp_h / 2;
    } else {
        off_x = (disp_w - img_w) / 2;
        off_y = (disp_h - img_h) / 2;
    }

    lv_obj_set_pos(preview.bg_img, off_x, off_y);
    lv_obj_update_layout(preview.bg_img);
}

static void preview_select_img(enum img_select_type type)
{
    struct iscan_config_data *data = lv_obj_get_user_data(preview.main);
    char *file_path = NULL;
    int index = preview.current_index;
    iscan_dec_t *dec = data ? data->dec : NULL;

    if (dec) {
        switch (type) {
        case SELECT_LAST:
            iscan_dec_set_center(dec, ISCAN_DEC_TAIL);
            file_path = iscan_dec_get_center(dec, &index);
            break;
        case SELECT_FIRST:
            iscan_dec_set_center(dec, ISCAN_DEC_HEAD);
            file_path = iscan_dec_get_center(dec, &index);
            break;
        case SELECT_NEXT:
            file_path = iscan_dec_get_next(dec, &index);
            break;
        case SELECT_PREV:
            file_path = iscan_dec_get_prev(dec, &index);
            break;
        default:
            iscan_dec_set_center(dec, ISCAN_DEC_TAIL);
            file_path = iscan_dec_get_center(dec, &index);
            break;
        }
    } else {
        file_path = preview.current_path;
        index = preview.current_index;
    }

    if (!file_path)
        index = 0;

    preview.current_path = file_path;
    preview.current_index = index;
    preview.zoom_type = ZOOM_NONE;
    preview.pan_dir = 0;

    img_zoom_event(ZOOM_NONE);
}

static void preview_update_label(void)
{
    lv_obj_align_to(preview.zoom_label,  preview.bottom_left, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    lv_obj_align_to(preview.files_label, preview.top_right, LV_ALIGN_OUT_LEFT_MID, -2, 0);

    if (preview.zoom_type == ZOOM_NONE) {
        lv_label_set_text_fmt(preview.files_label, "#ff0000 %04d#", preview.current_index);
        lv_obj_clear_flag(preview.files_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(preview.zoom_label, LV_OBJ_FLAG_HIDDEN);

        lv_obj_update_layout(preview.files_label);
        return;
    }

    lv_obj_add_flag(preview.files_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(preview.zoom_label, LV_OBJ_FLAG_HIDDEN);

    if (preview.zoom_type == ZOOM_4X)
        lv_label_set_text_fmt(preview.zoom_label, "#ff0000 x4.0#");
    else
        lv_label_set_text_fmt(preview.zoom_label, "#ff0000 x8.0#");


    lv_obj_update_layout(preview.zoom_label);

}



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

static void preview_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOAD_START) {
        lv_img_set_src(preview.top_left, DELETE_SRC);
        lv_img_set_src(preview.top_right, UP_SRC);
        lv_img_set_src(preview.bottom_left, ZOOM_SRC);
        lv_img_set_src(preview.bottom_right, DOWN_SRC);

        preview_select_img(SELECT_LAST);

        preview_update_label();
        return;
    }

    if (lv_event_get_code(e) == LV_EVENT_SCREEN_UNLOAD_START) {
        struct iscan_config_data *data = lv_obj_get_user_data(preview.main);
        if (data && data->dec)
            iscan_dec_set_center(data->dec, ISCAN_DEC_TAIL);

        int index;
        iscan_dec_get_center(data->dec, &index);
        preview.current_path = NULL;
        preview.current_index = 0;
        return;
    }

    return;
}

lv_obj_t *iscan_preview_view_init(struct iscan_config_data *data)
{
    lv_obj_t *main = ilv_obj_create(NULL);
    lv_obj_set_style_bg_opa(main, 0, 0);
    lv_obj_set_size(main, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(main, data);
    lv_obj_add_event_cb(main, preview_event_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
    lv_obj_add_event_cb(main, preview_event_cb, LV_EVENT_SCREEN_UNLOAD_START, NULL);

    lv_obj_t *img_obj = lv_img_create(main);
    lv_img_set_antialias(img_obj, true);

    lv_obj_t *lab_info = ilv_obj_create(main);
    lv_obj_set_style_bg_opa(lab_info, 0, 0);
    lv_obj_set_size(lab_info, lv_pct(100), lv_pct(100));

    lv_obj_t *icon_obj = ilv_obj_create(lab_info);
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

    lv_obj_t *zoom_label = lv_label_create(lab_info);
    lv_obj_t *files_label = lv_label_create(lab_info);



    lv_label_set_recolor(zoom_label,  1);
    lv_label_set_recolor(files_label,  1);


    preview.main = main;
    preview.lab_info = lab_info;
    preview.bg_img = img_obj;
    preview.top_left = top_left;
    preview.top_right = top_right;
    preview.bottom_left = bottom_left;
    preview.bottom_right = bottom_right;
    preview.zoom_label = zoom_label;
    preview.files_label = files_label;
    preview.msgbox = iscan_msgbox_create();

    return main;
}




static void reply_key_event(void)
{

    preview.zoom_type = (preview.zoom_type + 1) % ZOOM_RESVERT;
    img_zoom_event(preview.zoom_type);

    if (preview.zoom_type != ZOOM_NONE) {
        lv_img_set_src(preview.top_left, PAN_SRC);
    } else {
        lv_img_set_src(preview.top_right, UP_SRC);
        lv_img_set_src(preview.bottom_right, DOWN_SRC);
        lv_img_set_src(preview.top_left, DELETE_SRC);
    }

    preview_update_label();


}

static void setup_key_event(void)
{

    struct iscan_config_data *data = lv_obj_get_user_data(preview.main);

    if (preview.zoom_type != ZOOM_NONE) {
        preview.pan_dir = (preview.pan_dir + 1) % 2;
        if (preview.pan_dir) {
            lv_img_set_src(preview.top_right, LEFT_SRC);
            lv_img_set_src(preview.bottom_right, RIGHT_SRC);
        } else {
            lv_img_set_src(preview.top_right, UP_SRC);
            lv_img_set_src(preview.bottom_right, DOWN_SRC);
        }
    } else {
        lv_obj_t *msgbox = preview.msgbox;
        if (!msgbox)
            return;

        iscan_msgbox_set(msgbox, STRING_TABLE[data->language][STRID_DELETE_THIS_FILEES],
                                   STRING_TABLE[data->language][STRID_YES],
                                   STRING_TABLE[data->language][STRID_NO]);

        lv_obj_clear_flag(preview.msgbox, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(preview.lab_info, LV_OBJ_FLAG_HIDDEN);
    }
}

static void msgbox_key_event(int key)
{
    int param = 0;
    lv_obj_t *msgbox = preview.msgbox;
    struct iscan_config_data *data = lv_obj_get_user_data(preview.main);
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
            lv_obj_clear_flag(preview.lab_info, LV_OBJ_FLAG_HIDDEN);
            break;
        }

        case KEY_REPLY: {
            lv_obj_add_flag(msgbox, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(preview.lab_info, LV_OBJ_FLAG_HIDDEN);
            /*调用回调函数-->删除 图片*/
            int id = iscan_msgbox_get_focused(msgbox);
            if (id == 1) {
                iscan_dec_del(data->dec, preview.current_path);
                preview_select_img(SELECT_PREV);
                preview_update_label();

                if (data->scan_cb->delete_file)
                    data->scan_cb->delete_file(preview.current_path);
            }

            break;
        }
        default:
            return;
    }
}

void iscan_preview_view_key_event(int key, int key_value)
{
    if (key_value != 0)
        return;

    if (key == KEY_POWER) {
        lv_obj_add_flag(preview.msgbox, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *obj = iscan_menu_view_get();
        if (obj)
            lv_scr_load(obj);

        return;
    }

    if (!lv_obj_has_flag(preview.msgbox, LV_OBJ_FLAG_HIDDEN)) {
        msgbox_key_event(key);
        return;
    }

    switch (key) {
        case KEY_SETUP: {
            setup_key_event();
            break;
        }

        case KEY_REPLY: {
            reply_key_event();
            break;
        }

        case KEY_UP: {
            if (preview.zoom_type != ZOOM_NONE)
                preview_pan(PREVIEW_PAN_STEP);
            else {
                preview_select_img(SELECT_PREV);
                preview_update_label();
            }
            break;
        }

        case KEY_DOWN: {
            if (preview.zoom_type != ZOOM_NONE)
                preview_pan(-PREVIEW_PAN_STEP);
            else {
                preview_select_img(SELECT_NEXT);
                preview_update_label();
            }
            break;
        }

        default:
            break;

    }
}

lv_obj_t *iscan_preview_view_get(void)
{
    return preview.main;
}
