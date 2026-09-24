#ifndef __NV12_ROTATE__
#define __NV12_ROTATE__

/**
 *  需要保证高是64对齐，否则出来的图片uv末端会有异常
 *  即: 向右旋转1920*1080 nv12图片时，最后出来的图片可用范围:宽 = 1080-(1080%64) = 1024, 高 = 1920-(1920%32) = 1920
 * */

void nv12_left_rotate_90(unsigned char* nv12_data, unsigned char* rot90_nv12_data, int src_width, int src_height);

void nv12_right_rotate_90(unsigned char* nv12_data, unsigned char* rot90_nv12_data, int src_width, int src_height);

#endif // __NV12_ROTATE__
