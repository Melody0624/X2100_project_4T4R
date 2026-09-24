#ifndef __JPEG_HW_H__
#define __JPEG_HW_H__

#include <stdint.h>

enum helix_raw_format {
	HELIX_TILE_MODE = 0,
	HELIX_420P_MODE = 4,
	HELIX_NV12_MODE = 8,
	HELIX_NV21_MODE = 12,
};

/* JPEG encode quantization table select level */

void* hw_jpeg_encode_init(int width,int height);
void hw_jpeg_encode_deinit(void *handle);

/*quality 0.0 ～ 100.0 可调，正常在10.0以下， 越高压缩率越高，图片质量越低*/

int hw_yuv420_planar_nv12_to_jpeg_frame(void *handle, unsigned char *dst_jpeg_frame,
                                        unsigned char *ybuf, unsigned char *uvbuf,
                                        int width, int height,  float quality);


int hw_yuv420_planar_nv21_to_jpeg_frame(void *handle, unsigned char *dst_jpeg_frame,
                                        unsigned char *ybuf, unsigned char *uvbuf,
                                        int width, int height,  float quality);

void *JZMalloc(int align, int size);
unsigned int get_phy_addr(unsigned int vaddr);

#endif    /* __JPEG_HW_H__ */
