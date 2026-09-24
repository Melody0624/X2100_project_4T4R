#include <types.h>
#include <os/thread_waiter.h>
#include <driver/cache.h>
#include "helix/h264_encoder/h264e_rc.h"
#include "helix/helix_drv.h"
#include "helix/helix_ops.h"
#include "videodev2.h"

#include <malloc.h>
#include <string.h>

#include "helix/helix_h264_encoder.h"

struct helix_h264_encoder {
    struct ingenic_venc_ctx *ctx;
    void *buf[3];
    int size[3];
    int width;
    int height;
    int keyframe;
};

static void set_h264_params(struct ingenic_venc_ctx *ctx, struct helix_h264_param *params)
{
    ingenic_vcodec_helix_set_param(ctx, V4L2_CID_MPEG_VIDEO_BITRATE, params->bitrate);
    ingenic_vcodec_helix_set_param(ctx, V4L2_CID_MPEG_VIDEO_HEADER_MODE, V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME);
    ingenic_vcodec_helix_set_param(ctx, V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP, 30);
    ingenic_vcodec_helix_set_param(ctx, V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP, 30);
    ingenic_vcodec_helix_set_param(ctx, V4L2_CID_MPEG_VIDEO_H264_MIN_QP, 10);
    ingenic_vcodec_helix_set_param(ctx, V4L2_CID_MPEG_VIDEO_H264_MAX_QP, 40);
    ingenic_vcodec_helix_set_param(ctx, V4L2_CID_MPEG_VIDEO_H264_PROFILE, V4L2_MPEG_VIDEO_H264_PROFILE_MAIN);
    ingenic_vcodec_helix_set_param(ctx, V4L2_CID_MPEG_VIDEO_H264_LEVEL, V4L2_MPEG_VIDEO_H264_LEVEL_3_0);
    ingenic_vcodec_helix_set_param(ctx, V4L2_CID_MPEG_VIDEO_GOP_SIZE, params->gop_size);
    ingenic_vcodec_helix_set_param(ctx, V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE, 1);
    ingenic_vcodec_helix_set_param(ctx, V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE, 0);

    switch(params->bitrate_mode) {
        case HELIX_BITRATE_VBR:
            ingenic_vcodec_helix_set_param(ctx, V4L2_CID_MPEG_VIDEO_BITRATE_MODE, V4L2_MPEG_VIDEO_BITRATE_MODE_VBR);
            break;
        case HELIX_BITRATE_FIXQP:
            ingenic_vcodec_helix_set_param(ctx, V4L2_CID_MPEG_VIDEO_BITRATE_MODE, V4L2_MPEG_VIDEO_BITRATE_MODE_CQ);
            break;
        case HELIX_BITRATE_CBR:
        default:
            ingenic_vcodec_helix_set_param(ctx, V4L2_CID_MPEG_VIDEO_BITRATE_MODE, V4L2_MPEG_VIDEO_BITRATE_MODE_CBR);
            break;
    }

    int width = params->width;
    int height = params->height;

    ingenic_vcodec_helix_set_fmt_out(ctx, width, height, V4L2_PIX_FMT_NV12);
    ingenic_vcodec_helix_set_fmt_cap(ctx, width, height, V4L2_PIX_FMT_H264);
}

struct helix_h264_encoder *helix_h264_encoder_init(struct helix_h264_param *params)
{
    ingenic_helix_init();

    struct helix_h264_encoder *encoder = malloc(sizeof(*encoder));
    memset(encoder, 0, sizeof(*encoder));

    encoder->ctx = ingenic_helix_ctx_init(V4L2_PIX_FMT_H264, 0);

    set_h264_params(encoder->ctx, params);
    encoder->width = params->width;
    encoder->height = params->height;

    int ret = h264e_alloc_workbuf(&encoder->ctx->h264e_ctx);
    if (ret) {
        printf("h264: failed to alloc workbuf: %d\n", ret);
        goto deinit_ctx;
    }

    ret = h264e_generate_headers(&encoder->ctx->h264e_ctx, 1);
    if (ret) {
        printf("h264: failed to generate header: %d\n", ret);
        goto free_workbuf;
    }

    return encoder;
free_workbuf:
    h264e_free_workbuf(&encoder->ctx->h264e_ctx);
deinit_ctx:
    ingenic_helix_ctx_deinit(encoder->ctx);
    free(encoder);
    ingenic_helix_deinit();
    return NULL;
}

static void *check_alloc_mem(struct helix_h264_encoder *encoder, int index, int size)
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

int helix_h264_encoder_encode_nv12_separate(
    struct helix_h264_encoder *encoder, void *y_, void *uv_, void *dst_, int dst_size)
{
	struct ingenic_venc_ctx *ctx = encoder->ctx;
	struct h264e_ctx *h264e_ctx = &ctx->h264e_ctx;

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

    /*process*/
    if(ctx->state == INGENIC_VENC_STATE_HEADER) {	//h264e
        int ret = h264e_encode_headers(h264e_ctx, &dst_addr);
        if (ret)
           printf("h264: failed to encode headers: %d\n", ret);
        ctx->state = INGENIC_VENC_STATE_RUNNING;
    }

    h264e_ctx->encoded_bs_len = 0;
    encoder->keyframe = 0;
    h264e_encode(h264e_ctx, &src_frame, &dst_addr, 0, &encoder->keyframe);

    if (dst != dst_) {
        memcpy(dst_, dst, h264e_ctx->encoded_bs_len);
        flush_dcache_force((unsigned long)dst_, h264e_ctx->encoded_bs_len);
    }

    return h264e_ctx->encoded_bs_len;
}

int helix_h264_encoder_is_keyframe(struct helix_h264_encoder *encoder)
{
    return encoder->keyframe;
}

int helix_h264_encoder_encode(
    struct helix_h264_encoder *encoder, void *nv12, void *dst, int dst_size)
{
    return helix_h264_encoder_encode_nv12_separate(
        encoder, nv12, nv12+encoder->width*encoder->height, dst, dst_size);
}

void helix_h264_encoder_deinit(struct helix_h264_encoder *encoder)
{
    h264e_free_workbuf(&encoder->ctx->h264e_ctx);
    ingenic_helix_ctx_deinit(encoder->ctx);
    if (encoder->buf[0]) free(encoder->buf[0]);
    if (encoder->buf[1]) free(encoder->buf[1]);
    if (encoder->buf[2]) free(encoder->buf[2]);
    free(encoder);
    ingenic_helix_deinit();
}
