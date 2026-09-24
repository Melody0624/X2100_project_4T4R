#include <types.h>
#include <os/thread_waiter.h>
#include <driver/cache.h>
#include "helix/h264_encoder/h264e_rc.h"
#include "helix/helix_drv.h"
#include "helix/helix_ops.h"
#include "videodev2.h"

#include <malloc.h>
#include <string.h>

#include "helix/helix_jpeg_encoder.h"

struct helix_jpeg_encoder {
    struct ingenic_venc_ctx *ctx;
    void *buf[3];
    int size[3];
    int width;
    int height;
};

struct helix_jpeg_encoder *helix_jpeg_encoder_init(struct helix_jpeg_encoder_param *param)
{
    ingenic_helix_init();

    struct helix_jpeg_encoder *encoder = malloc(sizeof(*encoder));
    memset(encoder, 0, sizeof(*encoder));

    encoder->ctx = ingenic_helix_ctx_init(V4L2_PIX_FMT_JPEG, 0);

    encoder->width = param->width;
    encoder->height = param->height;

    int quality = param->compress_quality;

    /* Safety limit on quality factor.  Convert 0 to 1 to avoid zero divide. */
    if (quality <= 0) quality = 1;
    if (quality > 100) quality = 100;

    /* The basic table is used as-is (scaling 100) for a quality of 50.
    * Qualities 50..100 are converted to scaling percentage 200 - 2*Q;
    * note that at Q=100 the scaling is 0, which will cause jpeg_add_quant_table
    * to make all the table entries 1 (hence, minimum quantization loss).
    * Qualities 1..50 are converted to scaling percentage 5000/Q.
    */
    if (quality < 50)
        quality = 5000 / quality;
    else
        quality = 200 - quality * 2;

    ingenic_vcodec_helix_set_fmt_out(encoder->ctx, param->width, param->height, V4L2_PIX_FMT_NV12);
    ingenic_vcodec_helix_set_fmt_cap(encoder->ctx, param->width, param->height, V4L2_PIX_FMT_JPEG);
    ingenic_vcodec_helix_set_param(encoder->ctx, V4L2_CID_JPEG_COMPRESSION_QUALITY, quality);

    int ret = jpeg_encoder_alloc_workbuf(&encoder->ctx->jpge_ctx);
    if (ret) {
        printf("h264: failed to alloc workbuf: %d\n", ret);
        goto deinit_ctx;
    }

    return encoder;
deinit_ctx:
    ingenic_helix_ctx_deinit(encoder->ctx);
    free(encoder);
    ingenic_helix_deinit();
    return NULL;
}

static void *check_alloc_mem(struct helix_jpeg_encoder *encoder, int index, int size)
{
    if (encoder->buf[index]) {
        if (encoder->size[index] >= size)
            return encoder->buf[index];
        free(encoder->buf[index]);
    }
    encoder->buf[index] = memalign(256, ALIGN(size, cache_line_size()));
    if (!encoder->buf[index])
        printf("h264: faield alloc tmp buf[%d]: %d\n", index, size);
    encoder->size[index] = size;
    return encoder->buf[index];
}

int helix_jpeg_encoder_encode_nv12_separate(
    struct helix_jpeg_encoder *encoder, void *y_, void *uv_, void *dst_, int dst_size)
{
	struct ingenic_venc_ctx *ctx = encoder->ctx;
	struct jpge_ctx *jpge_ctx = &ctx->jpge_ctx;

    int width = encoder->width;
    int height = encoder->height;

    void *dst = dst_;
    void *y = y_;
    void *uv = uv_;

    if ((unsigned long)dst % 256) {
        dst = check_alloc_mem(encoder, 0, dst_size);
        if (!dst)
            return -ENOMEM;
    }
    invalidate_dcache_force((unsigned long)dst, dst_size);

    if ((unsigned long)y % 256) {
        y = check_alloc_mem(encoder, 1, width*height);
        if (!y)
            return -ENOMEM;
        memcpy(y, y_, width*height);
    }
    flush_dcache_force((unsigned long)y, width*height);

    if ((unsigned long)uv % 256) {
        uv = check_alloc_mem(encoder, 2, width*height/2);
        if (!uv)
            return -ENOMEM;
        memcpy(uv, uv_, width*height/2);
    }
    flush_dcache_force((unsigned long)uv, width*height/2);

	struct video_frame_buffer src_frame;
	struct ingenic_vcodec_mem dst_addr;

    dst_addr.va = dst;
    dst_addr.pa = virt_to_phys(dst);
    dst_addr.size = dst_size;

    src_frame.num_planes = 2;
    src_frame.fb_addr[0].va = y;
    src_frame.fb_addr[0].pa = virt_to_phys(y);
    src_frame.fb_addr[0].size = width*height;
    src_frame.fb_addr[1].va = uv;
    src_frame.fb_addr[1].pa = virt_to_phys(uv);
    src_frame.fb_addr[1].size = width*height/2;

    jpge_ctx->bslen = 0;
    int ret = jpeg_encoder_encode(jpge_ctx, &src_frame, &dst_addr);
    if (ret < 0) {
        printf("jpeg encoder: failed to encode: %d\n", ret);
        return ret;
    }

    if (dst != dst_) {
        memcpy(dst_, dst, jpge_ctx->bslen);
        flush_dcache_force((unsigned long)dst_, jpge_ctx->bslen);
    }

    return jpge_ctx->bslen;
}

int helix_jpeg_encoder_encode(
    struct helix_jpeg_encoder *encoder, void *nv12, void *dst, int dst_size)
{
    return helix_jpeg_encoder_encode_nv12_separate(
        encoder, nv12, nv12+encoder->width*encoder->height, dst, dst_size);
}

void helix_jpeg_encoder_deinit(struct helix_jpeg_encoder *encoder)
{
    jpeg_encoder_free_workbuf(&encoder->ctx->jpge_ctx);
    ingenic_helix_ctx_deinit(encoder->ctx);
    if (encoder->buf[0]) free(encoder->buf[0]);
    if (encoder->buf[1]) free(encoder->buf[1]);
    if (encoder->buf[2]) free(encoder->buf[2]);
    free(encoder);
    ingenic_helix_deinit();
}
