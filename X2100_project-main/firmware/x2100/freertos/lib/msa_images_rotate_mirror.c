#include <msa.h>
#include "msa_images_rotate_mirror.h"
#include "msa_rotate_utils.h"

enum mirror_type {
    MIRROR_NONE,
    MIRROR_HORIZONTAL,
    MIRROR_VERTICAL,
    MIRROR_BOTH,

    MIRROR_NUMS,
};

static void (*mirror_func_list[MIRROR_NUMS])(const uint8_t *, uint8_t *, int, int, int, int, int) = {
    [MIRROR_NONE] = NULL,
    [MIRROR_HORIZONTAL] = msa_mirror_h,
    [MIRROR_VERTICAL] = msa_mirror_v,
    [MIRROR_BOTH] = msa_rotate180,
};

static inline int rotate_mirror_get_coef(int angle, int *h_mirror, int *v_mirror, int *col_coef, int *blk_coef)
{
    if (angle % 90 || angle > 270)
        return -1;

    switch (angle) {
        case 90:
            *col_coef = -1;
            *blk_coef = 1;
            break;
        case 270:
            *col_coef = 1;
            *blk_coef = -1;
            break;
        case 0:
        case 180:
            *col_coef = 0;
            *blk_coef = 0;
            break;
    }

    if (*h_mirror)
        *col_coef *= -1;
    if (*v_mirror)
        *blk_coef *= -1;
    if (angle == 180) {
        *h_mirror = !(*h_mirror);
        *v_mirror = !(*v_mirror);
    }

    return 0;
}

void msa_bgra_rotate_mirror(const uint8_t *src, uint8_t *dst, int width, int height,
                            int src_stride, int dst_stride, int angle, int h_mirror, int v_mirror)
{
    int ret;
    const int bytes_for_pixel = 4;
    const int pixels_per_block = MSA_MAX_BYTES_SIZE / bytes_for_pixel;

    int src_w = width;
    int src_h = height;

    int col_coef;
    int blk_coef;
    ret = rotate_mirror_get_coef(angle, &h_mirror, &v_mirror, &col_coef, &blk_coef);
    if (ret < 0) {
        printf("%s: do not support this angle!\n", __func__);
        return;
    }

    if (!col_coef && !blk_coef) {
        const int mirror_flag = (v_mirror << 1) | h_mirror;
        void (*mirror_func)(const uint8_t *, uint8_t *, int, int, int, int, int) = NULL;
        mirror_func = mirror_func_list[mirror_flag];
        if (!mirror_func) {
            printf("%s: do not support this mirror type!\n", __func__);
            return;
        }

        mirror_func(src, dst, src_w, src_h, src_stride, dst_stride, bytes_for_pixel);
        return;
    }

    int align_size = msa_rotate_mirror_four_col(src, dst, src_w, src_h, src_stride,
                                dst_stride, col_coef, blk_coef, bytes_for_pixel);

    if (((src_w - align_size) / 1) > 0) {
        const uint8_t *src_p = src + align_size * bytes_for_pixel;
        uint8_t *dst_p = dst + blk_coef * align_size * dst_stride;

        msa_rotate_mirror_one_col(src_p, dst_p, (src_w - align_size), src_h, src_stride,
                                dst_stride, col_coef, blk_coef, bytes_for_pixel);
    }

    if ((src_h % pixels_per_block) > 0) {
        int h_blk = src_h / pixels_per_block;
        const uint8_t *src_p = src + h_blk * pixels_per_block * src_stride;
        uint8_t *dst_p = dst + col_coef * h_blk * pixels_per_block * bytes_for_pixel;

        c_rotate_mirror_pixel(src_p, dst_p, src_w, (src_h % pixels_per_block), src_stride,
                              dst_stride, col_coef, blk_coef, bytes_for_pixel);
    }
}


