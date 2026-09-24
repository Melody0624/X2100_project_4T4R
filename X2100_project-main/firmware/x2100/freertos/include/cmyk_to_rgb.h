#ifndef __CMYK_TO_RGB__
#define __CMYK_TO_RGB__

/* cmyk 转24位 rgb, 一个像素占3个字节*/
void cmyk_to_rgb24(unsigned int *cmyk, unsigned char *rgb, int cmyk_line_length, int rgb_line_length, int width, int height);

/* cmyk 转32位 rgb, 一个像素占4个字节 */
void cmyk_to_rgb32(unsigned int *cmyk, unsigned int *rgb, int cmyk_line_length, int rgb_line_length, int width, int height);

#endif