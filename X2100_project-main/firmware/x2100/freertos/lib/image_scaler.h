#include <common.h>

/**
 * @brief 图像缩放
 *
 * @param src 源图像数据的起始地址
 * @param dst 目标图像数据起始地址
 * @param zoom_w 宽缩放比例
 * @param zoom_h 高缩放比例
 * @param dst_w 目标图像数据宽
 * @param dst_h 目标图像数据高
 * @param unit_size 每个像素字节数
 * @param line_size 目标图像数据行字节数
 * @return int 0 成功; <0 失败
 */
int image_scaling_simple(void *src, void *dst,
                          double zoom_w, double zoom_h,
                          uint32_t dst_w, uint32_t dst_h,
                          uint32_t unit_size, uint32_t line_size);