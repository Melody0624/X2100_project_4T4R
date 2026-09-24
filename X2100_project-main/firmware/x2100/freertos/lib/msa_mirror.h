#ifndef __MSA_MIRROR_H__
#define __MSA_MIRROR_H__

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/**
 * @brief 水平镜像(MSA加速处理)
 *
 * @param src 源图像数据的起始地址
 * @param dst 目标图像数据起始地址
 * @param width 源图像数据宽，需为偶数
 * @param height  源图像数据高，需为偶数
 * @param src_stride 源图像数据的行跨度(字节数)
 * @param dst_stride 目标图像数据的行跨度(字节数)
 * @param bytes_for_pixel 每个像素所占字节数,支持1/2/4/8
 */
void msa_mirror_h(const uint8_t *src, uint8_t *dst, int width, int height,
                  int src_stride, int dst_stride, int bytes_for_pixel);

/**
 * @brief 垂直镜像(MSA加速处理)
 *
 * @param src 源图像数据的起始地址
 * @param dst 目标图像数据起始地址
 * @param width 源图像数据宽，需为偶数
 * @param height  源图像数据高，需为偶数
 * @param src_stride 源图像数据的行跨度(字节数)
 * @param dst_stride 目标图像数据的行跨度(字节数)
 * @param bytes_for_pixel 每个像素所占字节数,支持1/2/4/8
 */
void msa_mirror_v(const uint8_t *src, uint8_t *dst, int width, int height,
                  int src_stride, int dst_stride, int bytes_for_pixel);

#endif /* __MSA_MIRROR_H__ */
