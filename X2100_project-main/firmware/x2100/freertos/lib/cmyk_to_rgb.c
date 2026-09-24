#include <cmyk_to_rgb.h>

/* cmyk 转24位 rgb, 一个像素占3个字节*/
void cmyk_to_rgb24(unsigned int *cmyk, unsigned char *rgb, int cmyk_line_length, int rgb_line_length, int width, int height)
{
    int i, j;
    unsigned char *p_rgb;
    unsigned int *p_cmyk;

    for (i = 0; i < height; i++) {

        p_rgb = rgb;
        p_cmyk = cmyk;
        for (j = 0; j < width; j++) {

            unsigned char c = *p_cmyk & 0x000000FF;
            unsigned char m = (*p_cmyk >> 8) & 0x000000FF;
            unsigned char y = (*p_cmyk >> 16) & 0x000000FF;
            unsigned char k = (*p_cmyk >> 24) & 0x000000FF;

            *p_rgb++ = k * c/255;//RGB
            *p_rgb++ = k * m/255;
            *p_rgb++ = k * y/255;

            p_cmyk++;
        }

        cmyk = (void *)cmyk + cmyk_line_length;
        rgb += rgb_line_length;
    }
}

/* cmyk 转32位 rgb, 一个像素占4个字节 */
void cmyk_to_rgb32(unsigned int *cmyk, unsigned int *rgb, int cmyk_line_length, int rgb_line_length, int width, int height)
{
    int i,j;
    unsigned char *p_rgb;
    unsigned int *p_cmyk;

    for (i = 0; i < height; i++) {
        p_rgb = (unsigned char *)rgb;
        p_cmyk = cmyk;
        for(j = 0; j < width; j++) {

            unsigned char c = *p_cmyk & 0x000000FF;
            unsigned char m = (*p_cmyk >> 8) & 0x000000FF;
            unsigned char y = (*p_cmyk >> 16) & 0x000000FF;
            unsigned char k = (*p_cmyk >> 24) & 0x000000FF;

            *p_rgb++ = (unsigned int)k * y/255;//BGRA
            *p_rgb++ = (unsigned int)k * m/255;
            *p_rgb++ = (unsigned int)k * c/255;
            *p_rgb++ = 0xff;

            p_cmyk++;
        }

        cmyk = (void *)cmyk + cmyk_line_length;
        rgb = (void *)rgb + rgb_line_length;
    }
}