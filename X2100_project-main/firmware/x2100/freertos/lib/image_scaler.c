#include "image_scaler.h"

int image_scaling_simple(void *src, void *dst,
                          double zoom_w, double zoom_h,
                          uint32_t dst_w, uint32_t dst_h,
                          uint32_t unit_size, uint32_t line_size)
{
    if (!zoom_w || !zoom_h)
        return -EINVAL;

    uint32_t src_line = dst_w * unit_size / zoom_w;
    uint32_t dst_line = line_size;
    uint8_t *src_row, *dst_row;
    uint32_t dst_x, dst_y, src_x, src_y;

    for (dst_y = 0; dst_y < dst_h; ++dst_y) {
        src_y = dst_y / zoom_h;
        src_row = src + src_y * src_line;
        dst_row = dst + dst_y * dst_line;

        for (dst_x = 0; dst_x < dst_w; ++dst_x) {
            src_x = dst_x / zoom_w;
            memcpy(dst_row + dst_x * unit_size,
                   src_row + src_x * unit_size,
                   unit_size);
        }
    }

    return 0;
}
