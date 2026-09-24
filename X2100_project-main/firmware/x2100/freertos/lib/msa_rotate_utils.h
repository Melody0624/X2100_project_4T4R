#include <msa.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define MSA_MAX_BYTES_SIZE  16

static const v16i8 one_byte_rev_mask = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
static const v16i8 two_bytes_rev_mask = {14, 15, 12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1};
static const v16i8 four_bytes_rev_mask = {12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3};
static const v16i8 eight_bytes_rev_mask = {8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7};

#define ZERO_VEC __msa_ldi_b(0)

static inline v16i8 get_rev_mask(int bytes_for_pixel) {
    switch (bytes_for_pixel)
    {
        case 1:
            return one_byte_rev_mask;
        case 2:
            return two_bytes_rev_mask;
        case 4:
            return four_bytes_rev_mask;
        case 8:
            return eight_bytes_rev_mask;
        default:
            printf("this bytes per pixel %d is not supported!\n", bytes_for_pixel);
            return ZERO_VEC;
    }
}

#define one_byte_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, col_coef, blk_coef, num)\
do {\
    const uint8_t *src_p##num = src_p + num * bytes_for_pixel;\
    uint8_t col##num[16] __attribute__((aligned(16)));\
    col##num[0] = *(src_p##num + 0 * src_stride);\
    col##num[1] = *(src_p##num + 1 * src_stride);\
    col##num[2] = *(src_p##num + 2 * src_stride);\
    col##num[3] = *(src_p##num + 3 * src_stride);\
    col##num[4] = *(src_p##num + 4 * src_stride);\
    col##num[5] = *(src_p##num + 5 * src_stride);\
    col##num[6] = *(src_p##num + 6 * src_stride);\
    col##num[7] = *(src_p##num + 7 * src_stride);\
    col##num[8] = *(src_p##num + 8 * src_stride);\
    col##num[9] = *(src_p##num + 9 * src_stride);\
    col##num[10] = *(src_p ##num + 10 * src_stride);\
    col##num[11] = *(src_p ##num + 11 * src_stride);\
    col##num[12] = *(src_p ##num + 12 * src_stride);\
    col##num[13] = *(src_p ##num + 13 * src_stride);\
    col##num[14] = *(src_p ##num + 14 * src_stride);\
    col##num[15] = *(src_p ##num + 15 * src_stride);\
    v16u8 pixel_data##num = (v16u8)__msa_ld_b((v16i8 *)col##num, 0);\
    if (blk_coef < 0){\
        pixel_data##num = (v16u8)__msa_vshf_b(rev_mask, (v16i8)pixel_data##num, (v16i8)pixel_data##num);\
    }\
    uint8_t *dst_p##num = dst_p + col_coef * num * dst_stride;\
    __msa_st_b((v16i8)pixel_data##num, (v16i8 *)dst_p##num, 0);\
} while(0)

#define two_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, col_coef, blk_coef, num)\
do {\
    const uint8_t *src_p##num = src_p + num * bytes_for_pixel;\
    uint16_t col##num[8] __attribute__((aligned(16)));\
    col##num[0] = *(const uint16_t *)(src_p##num + 0 * src_stride);\
    col##num[1] = *(const uint16_t *)(src_p##num + 1 * src_stride);\
    col##num[2] = *(const uint16_t *)(src_p##num + 2 * src_stride);\
    col##num[3] = *(const uint16_t *)(src_p##num + 3 * src_stride);\
    col##num[4] = *(const uint16_t *)(src_p##num + 4 * src_stride);\
    col##num[5] = *(const uint16_t *)(src_p##num + 5 * src_stride);\
    col##num[6] = *(const uint16_t *)(src_p##num + 6 * src_stride);\
    col##num[7] = *(const uint16_t *)(src_p##num + 7 * src_stride);\
    v16u8 pixel_data##num = (v16u8)__msa_ld_b((v16i8 *)col##num, 0);\
    if (blk_coef < 0){\
        pixel_data##num = (v16u8)__msa_vshf_b(rev_mask, (v16i8)pixel_data##num, (v16i8)pixel_data##num);\
    }\
    uint8_t *dst_p##num = dst_p + col_coef * num * dst_stride;\
    __msa_st_b((v16i8)pixel_data##num, (v16i8 *)dst_p##num, 0);\
}while(0)

#define four_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, col_coef, blk_coef, num)\
do {\
    const uint8_t *src_p##num = src_p + num * bytes_for_pixel;\
    uint32_t col##num[4] __attribute__((aligned(16)));\
    col##num[0] = *(const uint32_t *)(src_p##num + 0 * src_stride);\
    col##num[1] = *(const uint32_t *)(src_p##num + 1 * src_stride);\
    col##num[2] = *(const uint32_t *)(src_p##num + 2 * src_stride);\
    col##num[3] = *(const uint32_t *)(src_p##num + 3 * src_stride);\
    v16u8 pixel_data##num = (v16u8)__msa_ld_b((v16i8 *)col##num, 0);\
    if (blk_coef < 0){\
        pixel_data##num = (v16u8)__msa_vshf_b(rev_mask, (v16i8)pixel_data##num, (v16i8)pixel_data##num);\
    }\
    uint8_t *dst_p##num = dst_p + col_coef * num * dst_stride;\
    __msa_st_b((v16i8)pixel_data##num, (v16i8 *)dst_p##num, 0);\
} while (0)

#define eight_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, col_coef, blk_coef, num)\
do {\
    const uint8_t *src_p##num = src_p + num * bytes_for_pixel;\
    uint64_t col##num[2] __attribute__((aligned(16)));\
    col##num[0] = *(const uint64_t *)(src_p##num + 0 * src_stride);\
    col##num[1] = *(const uint64_t *)(src_p##num + 1 * src_stride);\
    v16u8 pixel_data##num = (v16u8)__msa_ld_b((v16i8 *)col##num, 0);\
    if (blk_coef < 0){\
        pixel_data##num = (v16u8)__msa_vshf_b(rev_mask, (v16i8)pixel_data##num, (v16i8)pixel_data##num);\
    }\
    uint8_t *dst_p##num = dst_p + col_coef * num * dst_stride;\
    __msa_st_b((v16i8)pixel_data##num, (v16i8 *)dst_p##num, 0);\
} while(0)

static void c_rotate_mirror_pixel(const uint8_t *src, uint8_t *dst,
                                int width, int height,
                                int src_stride, int dst_stride,
                                int col_coef, int blk_coef,
                                int bytes_for_pixel)
{
    const uint8_t * src_row, *src_p;
    uint8_t *dst_row, *dst_p;

    int src_col = src_stride;
    int dst_col = col_coef * bytes_for_pixel;

    int src_blk = bytes_for_pixel;
    int dst_blk = blk_coef * dst_stride;

    for (int i = 0; i < height; i++) {
        src_row = src + (i * src_col);
        dst_row = dst + (i * dst_col);

        for (int j = 0; j < width; j++) {
            src_p = src_row + (j * src_blk);
            dst_p = dst_row + (j * dst_blk);

            for (int b = 0; b < bytes_for_pixel; b++) {
                dst_p[b] = src_p[b];
            }
        }
    }
}

static int msa_rotate_mirror_four_col(const uint8_t *src, uint8_t *dst,
                                 int width, int height,
                                 int src_stride, int dst_stride,
                                 int col_coef, int blk_coef,
                                 int bytes_for_pixel)
{
    const int pixels_per_block = MSA_MAX_BYTES_SIZE / bytes_for_pixel;
    v16i8 rev_mask = get_rev_mask(bytes_for_pixel);

    int h_blk_num = height / pixels_per_block;
    dst += ((col_coef > 0) ? 0 : ((h_blk_num - 1) * pixels_per_block * bytes_for_pixel));

    int w_blk_num = width / 4;
    dst += ((blk_coef > 0) ? 0 : (w_blk_num * dst_stride * 4));

    int src_col = pixels_per_block * src_stride;
    int dst_col = col_coef * pixels_per_block * bytes_for_pixel;

    int src_blk = bytes_for_pixel * 4;
    int dst_blk = blk_coef * dst_stride * 4;

    const uint8_t *src_row, *src_p;
    uint8_t *dst_row, *dst_p;

    for (int i = 0; i < h_blk_num; i++) {
        src_row = src + (i * src_col);
        dst_row = dst + (i * dst_col);

        for (int j = 0; j < w_blk_num; j++) {
            src_p = src_row + (j * src_blk);
            dst_p = dst_row + (j * dst_blk);

            if (bytes_for_pixel == 1) {
                one_byte_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 0);
                one_byte_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 1);
                one_byte_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 2);
                one_byte_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 3);
            } else if (bytes_for_pixel == 2) {
                two_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 0);
                two_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 1);
                two_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 2);
                two_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 3);
            } else if (bytes_for_pixel == 4) {
                four_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 0);
                four_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 1);
                four_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 2);
                four_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 3);
            } else if (bytes_for_pixel == 8){
                eight_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 0);
                eight_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 1);
                eight_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 2);
                eight_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 3);
            }
        }
    }

    return (w_blk_num * 4);
}

