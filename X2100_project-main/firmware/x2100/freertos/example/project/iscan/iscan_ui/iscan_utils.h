#ifndef __ISCAN_UTILS_H__
#define __ISCAN_UTILS_H__

#include "third_party/lvgl/lvgl/lvgl.h"



int ilv_file_list_init(lv_ll_t *head, const char *dir_path, char *extension[], int check_num, int user_size);
int ilv_file_list_init_recursion(lv_ll_t *head, const char *dir_path, char *extension[], int check_num, int user_size);

void *ilv_file_list_get_user_data(lv_ll_t *head, void *node);

lv_style_t *ilv_load_font(const char *path, int height);
void ilv_del_font(lv_style_t *style);

int ilv_jpeg_decode(char *file, lv_img_dsc_t *img_data, int rect_w, int rect_h);

int ilv_jpeg_to_pdf(void *jpeg_data, int jpeg_size, int width, int height, const char *pdf_path);

#endif