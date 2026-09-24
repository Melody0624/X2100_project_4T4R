#ifndef __MSA_IMAGES_ROTATE_MIRROR_H__
#define __MSA_IMAGES_ROTATE_MIRROR_H__

#include "msa_mirror.h"
#include "msa_rotate.h"

/**
 * @brief BGRA/RGBA旋转镜像（MSA加速处理）
 *
 * @param src 源图像数据的起始地址
 * @param dst 目标图像数据起始地址
 * @param width 源图像宽，需为偶数
 * @param height 源图像高，需为偶数
 * @param src_stride 源图像数据的行跨度(字节数)
 * @param dst_stride 目标图像数据的行跨度(字节数)
 * @param angle 旋转角度，支持0、90、180、270
 * @param h_mirror 是否需要水平镜像
 * @param v_mirror 是否需要垂直镜像
 */
void msa_bgra_rotate_mirror(const uint8_t *src, uint8_t *dst,
                            int width, int height,
                            int src_stride, int dst_stride,
                            int angle, int h_mirror, int v_mirror);

/**
 * @brief NV12旋转镜像（MSA加速处理）
 *
 * @param src_y 源图像数据Y分量的起始地址
 * @param src_uv 源图像数据UV分量的起始地址
 * @param dst_y 目标图像数据Y分量的起始地址
 * @param dst_uv 目标图像数据UV分量的起始地址
 * @param width 源图像宽，需为偶数
 * @param height 源图像高，需为偶数
 * @param src_y_stride 源图像数据Y分量的行跨度(字节数)
 * @param dst_y_stride 目标图像数据Y分量的行跨度(字节数)
 * @param src_uv_stride 源图像数据UV分量的行跨度(字节数)
 * @param dst_uv_stride 目标图像数据UV分量的行跨度(字节数)
 * @param dst_stride 目标图像数据的行跨度(字节数)
 * @param angle 旋转角度，支持0、90、180、270
 * @param h_mirror 是否需要水平镜像
 * @param v_mirror 是否需要垂直镜像
 */
void msa_nv12_rotate_mirror(const uint8_t *src_y, const uint8_t *src_uv,
                            uint8_t *dst_y, uint8_t *dst_uv,
                            int width, int height,
                            int y_src_stride, int y_dst_stride,
                            int uv_src_stride, int uv_dst_stride,
                            int angle, int h_mirror, int v_mirror);

#endif  /* __MSA_IMAGES_ROTATE_MIRROR_H__ */