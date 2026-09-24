#include <os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <include_bin.h>

#include <jpeg-hw.h>

#define WIDTH (128)
#define HEIGHT (72)

// #define YUV_RAWBIN

#if defined(YUV_RAWBIN)
#define WIDTH (1280)
#define HEIGHT (720)
// rawbinData, rawbinSize
INCBIN(rawbin, "example/resource/video-1280x720_nv12.yuv");
#endif

static int dump_framebuffer_hex(unsigned char *frame, int len)
{
    int i;
    unsigned char * buf;
    buf= (unsigned char *) frame;
    for (i=0;i<len;i++) {
        fprintf(stderr, "0x%02x, ", *buf++);
        if ((i&0xf)==0xf)
            fprintf(stderr, "\n");
        msleep(1);
    }
    if ((i&0xf)!=0xf)
        fprintf(stderr, "\n");

    return 0;
}

static int generate_color_pattern_nv12(unsigned char *yuvframe, int width, int height)
{
    int i,j;
    unsigned char * y0;
    unsigned short * uv;
    y0 = (unsigned char *)(yuvframe);
    uv = (unsigned short *)(yuvframe+width*height);
    /* y buffer */
    for (j=0;j<height;j++) {
        for (i=0;i<width;i++) {
            *y0 = (i&0xff);
            y0++;
        }
    }
    /* uv buffer */
    for (j=0;j<height/2;j++) {
        for (i=0;i<width/2;i++) {
            //*uv = (((255-j)&0xff)<<8)|(i&0xff);
            *uv = (((255-i)&0xff)<<8)|(i&0xff);
            uv++;
        }
    }

    return 0;
}

static int generate_color_pattern_yuv422(unsigned char *yuvframe, int width, int height)
{
    int i,j;
    unsigned short * y0u;
    unsigned short * y1v;
    y0u = (unsigned short *)(yuvframe);
    y1v = (unsigned short *)(yuvframe+2);
    for (j=0;j<height;j++) {
        for (i=0;i<width/2;i++) {
            *y0u = (((j)&0xff)<<8)|(i&0xff);
            *y1v = (((255-j)&0xff)<<8)|(i&0xff);
            y0u+=2; y1v+=2;
        }
    }

    return 0;
}

static int generate_color_pattern_yuv422_2(char *yuvframe, int width, int height)
{
    int i,j;
    unsigned short * y0u;
    unsigned short * y1v;
    y0u = (unsigned short *)(yuvframe);
    y1v = (unsigned short *)(yuvframe+2);
    for (j=0;j<height;j++) {
        for (i=0;i<width/2;i++) {
            *y0u = (((i)&0xff)<<8)|(j&0xff);
            *y1v = (((255-i)&0xff)<<8)|(j&0xff);
            y0u+=2; y1v+=2;
        }
    }

    return 0;
}

int test_jpeg(void)
{
    int jpg_bs_len;
    void *jz_jpeg = NULL;
    int width = WIDTH;
    int height = HEIGHT;
    float quality = 1.0;
    unsigned char *yframe;
    unsigned char *uvframe;
    printf("%s() width=%d, height=%d, quality=%d\n", __func__, width, height, quality);

    jz_jpeg = hw_jpeg_encode_init(width, height);

    unsigned char *yuvframe = (unsigned char *)JZMalloc(256, width*height*2);
    if(yuvframe == NULL) {
        printf("Get src yuv buffer failed!\n");
        return -1;
    }
    yframe = yuvframe;
    uvframe = yuvframe + (width*height);
    unsigned char *dst_jpeg_frame = (unsigned char *)JZMalloc(256, width*height*3/2);
    if(dst_jpeg_frame == NULL) {
        printf("Get src yuv buffer failed!\n");
        return -1;
    }
    memset(dst_jpeg_frame, 0, width*height*3/2);
    // dump_framebuffer_hex(dst_jpeg_frame, 124);

#if defined(YUV_RAWBIN)
    memcpy(yuvframe, rawbinData, width*height*2);
#else
    generate_color_pattern_nv12(yuvframe, width, height);
#endif
    printf("convert to jpeg frame:\n");
    jpg_bs_len = hw_yuv420_planar_nv12_to_jpeg_frame(jz_jpeg, (char *)dst_jpeg_frame, yframe, uvframe, width, height, quality);
    // jpg_bs_len = hw_yuv420_planar_nv21_to_jpeg_frame(jz_jpeg, (char *)dst_jpeg_frame, yframe, uvframe, width, height, quality);
    printf("convert to jpeg frame finish. quality=%d, jpg_bs_len=%d\n", quality, jpg_bs_len);
    if (jpg_bs_len>0) {
        dump_framebuffer_hex(dst_jpeg_frame, jpg_bs_len);
    } else {
        printf("err --- jpg_bs_len = %d\n", jpg_bs_len);
    }
    printf("jpeg encode deinit\n");
    hw_jpeg_encode_deinit(jz_jpeg);

    free(yuvframe);
    free(dst_jpeg_frame);

    return 0;
}