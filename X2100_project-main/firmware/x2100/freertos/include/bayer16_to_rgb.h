#ifndef BAYER16_TO_RGB_H
#define BAYER16_TO_RGB_H

#include <stdio.h>
#include <driver/fb.h>
#include <driver/camera.h>
#include <common.h>
#include <os.h>

/**
 * @brief bayer16bit格式的图像数据转换成rgb格式，转换后的图像显示在fb设备上
 *        图像大小根据yuv、fb尺寸有适当裁剪
 * @param from_bayer  bayer图像数据的起始地址
 * @param to_rgb  转换成功后，rgb图像数据的存储地址
 * @param fb_xres  fb设备的宽度
 * @param fb_yres  fb设备的高度
 * @param fb_data_fmt   fb设备需要的数据格式
 * @param fb_bytes_per_line  fb设备每行数据的字节数量
 * @param bayer_width  bayer图像数据的宽度
 * @param bayer_height  bayer图像数据的高度
 * @param data_fmt  bayer数据的格式
 * @return 0 表示成功, -1 表示失败
 */
int bayer16_to_rgb(void *from_bayer, void *to_rgb, int fb_xres, int fb_yres, enum fb_fmt fb_data_fmt,
                int fb_bytes_per_line, int bayer_width, int bayer_height, camera_pixel_fmt data_fmt);

#endif