#include <msa.h>
#include "msa_rotate.h"
#include "msa_rotate_utils.h"

void msa_rotate90(const uint8_t *src, uint8_t *dst, int width, int height, int src_stride, int dst_stride, int bytes_for_pixel)
{
    int col_coef = -1;
    int blk_coef = 1;
    const int pixels_per_block = MSA_MAX_BYTES_SIZE / bytes_for_pixel;

    int align_size = msa_rotate_mirror_four_col(src, dst, width, height, src_stride, dst_stride,
                                                 col_coef, blk_coef, bytes_for_pixel);

    if (((width - align_size) / 1) > 0) {
        const uint8_t *src_p = src + align_size * bytes_for_pixel;
        uint8_t *dst_p = dst + blk_coef * align_size * dst_stride;

        msa_rotate_mirror_one_col(src_p, dst_p, (width - align_size), height, src_stride, dst_stride,
                                  col_coef, blk_coef, bytes_for_pixel);
    }

    if ((height % pixels_per_block) > 0) {
        int h_blk = height / pixels_per_block;
        const uint8_t *src_p = src + h_blk * pixels_per_block * src_stride;
        uint8_t *dst_p = dst + col_coef * h_blk * pixels_per_block * bytes_for_pixel;
        c_rotate_mirror_pixel(src_p, dst_p, width, (height % pixels_per_block), src_stride, dst_stride,
                              col_coef, blk_coef, bytes_for_pixel);
    }
}

void msa_rotate270(const uint8_t *src, uint8_t *dst, int width, int height, int src_stride, int dst_stride, int bytes_for_pixel)
{
    int col_coef = 1;
    int blk_coef = -1;
    const int pixels_per_block = MSA_MAX_BYTES_SIZE / bytes_for_pixel;

    int align_size = msa_rotate_mirror_four_col(src, dst, width, height, src_stride, dst_stride,
                                                 col_coef, blk_coef, bytes_for_pixel);

    if (((width - align_size) / 1) > 0) {
        const uint8_t *src_p = src + align_size * bytes_for_pixel;
        uint8_t *dst_p = dst + blk_coef * align_size * dst_stride;

        msa_rotate_mirror_one_col(src_p, dst_p, (width - align_size), height, src_stride, dst_stride,
                                  col_coef, blk_coef, bytes_for_pixel);
    }

    if ((height % pixels_per_block) > 0) {
        int h_blk = height / pixels_per_block;
        const uint8_t *src_p = src + h_blk * pixels_per_block * src_stride;
        uint8_t *dst_p = dst + col_coef * h_blk * pixels_per_block * bytes_for_pixel;

        c_rotate_mirror_pixel(src_p, dst_p, width, (height % pixels_per_block), src_stride, dst_stride,
                              col_coef, blk_coef, bytes_for_pixel);
    }

}

void msa_rotate180(const uint8_t *src, uint8_t *dst, int width, int height, int src_stride, int dst_stride, int bytes_for_pixel)
{
    const int pixels_per_block = 16 / bytes_for_pixel;
    v16i8 rev_mask = get_rev_mask(bytes_for_pixel);

    for (int y = 0; y < height; y++) {
        const uint8_t *s_row = src + y * src_stride;
        uint8_t *d_row = dst + (height - 1 - y) * dst_stride;

        int x = 0;
        for (; x <= width - pixels_per_block; x += pixels_per_block) {
            const uint8_t *src_pixel = s_row + x * bytes_for_pixel;
            uint8_t *dst_pixel = d_row + (width - x - pixels_per_block) * bytes_for_pixel;

            v16u8 v = (v16u8)__msa_ld_b(src_pixel, 0);
            v16u8 rev = (v16u8)__msa_vshf_b(rev_mask, (v16i8)v, (v16i8)v);
            __msa_st_b((v16i8)rev, dst_pixel, 0);
        }

        for (; x < width; x++) {
            const uint8_t *src_pixel = s_row + x * bytes_for_pixel;
            uint8_t *dst_pixel = d_row + (width - x - 1) * bytes_for_pixel;

            for (int z = 0; z < bytes_for_pixel; z++)
                dst_pixel[z] = src_pixel[z];
        }
    }
}