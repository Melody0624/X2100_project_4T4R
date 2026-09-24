#include "c_mirror.h"

#define C_ROTATE180_FUNC(name, pixel_type) \
void name(const pixel_type *src, pixel_type *dst, int width, int height, int src_stride, int dst_stride) { \
    for (int y = 0; y < height; y++) { \
        const pixel_type *s_row = (const pixel_type *)((const uint8_t *)src + y * src_stride); \
        pixel_type *d_row = (pixel_type *)((uint8_t *)dst + (height - 1 - y) * dst_stride) + (width - 1); \
        for (int x = 0; x < width; x++) { \
            *d_row-- = *s_row++; \
        } \
    } \
}

C_ROTATE180_FUNC(c_rotate180_u8, uint8_t)
C_ROTATE180_FUNC(c_rotate180_u16, uint16_t)
C_ROTATE180_FUNC(c_rotate180_u32, uint32_t)
C_ROTATE180_FUNC(c_rotate180_u64, uint64_t)


#define C_MIRROR_H_FUNC(name, pixel_type) \
void name(const pixel_type *src, pixel_type *dst, int width, int height, int src_stride, int dst_stride) { \
    for (int y = 0; y < height; y++) { \
        const pixel_type *s_row = (const pixel_type *)((const uint8_t *)src + y * src_stride); \
        pixel_type *d_row = (pixel_type *)((uint8_t *)dst + y * dst_stride) + (width - 1); \
        for (int x = 0; x < width; x++) { \
            *d_row-- = *s_row++; \
        } \
    } \
}

C_MIRROR_H_FUNC(c_mirror_h_u8, uint8_t)
C_MIRROR_H_FUNC(c_mirror_h_u16, uint16_t)
C_MIRROR_H_FUNC(c_mirror_h_u32, uint32_t)
C_MIRROR_H_FUNC(c_mirror_h_u64, uint64_t)


#define C_MIRROR_V_FUNC(name, pixel_type) \
void name(const pixel_type *src, pixel_type *dst, int width, int height, int src_stride, int dst_stride) { \
    for (int y = 0; y < height; y++) { \
        const pixel_type *s_row = (const pixel_type *)((const uint8_t *)src + y * src_stride); \
        pixel_type *d_row = (pixel_type *)((uint8_t *)dst + (height - 1 - y) * dst_stride); \
        memcpy(d_row, s_row, width * sizeof(pixel_type)); \
    } \
}

C_MIRROR_V_FUNC(c_mirror_v_u8, uint8_t)
C_MIRROR_V_FUNC(c_mirror_v_u16, uint16_t)
C_MIRROR_V_FUNC(c_mirror_v_u32, uint32_t)
C_MIRROR_V_FUNC(c_mirror_v_u64, uint64_t)

void c_rotate180(const uint8_t *src, uint8_t *dst, int width, int height, int src_stride, int dst_stride, int bytes_for_pixel)
{
    switch (bytes_for_pixel) {
        case 1:
            c_rotate180_u8(src, dst, width, height, src_stride, dst_stride);
            break;
        case 2:
            c_rotate180_u16((const uint16_t *)src, (uint16_t *)dst, width, height, src_stride, dst_stride);
            break;
        case 4:
            c_rotate180_u32((const uint32_t *)src, (uint32_t *)dst, width, height, src_stride, dst_stride);
            break;
        case 8:
            c_rotate180_u64((const uint64_t *)src, (uint64_t *)dst, width, height, src_stride, dst_stride);
            break;
        default:
            printf("c_rotate180:this bytes per pixel %d is not supported!\n", bytes_for_pixel);
            return;
    }
}

void c_mirror_h(const uint8_t *src, uint8_t *dst, int width, int height, int src_stride, int dst_stride, int bytes_for_pixel)
{
    switch (bytes_for_pixel) {
        case 1:
            c_mirror_h_u8(src, dst, width, height, src_stride, dst_stride);
            break;
        case 2:
            c_mirror_h_u16((const uint16_t *)src, (uint16_t *)dst, width, height, src_stride, dst_stride);
            break;
        case 4:
            c_mirror_h_u32((const uint32_t *)src, (uint32_t *)dst, width, height, src_stride, dst_stride);
            break;
        case 8:
            c_mirror_h_u64((const uint64_t *)src, (uint64_t *)dst, width, height, src_stride, dst_stride);
            break;
        default:
            printf("c_mirror_h:this bytes per pixel %d is not supported!\n", bytes_for_pixel);
            return;
    }
}

void c_mirror_v(const uint8_t *src, uint8_t *dst, int width, int height, int src_stride, int dst_stride, int bytes_for_pixel)
{
    switch (bytes_for_pixel) {
        case 1:
            c_mirror_v_u8(src, dst, width, height, src_stride, dst_stride);
            break;
        case 2:
            c_mirror_v_u16((const uint16_t *)src, (uint16_t *)dst, width, height, src_stride, dst_stride);
            break;
        case 4:
            c_mirror_v_u32((const uint32_t *)src, (uint32_t *)dst, width, height, src_stride, dst_stride);
            break;
        case 8:
            c_mirror_v_u64((const uint64_t *)src, (uint64_t *)dst, width, height, src_stride, dst_stride);
            break;
        default:
            printf("c_mirror_v:this bytes per pixel %d is not supported!\n", bytes_for_pixel);
            return;
    }
}
