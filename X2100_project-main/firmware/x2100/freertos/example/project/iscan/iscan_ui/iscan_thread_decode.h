#ifndef __ISCAN_THREAD_DECODE_H__
#define __ISCAN_THREAD_DECODE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "third_party/lvgl/lvgl/lvgl.h"

typedef struct iscan_thread_decode_handle iscan_dec_t;

typedef enum {
    ISCAN_DEC_HEAD,
    ISCAN_DEC_TAIL,
    ISCAN_DEC_CENTER,
} iscan_dec_center_t;

iscan_dec_t *iscan_dec_create(int rect_w, int rect_h, const char *dir_path, int cache_total);
void iscan_dec_destroy(iscan_dec_t *dec);

int iscan_dec_add(iscan_dec_t *dec, const char *path);
int iscan_dec_del(iscan_dec_t *dec, void *node);
void iscan_dec_clear(iscan_dec_t *dec);

int iscan_dec_set_center(iscan_dec_t *dec, iscan_dec_center_t type);
lv_img_dsc_t *iscan_dec_get_img(iscan_dec_t *dec, void *node);
void *iscan_dec_get_center(iscan_dec_t *dec, int *index_out);
void *iscan_dec_get_next(iscan_dec_t *dec, int *index_out);
void *iscan_dec_get_prev(iscan_dec_t *dec, int *index_out);
int iscan_dec_get_nums(iscan_dec_t *dec);
int iscan_dec_rescan(iscan_dec_t *dec);



#ifdef __cplusplus
}
#endif

#endif
