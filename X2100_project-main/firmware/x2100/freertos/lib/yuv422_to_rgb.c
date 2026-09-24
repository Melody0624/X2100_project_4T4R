#include <yuv422_to_rgb.h>

#define range_limit(x) ((x) > 255 ? 255 : ((x) < 0 ? 0 : (x)))
#define next_line(addr, len) ((void *)(addr) + (len))

/*计算公式*/
// R = Y + 1.402*(V-128)
// G = Y - 0.34414*(U-128)- 0.71414*(V-128);
// B = Y + 1.772*(U-128)
// 根据计算公式，将除法转换为移位计算

static inline unsigned int yuv_to_r(int y, int v)
{
    int r;

    v = v - 128;
    r = y + v + ((v*103) >> 8);
    r = range_limit(r);

    return r;
}

static inline unsigned int yuv_to_g(int y, int u, int v)
{
    int g;

    u = u - 128;
    v = v - 128;
    g = y + ((u*88) >> 8) - ((v*183) >> 8);
    g = range_limit(g);

    return g;
}

static inline unsigned int yuv_to_b(int y, int u)
{
    int b;

    u = u - 128;
    b = y + u + ((u*198) >> 8);
    b = range_limit(b);

    return b;
}

static inline unsigned int make_argb888(unsigned char r, unsigned char g, unsigned char b)
{
    unsigned int rgb = (0xff << 24) | ((int)r << 16) | ((int)g << 8) | (int)b;
    return rgb;
}

static inline unsigned short make_rgb565(unsigned char r, unsigned char g, unsigned char b)
{
    unsigned short rgb = (((int)r & 0xf8) << 8) | (((int)g & 0xfc) << 3) | (((int)b & 0xf8) >> 3);
    return rgb;
}

static inline unsigned short yuv_rgb_565(int y, int u, int v)
{
    unsigned int r = yuv_to_r(y, v);
    unsigned int g = yuv_to_g(y, u, v);
    unsigned int b = yuv_to_b(y ,u);

    return make_rgb565(r, g, b);
}

static inline unsigned int yuv_argb_888(int y, int u, int v)
{
    unsigned int r = yuv_to_r(y, v);
    unsigned int g = yuv_to_g(y, u, v);
    unsigned int b = yuv_to_b(y ,u);

    return make_argb888(r, g, b);
}

void yuv422_to_rgb565(void *yuv_buf, int yuv_line_len, void *rgb_buf, int rgb_line_len, int w, int h)
{
    unsigned char *yuv = yuv_buf;
    unsigned short *rgb = rgb_buf;
    int i, j;

    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i += 2) {
            unsigned int y0 = yuv[i*2 + 0];
            unsigned int u = yuv[i*2 + 1];
            unsigned int y1 = yuv[i*2 + 2];
            unsigned int v = yuv[i*2 + 3];

            rgb[i + 0] = yuv_rgb_565(y0, u, v);
            rgb[i + 1] = yuv_rgb_565(y1, u, v);
        }

        yuv = next_line(yuv, yuv_line_len);
        rgb = next_line(rgb, rgb_line_len);
    }
}

void yuv422_to_argb888(void *yuv_buf, int yuv_line_len, void *rgb_buf, int rgb_line_len, int w, int h)
{
    unsigned char * yuv = yuv_buf;
    unsigned int * rgb = rgb_buf;
    int i, j;

    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i += 2) {
            unsigned int y0 = yuv[i*2 + 0];
            unsigned int u = yuv[i*2 + 1];
            unsigned int y1 = yuv[i*2 + 2];
            unsigned int v = yuv[i*2 + 3];

            rgb[i + 0] = yuv_argb_888(y0, u, v);
            rgb[i + 1] = yuv_argb_888(y1, u, v);
        }
        yuv = next_line(yuv, yuv_line_len);
        rgb = next_line(rgb, rgb_line_len);
    }
}

int yuv422_to_rgb(void *from_yuv, void *to_rgb, int fb_xres, int fb_yres, enum fb_fmt fb_data_fmt,
                 int fb_bytes_per_line, int yuv_width, int yuv_height)
{
    int w0 = fb_xres;
    int h0 = fb_yres;

    int w1 = yuv_width;
    int h1 = yuv_height;

    /* 计算最终待转换计算的图像尺寸大小
    *  即 屏幕、图像中心重合，相重合的区域大小
    */
    int w = min(w0, w1);
    int h = min(h0, h1);

    int yuv_line_len =  yuv_width * 2;
    int rgb_line_len =  fb_bytes_per_line;

    void *yuv = from_yuv;
    void *rgb = to_rgb;

    /* 根据转换计算的图像尺寸，图像、fb地址做对应偏移
    */
    if (h0 > h)
        rgb += rgb_line_len * (h0 - h) / 2;

    if (h1 > h)
        yuv += yuv_line_len * (h1 - h) / 2;

    if (w0 > w)
        rgb += fb_bytes_per_pixel(fb_data_fmt) * (w0 - w) / 2;

    if (w1 > w)
        yuv += (w1 - w);

    if (fb_data_fmt != fb_fmt_RGB565 && fb_data_fmt != fb_fmt_RGB565)
    {
        printf("not support this fb format.\n");
        return -1;
    }

    if (fb_data_fmt == fb_fmt_RGB565)
        yuv422_to_rgb565(yuv, yuv_line_len, rgb, rgb_line_len, w, h);

    if (fb_data_fmt == fb_fmt_ARGB8888)
        yuv422_to_argb888(yuv, yuv_line_len, rgb, rgb_line_len, w, h);

    return 0;
}
