#ifndef __MSA_ROTATE_H__
#define __MSA_ROTATE_H__

#include <stdio.h>
#include <string.h>
#include <stdint.h>


/**
 * @brief 顺时针旋转90度（MSA加速处理）
 *
 * @param src 源数据的起始地址
 * @param dst 目标数据起始地址
 * @param width 源数据宽，需为偶数
 * @param height 源数据高，需为偶数
 * @param src_stride 源数据的行跨度(字节数)
 * @param dst_stride 目标数据的行跨度(字节数)
 * @param bytes_for_pixel 每个数据单位所占字节数,支持1/2/4/8
 */
void msa_rotate90(const uint8_t *src, uint8_t *dst, int width, int height, int src_stride, int dst_stride, int bytes_for_pixel);

/**
 * @brief 旋转180度(MSA加速处理)
 *
 * @param src 源数据的起始地址
 * @param dst 目标数据起始地址
 * @param width 源数据宽，需为偶数
 * @param height  源数据高，需为偶数
 * @param src_stride 源数据的行跨度(字节数)
 * @param dst_stride 目标数据的行跨度(字节数)
 * @param bytes_for_pixel 每个数据单位所占字节数,支持1/2/4/8
 */
void msa_rotate180(const uint8_t *src, uint8_t *dst, int width, int height, int src_stride, int dst_stride, int bytes_for_pixel);

/**
 * @brief 顺时针旋转270度（MSA加速处理）
 *
 * @param src 源数据的起始地址
 * @param dst 目标数据起始地址
 * @param width 源数据宽，需为偶数
 * @param height 源数据高，需为偶数
 * @param src_stride 源数据的行跨度(字节数)
 * @param dst_stride 目标数据的行跨度(字节数)
 * @param bytes_for_pixel 每个数据单位所占字节数,支持1/2/4/8
 */
void msa_rotate270(const uint8_t *src, uint8_t *dst, int width, int height, int src_stride, int dst_stride, int bytes_for_pixel);

#endif /* __MSA_ROTATE_H__ */
