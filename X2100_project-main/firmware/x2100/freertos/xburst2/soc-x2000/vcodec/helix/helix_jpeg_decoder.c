#include <types.h>
#include <os/thread_waiter.h>
#include <driver/cache.h>
#include "helix/helix_drv.h"
#include "helix/helix_ops.h"
#include "videodev2.h"

#include <malloc.h>
#include <string.h>

#include "helix/helix_jpeg_decoder.h"

struct helix_jpeg_decoder {
    struct ingenic_venc_ctx *ctx;
    void *buf[3];
    int size[3];
    int width;
    int height;
};

struct helix_jpeg_decoder *helix_jpeg_decoder_init(struct helix_jpeg_decoder_param *param)
{
    ingenic_helix_init();

    struct helix_jpeg_decoder *decoder = malloc(sizeof(*decoder));
    memset(decoder, 0, sizeof(*decoder));

    decoder->ctx = ingenic_helix_ctx_init(V4L2_PIX_FMT_JPEG, 1);

    decoder->width = param->width;
    decoder->height = param->height;

    // printf("size: %dx%d\n", param->width, param->height);

    ingenic_vcodec_helix_set_fmt_out(decoder->ctx, param->width, param->height, V4L2_PIX_FMT_JPEG);
    ingenic_vcodec_helix_set_fmt_cap(decoder->ctx, param->width, param->height, V4L2_PIX_FMT_NV12);

    int ret = jpeg_decoder_alloc_workbuf(&decoder->ctx->jpgd_ctx);
    if (ret) {
        printf("jpeg: failed to alloc workbuf: %d\n", ret);
        goto deinit_ctx;
    }

    return decoder;
deinit_ctx:
    ingenic_helix_ctx_deinit(decoder->ctx);
    free(decoder);
    ingenic_helix_deinit();
    return NULL;
}

static void *check_alloc_mem(struct helix_jpeg_decoder *decoder, int index, int size)
{
    if (decoder->buf[index]) {
        if (decoder->size[index] >= size)
            return decoder->buf[index];
        free(decoder->buf[index]);
    }
    decoder->buf[index] = memalign(256, ALIGN(size, cache_line_size()));
    if (!decoder->buf[index])
        printf("h264: faield alloc tmp buf[%d]: %d\n", index, size);
    decoder->size[index] = size;
    return decoder->buf[index];
}

int helix_jpeg_decoder_decode_nv12_separate(
    struct helix_jpeg_decoder *decoder, void *src_, int src_size, void *y_, void *uv_)
{
	struct ingenic_venc_ctx *ctx = decoder->ctx;
	struct jpgd_ctx *jpgd_ctx = &ctx->jpgd_ctx;

    int width = decoder->width;
    int height = decoder->height;

    void *src = src_;
    void *y = y_;
    void *uv = uv_;

    if ((unsigned long)src % 256) {
        src = check_alloc_mem(decoder, 0, src_size);
        if (!src)
            return -ENOMEM;
        memcpy(src, src_, src_size);
    }
    flush_dcache_force((unsigned long)src, src_size);

    if ((unsigned long)y % 256) {
        y = check_alloc_mem(decoder, 1, width*height);
        if (!y)
            return -ENOMEM;
    }
    invalidate_dcache_force((unsigned long)y, width*height);

    if ((unsigned long)uv % 256) {
        uv = check_alloc_mem(decoder, 2, width*height/2);
        if (!uv)
            return -ENOMEM;
    }
    invalidate_dcache_force((unsigned long)uv, width*height/2);

	struct video_frame_buffer dst_frame;
	struct ingenic_vcodec_mem src_addr;

    src_addr.va = src;
    src_addr.pa = virt_to_phys(src);
    src_addr.size = src_size;

    dst_frame.num_planes = 2;
    dst_frame.fb_addr[0].va = y;
    dst_frame.fb_addr[0].pa = virt_to_phys(y);
    dst_frame.fb_addr[0].size = width*height;
    dst_frame.fb_addr[1].va = uv;
    dst_frame.fb_addr[1].pa = virt_to_phys(uv);
    dst_frame.fb_addr[1].size = width*height/2;

    int ret = jpeg_decoder_decode(jpgd_ctx, &src_addr, &dst_frame);
    if (ret < 0) {
        printf("jpeg decoder: failed to decode: %d\n", ret);
        return ret;
    }

    if (y != y_) {
        memcpy(y_, y, width*height);
        flush_dcache_force((unsigned long)y_, width*height);
    }

    if (uv != uv_) {
        memcpy(uv_, uv, width*height/2);
        flush_dcache_force((unsigned long)uv_, width*height/2);
    }

    return 0;
}

int helix_jpeg_decoder_decode(
    struct helix_jpeg_decoder *decoder, void *src, int src_size, void *nv12)
{
    return helix_jpeg_decoder_decode_nv12_separate(
        decoder, src, src_size, nv12, nv12+decoder->width*decoder->height);
}

void helix_jpeg_decoder_deinit(struct helix_jpeg_decoder *decoder)
{
    jpeg_decoder_free_workbuf(&decoder->ctx->jpgd_ctx);
    ingenic_helix_ctx_deinit(decoder->ctx);
    if (decoder->buf[0]) free(decoder->buf[0]);
    if (decoder->buf[1]) free(decoder->buf[1]);
    if (decoder->buf[2]) free(decoder->buf[2]);
    free(decoder);
    ingenic_helix_deinit();
}