void msa_nv12_rotate_mirror(const uint8_t *src_y, const uint8_t *src_uv, uint8_t *dst_y, uint8_t *dst_uv,
                            int width, int height, int y_src_stride, int y_dst_stride,
                            int uv_src_stride, int uv_dst_stride, int angle, int h_mirror, int v_mirror)
{
    int ret;

    int y_src_w = width;
    int y_src_h = height;
    int uv_src_w = width / 2;
    int uv_src_h = height / 2;

    const int y_bytes_for_pixel = 1;
    const int uv_bytes_for_pixel = 2;

    const int y_pixels_per_block = MSA_MAX_BYTES_SIZE / y_bytes_for_pixel;
    const int uv_pixels_per_block = MSA_MAX_BYTES_SIZE / uv_bytes_for_pixel;

    int col_coef;
    int blk_coef;
    ret = rotate_mirror_get_coef(angle, &h_mirror, &v_mirror, &col_coef, &blk_coef);
    if (ret < 0) {
        printf("%s: do not support this angle!\n", __func__);
        return;
    }

    if (!col_coef && !blk_coef) {
        const int mirror_flag = (v_mirror << 1) | h_mirror;
        void (*mirror_func)(const uint8_t *, uint8_t *, int, int, int, int, int) = NULL;
        mirror_func = mirror_func_list[mirror_flag];
        if (!mirror_func) {
            printf("%s: do not support this mirror type!\n", __func__);
            return;
        }

        mirror_func(src_y, dst_y, y_src_w, y_src_h, y_src_stride, y_dst_stride, y_bytes_for_pixel);
        mirror_func(src_uv, dst_uv, uv_src_w, uv_src_h, uv_src_stride, uv_dst_stride, uv_bytes_for_pixel);
        return;
    }


    /****************************** Y *********************************/
    int y_align_size = msa_rotate_mirror_four_col(src_y, dst_y, y_src_w, y_src_h, y_src_stride,
                                y_dst_stride, col_coef, blk_coef, y_bytes_for_pixel);

    if (((y_src_w - y_align_size) / 1) > 0) {
        const uint8_t *y_src_p = src_y + y_align_size * y_bytes_for_pixel;
        uint8_t *y_dst_p = dst_y + blk_coef * y_align_size * y_dst_stride;

        msa_rotate_mirror_one_col(y_src_p, y_dst_p, (width - y_align_size), y_src_h, y_src_stride,
                                y_dst_stride, col_coef, blk_coef, y_bytes_for_pixel);
    }

    if ((y_src_h % y_pixels_per_block) > 0) {
        int y_h_blk = y_src_h / y_pixels_per_block;
        const uint8_t *y_src_p = src_y + y_h_blk * y_pixels_per_block * y_src_stride;
        uint8_t *y_dst_p = dst_y + col_coef * y_h_blk * y_pixels_per_block * y_bytes_for_pixel;

        c_rotate_mirror_pixel(y_src_p, y_dst_p, y_src_w, (y_src_h % y_pixels_per_block),
                              y_src_stride, y_dst_stride, col_coef, blk_coef, y_bytes_for_pixel);
    }
    /****************************** UV *********************************/
    int uv_align_size = msa_rotate_mirror_four_col(src_uv, dst_uv, uv_src_w, uv_src_h,
                            uv_src_stride, uv_dst_stride, col_coef, blk_coef, uv_bytes_for_pixel);

    if (((uv_src_w - uv_align_size) / 1) > 0) {
        const uint8_t *uv_src_p = src_uv + uv_align_size * uv_bytes_for_pixel;
        uint8_t *uv_dst_p = dst_uv + blk_coef * uv_align_size * uv_dst_stride;

        msa_rotate_mirror_one_col(uv_src_p, uv_dst_p, (uv_src_w - uv_align_size), uv_src_h,
                                  uv_src_stride, uv_dst_stride, col_coef, blk_coef, uv_bytes_for_pixel);
    }

    if ((uv_src_h % uv_pixels_per_block) > 0) {
        int uv_h_blk = uv_src_h / uv_pixels_per_block;
        const uint8_t *uv_src_p = src_uv + uv_h_blk * uv_pixels_per_block * uv_src_stride;
        uint8_t *uv_dst_p = dst_uv + col_coef * uv_h_blk * uv_pixels_per_block * uv_bytes_for_pixel;

        c_rotate_mirror_pixel(uv_src_p, uv_dst_p, uv_src_w, (uv_src_h % uv_pixels_per_block),
                              uv_src_stride, uv_dst_stride, col_coef, blk_coef, uv_bytes_for_pixel);
    }
}