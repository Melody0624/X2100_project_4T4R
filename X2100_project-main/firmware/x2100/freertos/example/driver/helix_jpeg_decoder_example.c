

#include <stdio.h>
#include <printf.h>
#include <common.h>
#include <os.h>
#include <driver/cache.h>

#include <stdlib.h>
#include <malloc.h>
#include <include_bin.h>

#include <helix/helix_jpeg_decoder.h>

INCBIN(jpeg, "example/resource/test.jpeg");

void helix_jpeg_decoder_test(void)
{
    int jpeg_width = 658, jpeg_height = 411;
    int width = ALIGN(jpeg_width, 16);
    int height = ALIGN(jpeg_height, 16);

    struct helix_jpeg_decoder_param param = {
        .width = width,
        .height = height,
    };
    struct helix_jpeg_decoder *decoder = helix_jpeg_decoder_init(&param);
    if (!decoder)
        return;

    void *nv12 = memalign(256, ALIGN(width*height*3/2, cache_line_size()));

    helix_jpeg_decoder_decode(decoder, (void *)jpegData, jpegSize, nv12);

    int w = 400, h = 400;
    void *y = memalign(256, w*h);
    unsigned char *s = nv12 + (width-w)/2;
    unsigned char *d = y;
    int i, j;
    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            d[j] = s[j];
        }
        d += w;
        s += width;
    }

    printf_disable_time_stamp();
    dump_mem32_c_style(y, w*h, 8);

    free(y);
    free(nv12);
    helix_jpeg_decoder_deinit(decoder);
}