static void msa_rotate_mirror_one_col(const uint8_t *src, uint8_t *dst,
                                int width, int height,
                                 int src_stride, int dst_stride,
                                 int col_coef, int blk_coef,
                                 int bytes_for_pixel)
{
    const int pixels_per_block = MSA_MAX_BYTES_SIZE / bytes_for_pixel;
    v16i8 rev_mask = get_rev_mask(bytes_for_pixel);

    int src_col = pixels_per_block * src_stride;
    int dst_col = col_coef * pixels_per_block * bytes_for_pixel;

    int src_blk = bytes_for_pixel;
    int dst_blk = blk_coef * dst_stride;

    int h_blk_num = height / pixels_per_block;

    const uint8_t *src_row, *src_p;
    uint8_t *dst_row, *dst_p;

    for (int i = 0; i < h_blk_num; i++) {
        src_row = src + (i * src_col);
        dst_row = dst + (i * dst_col);

        for (int j = 0; j < width; j++) {
            src_p = src_row + (j * src_blk);
            dst_p = dst_row + (j * dst_blk);

            if (bytes_for_pixel == 1)
                one_byte_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 0);
            else if (bytes_for_pixel == 2)
                two_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 0);
            else if (bytes_for_pixel == 4)
                four_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 0);
            else if (bytes_for_pixel == 8)
                eight_bytes_col_rotate_mirror(src_p, dst_p, bytes_for_pixel, src_stride, dst_stride, blk_coef, col_coef, 0);
        }
    }
}