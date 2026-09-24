#include <msa.h>
#include "msa_mirror.h"

static const v16i8 one_byte_rev_mask = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
static const v16i8 two_bytes_rev_mask = {14, 15, 12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1};
static const v16i8 four_bytes_rev_mask = {12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3};
static const v16i8 eight_bytes_rev_mask = {8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7};

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
            /* 返回零向量作为后备 */
            return (v16i8)__msa_ldi_b(0);
    }
}

void msa_mirror_h(const uint8_t *src, uint8_t *dst, int width, int height, int src_stride, int dst_stride, int bytes_for_pixel)
{
    const int pixels_per_block = 16 / bytes_for_pixel;
    v16i8 rev_mask = get_rev_mask(bytes_for_pixel);

    for (int y = 0; y < height; y++) {
        const uint8_t *s_row = src + y * src_stride;
        uint8_t *d_row = dst + y * dst_stride;

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

void msa_mirror_v(const uint8_t *src, uint8_t *dst, int width, int height, int src_stride, int dst_stride, int bytes_for_pixel)
{
    /* 垂直镜像不需要 MSA 加速，直接使用 memcpy 即可 */
    for (int y = 0; y < height; y++) {
        const uint8_t *s_row = src + y * src_stride;
        uint8_t *d_row = dst + (height - 1 - y) * dst_stride;
        memcpy(d_row, s_row, width * bytes_for_pixel);
    }
}
