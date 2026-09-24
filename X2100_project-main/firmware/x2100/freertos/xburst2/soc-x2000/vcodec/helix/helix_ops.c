#include <types.h>
#include <os/thread_waiter.h>
#include <driver/cache.h>
#include <string.h>
#include "helix/h264_encoder/h264e_rc.h"
#include "helix/helix_drv.h"
#include "helix/helix_ops.h"
#include "videodev2.h"

#define INGENIC_VENC_MIN_W      160U
#define INGENIC_VENC_MIN_H      120U
#define INGENIC_VENC_MAX_W      2560U
#define INGENIC_VENC_MAX_H      2048U


static struct ingenic_video_fmt ingenic_video_formats[] = {
	/* output */
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.type   = INGENIC_FMT_FRAME,
		.num_planes = 2,
		.format = HELIX_NV12_MODE,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.type = INGENIC_FMT_FRAME,
		.num_planes = 2,
		.format = HELIX_NV21_MODE,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUV420,
		.type = INGENIC_FMT_FRAME,
		.num_planes = 3,
		.format = HELIX_420P_MODE,
	},
	{
		.fourcc = V4L2_PIX_FMT_JPEG,
		.type = INGENIC_FMT_DEC,
		.num_planes = 1,
	},
	/*capture*/
	/* video encoder idx */
	{
		.fourcc = V4L2_PIX_FMT_H264,
		.type = INGENIC_FMT_ENC,
		.num_planes = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_JPEG,
		.type = INGENIC_FMT_ENC,
		.num_planes = 1,
	},
	{       /* jpeg decoder */
		.fourcc = V4L2_PIX_FMT_NV12,
		.type   = INGENIC_FMT_FRAME,
		.num_planes = 2,
		.format = HELIX_NV12_MODE,
	},

};

#define NUM_FORMATS     ARRAY_SIZE(ingenic_video_formats)
#define OUT_FMT_IDX     0
#define CAP_FMT_IDX     ARRAY_SIZE(ingenic_video_formats) - 4


static struct ingenic_video_fmt *ingenic_venc_find_format(u32 fourcc, int isout)
{
	struct ingenic_video_fmt *fmt;
	int i;
	int start_index = isout ? OUT_FMT_IDX : CAP_FMT_IDX;

	for(i = start_index; i < NUM_FORMATS; i++) {
		fmt = &ingenic_video_formats[i];
		if(fmt->fourcc == fourcc)
			return fmt;
	}

	return NULL;
}

struct ingenic_venc_q_data *ingenic_venc_get_q_data(struct ingenic_venc_ctx *ctx,
		                                                        enum v4l2_buf_type type)
{
	        if(V4L2_TYPE_IS_OUTPUT(type))
			                return &ctx->q_data[INGENIC_Q_DATA_SRC];

		        return &ctx->q_data[INGENIC_Q_DATA_DST];
}

void ingenic_vcodec_helix_work_thread(void *data)
{
	struct ingenic_venc_ctx *ctx = data;
	struct h264e_ctx *h264e_ctx = &ctx->h264e_ctx;
	struct jpge_ctx *jpge_ctx = &ctx->jpge_ctx;
	struct jpgd_ctx *jpgd_ctx = &ctx->jpgd_ctx;
	struct video_frame_buffer *src_frame = NULL;
	struct video_frame_buffer *dest_frame = NULL;
	struct video_frame_buffer dest_frame_t;
	int keyframe = 0;
	unsigned int ret = 0;
	unsigned long flags = 0;

	//printf("%s %d waiting...\n", __func__, __LINE__);
	thread_waiter_wait(&ctx->work_waiter);
	//printf("%s %d wakeup!\n", __func__, __LINE__);

	while(1) {
		sem_wait(&ctx->src_buf_queued_sem);
		sem_wait(&ctx->dest_buf_queued_sem);

		/*src_buf_remove*/
		spin_lock_irqsave(&ctx->src_buf_lock, flags);
		src_frame = list_first_entry(&ctx->src_queued_list, struct video_frame_buffer, queue_entry);
		list_del(&src_frame->queue_entry);
		spin_unlock_irqrestore(&ctx->src_buf_lock, flags);
		/*dest_buf_remove*/
		spin_lock_irqsave(&ctx->dest_buf_lock, flags);
		dest_frame = list_first_entry(&ctx->dest_queued_list, struct video_frame_buffer, queue_entry);
		list_del(&dest_frame->queue_entry);
		spin_unlock_irqrestore(&ctx->dest_buf_lock, flags);

		/*process*/
		if(ctx->state == INGENIC_VENC_STATE_HEADER) {	//h264e
			memcpy(dest_frame_t.fb_addr, dest_frame->fb_addr, sizeof(struct ingenic_vcodec_mem));
			ret = h264e_encode_headers(h264e_ctx, &dest_frame_t.fb_addr[0]);
			if(ret){
				/*TODO*/
			}
			ctx->state = INGENIC_VENC_STATE_RUNNING;
		}

		switch(ctx->codec_id) {
			case CODEC_ID_H264E:
				h264e_ctx->encoded_bs_len = 0;
				memcpy(dest_frame_t.fb_addr, dest_frame->fb_addr, sizeof(struct ingenic_vcodec_mem));
				h264e_encode(h264e_ctx, src_frame, &dest_frame_t.fb_addr[0], 0, &keyframe);
				dest_frame->fb_addr[0].size = h264e_ctx->encoded_bs_len;
				break;
			case CODEC_ID_JPGE:
				jpge_ctx->bslen = 0;
				memcpy(dest_frame_t.fb_addr, dest_frame->fb_addr, sizeof(struct ingenic_vcodec_mem));
				ret = jpeg_encoder_encode(jpge_ctx, src_frame, &dest_frame_t.fb_addr[0]);
				if(ret < 0)
					ctx->state = INGENIC_VENC_STATE_ABORT;
				dest_frame->fb_addr[0].size = jpge_ctx->bslen;
				break;
			case CODEC_ID_JPGD:
				ret = jpeg_decoder_decode(jpgd_ctx, &src_frame->fb_addr[0], dest_frame);
				if(ret < 0)
					ctx->state = INGENIC_VENC_STATE_ABORT;
			default:
				break;
		}

		/*src_buf_done*/
		spin_lock_irqsave(&ctx->src_buf_lock, flags);
		list_add_tail(&src_frame->done_entry, &ctx->src_done_list);
		spin_unlock_irqrestore(&ctx->src_buf_lock, flags);
		sem_post(&ctx->src_buf_done_sem);
		/*dest_buf_done*/
		spin_lock_irqsave(&ctx->dest_buf_lock, flags);
		list_add_tail(&dest_frame->done_entry, &ctx->dest_done_list);
		spin_unlock_irqrestore(&ctx->dest_buf_lock, flags);
		sem_post(&ctx->dest_buf_done_sem);
	}
}

int ingenic_vcodec_helix_set_param(void *data, unsigned int id, int value)
{
	struct ingenic_venc_ctx *ctx = data;
	struct h264e_ctx *h264e_ctx = &ctx->h264e_ctx;
	struct h264e_params *h264e_p = &h264e_ctx->p;
	struct jpge_ctx *jpge_ctx = &ctx->jpge_ctx;
	struct jpge_params *jpge_p = &jpge_ctx->p;
	unsigned int ret = 0;

	switch(id){
		case V4L2_CID_MPEG_VIDEO_BITRATE:
			h264e_p->bitrate = value;
			break;
		case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
			h264e_p->h264_hdr_mode = value;
			break;
		case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP:
			h264e_p->i_qp = value;
			break;
		case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP:
			h264e_p->p_qp = value;
			break;
		case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
			h264e_p->h264_max_qp = value;
			break;
		case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:
			h264e_p->h264_min_qp = value;
			break;
		case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
			h264e_p->h264_profile = value;
			break;
		case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
			h264e_p->h264_level = value;            //keep this value.
			break;
		case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
			h264e_p->gop_size = value;
			break;
		case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
			h264e_p->frame_rc_enable = value;
			break;
		case V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE:
			h264e_p->mb_rc_enable = value;
			break;
		case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
			h264e_p->rc_mode = value;
			break;
		case V4L2_CID_JPEG_COMPRESSION_QUALITY:
			jpge_p->compr_quality = value;
			break;
		default:
			ret = -EINVAL;
			break;
	}
	return ret;
}

int ingenic_vcodec_helix_get_param(void *data, unsigned int id, int *value)
{
	struct ingenic_venc_ctx *ctx = data;
	struct h264e_ctx *h264e_ctx = &ctx->h264e_ctx;
	struct h264e_params *h264e_p = &h264e_ctx->p;
	struct jpge_ctx *jpge_ctx = &ctx->jpge_ctx;
	struct jpge_params *jpge_p = &jpge_ctx->p;
	unsigned int ret = 0;

	switch(id){
		case V4L2_CID_MPEG_VIDEO_BITRATE:
			*value = h264e_p->bitrate;
			break;
		case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
			*value = h264e_p->h264_hdr_mode;
			break;
		case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP:
			*value = h264e_p->i_qp;
			break;
		case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP:
			*value = h264e_p->p_qp;
			break;
		case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
			*value = h264e_p->h264_max_qp;
			break;
		case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:
			*value = h264e_p->h264_min_qp;
			break;
		case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
			*value = h264e_p->h264_profile;
			break;
		case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
			*value = h264e_p->h264_level;
			break;
		case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
			*value = h264e_p->gop_size;
			break;
		case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
			*value = h264e_p->frame_rc_enable;
			break;
		case V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE:
			*value = h264e_p->mb_rc_enable;
			break;
		case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
			*value = h264e_p->rc_mode;
			break;
		case V4L2_CID_JPEG_COMPRESSION_QUALITY:
			*value = jpge_p->compr_quality;
			break;
		default:
			ret = -EINVAL;
			break;
	}
	return ret;
}

static int ingenic_vcodec_helix_try_fmt(struct ingenic_venc_q_data *q_data, int *width, int *height)
{
	struct ingenic_video_fmt *fmt = q_data->fmt;

	if(fmt->type == INGENIC_FMT_FRAME) {
		int tmp_w, tmp_h;

		*height = clamp((unsigned int)*height,
				INGENIC_VENC_MIN_H,
				INGENIC_VENC_MAX_H);
		*width = clamp((unsigned int)*width,
				INGENIC_VENC_MIN_W,
				INGENIC_VENC_MAX_W);

#if 0
		tmp_w = ALIGN(pix_fmt_mp->width, 16);
		tmp_h = ALIGN(pix_fmt_mp->height, 16);
		pr_info("tmp_w %d tmp_h %d, w %d h %d\n",
				tmp_w, tmp_h,
				pix_fmt_mp->width,
				pix_fmt_mp->height);
#else
		tmp_w = *width;
		tmp_h = *height;
#endif

		q_data->sizeimage[0] = tmp_w * tmp_h;

		q_data->bytesperline[0] = tmp_w;

		if(fmt->num_planes == 2) {
			q_data->sizeimage[1] =
				tmp_w * tmp_h / 2;
			q_data->sizeimage[2] = 0;

			q_data->bytesperline[1] = tmp_w;
			q_data->bytesperline[2] = 0;
		} else if(fmt->num_planes == 3) {
			q_data->sizeimage[1] =
				q_data->sizeimage[2] =
				(tmp_w * tmp_h) / 4;

			q_data->bytesperline[1] =
				q_data->bytesperline[2] =
				tmp_w / 2;
		}

	} else {
		q_data->bytesperline[0] = 0;

		/*restrict output bs buffer size, max width * height.
		 *                   sizeimage can be set by userspace.
		 *                                   */
		if(q_data->sizeimage[0]) {
			q_data->sizeimage[0] = min(q_data->sizeimage[0] , (unsigned int)(*width * *height * 3 / 4));
		} else {
			q_data->sizeimage[0] = *width * *height * 3 / 4;
		}
	}

	return 0;
}




int ingenic_vcodec_helix_set_fmt_out(void *data, int width, int height,unsigned int format)
{
	struct ingenic_venc_ctx *ctx = data;
	struct h264e_ctx *h264e_ctx = &ctx->h264e_ctx;
	struct jpge_ctx *jpge_ctx = &ctx->jpge_ctx;
	struct ingenic_video_fmt *fmt = NULL;
	struct ingenic_venc_q_data *q_data = NULL;
	unsigned int ret = 0;

	q_data = ingenic_venc_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

	fmt = ingenic_venc_find_format(format, 1);
	q_data->fmt = fmt;

	ret = ingenic_vcodec_helix_try_fmt(q_data, &width, &height);
	if(ret)
		return ret;


	switch(ctx->codec_id) {
		case CODEC_ID_H264E:
			h264e_encoder_init(h264e_ctx);
			h264e_set_fmt(h264e_ctx, width, height, fmt->format);
			break;
		case CODEC_ID_JPGE:
			jpeg_encoder_init(jpge_ctx);
			jpeg_encoder_set_fmt(jpge_ctx, width, height, fmt->format);
			break;
		default:
			break;
	}


	return ret;
}

int ingenic_vcodec_helix_set_fmt_cap(void *data, int width, int height,unsigned int format)
{
	struct ingenic_venc_ctx *ctx = data;
	struct jpgd_ctx *jpgd_ctx = &ctx->jpgd_ctx;
	struct ingenic_video_fmt *fmt = NULL;
	struct ingenic_venc_q_data *q_data = NULL;
	unsigned int ret = 0;

	q_data = ingenic_venc_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	fmt = ingenic_venc_find_format(format, 0);
	q_data->fmt = fmt;

	ret = ingenic_vcodec_helix_try_fmt(q_data, &width, &height);
	if(ret)
		return ret;

	if(ctx->codec_id == CODEC_ID_JPGD) {
		jpeg_decoder_init(jpgd_ctx);
		jpeg_decoder_set_fmt(jpgd_ctx, width, height, fmt->format);
	}


	return 0;
}

int ingenic_vcodec_helix_start(void *data)
{
	struct ingenic_venc_ctx *ctx = data;
	struct h264e_ctx *h264e_ctx = &ctx->h264e_ctx;
	struct jpge_ctx *jpge_ctx = &ctx->jpge_ctx;
	struct jpgd_ctx *jpgd_ctx = &ctx->jpgd_ctx;
	unsigned int ret;

	switch(ctx->codec_id) {
		case CODEC_ID_H264E:
			ret = h264e_alloc_workbuf(h264e_ctx);
			if(ret)
				return ret;
			ret = h264e_generate_headers(h264e_ctx, 1);
			if(ret)
				return ret;
			ctx->state = INGENIC_VENC_STATE_HEADER;
			break;
		case CODEC_ID_JPGE:
			ret = jpeg_encoder_alloc_workbuf(jpge_ctx);
			if(ret)
				return ret;
			ctx->state = INGENIC_VENC_STATE_RUNNING;
			break;
		case CODEC_ID_JPGD:
			ret = jpeg_decoder_alloc_workbuf(jpgd_ctx);
			if(ret)
				return ret;
			ctx->state = INGENIC_VENC_STATE_RUNNING;
			break;
		default:
			break;
	}

	thread_waiter_wakeup(&ctx->work_waiter);
	return 0;
}

int ingenic_vcodec_helix_stop(void *data)
{
	struct ingenic_venc_ctx *ctx = data;
	struct h264e_ctx *h264e_ctx = &ctx->h264e_ctx;
	struct jpge_ctx *jpge_ctx = &ctx->jpge_ctx;
	struct jpgd_ctx *jpgd_ctx = &ctx->jpgd_ctx;

	/*TODO:buf_done*/
	switch(ctx->codec_id) {
		case CODEC_ID_H264E:
			h264e_free_workbuf(h264e_ctx);
			break;
		case CODEC_ID_JPGE:
			jpeg_encoder_free_workbuf(jpge_ctx);
			break;
		case CODEC_ID_JPGD:
			jpeg_decoder_free_workbuf(jpgd_ctx);
			break;
		default:
			break;
	}

	ctx->state = INGENIC_VENC_STATE_IDLE;
	return 0;
}

int ingenic_vcodec_helix_create_srcbuf(void *data, int create_num)
{
	struct ingenic_venc_ctx *ctx = data;
	struct video_frame_buffer *src_frame = NULL;
	struct ingenic_video_fmt *fmt = NULL;
	struct ingenic_venc_q_data *q_data = NULL;
	int i,j;

	q_data = ingenic_venc_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	fmt = q_data->fmt;

	ctx->src_frame = (struct video_frame_buffer *)JZMalloc(4, sizeof(struct video_frame_buffer) * create_num);
	src_frame = ctx->src_frame;
	ctx->srcframe_num = create_num;

	for(j = 0; j < create_num; j++)
	{
		for(i = 0; i < fmt->num_planes; i++) {
			src_frame[j].fb_addr[i].va = (void *)JZMalloc(0x1000, q_data->sizeimage[i]);
			src_frame[j].fb_addr[i].pa = (dma_addr_t)get_phy_addr(src_frame[j].fb_addr[i].va);
			src_frame[j].fb_addr[i].size = q_data->sizeimage[i];
		}

		src_frame[j].index = j;
		src_frame[j].num_planes = fmt->num_planes;
	}

	INIT_LIST_HEAD(&ctx->src_queued_list);
	INIT_LIST_HEAD(&ctx->src_done_list);

	return create_num;
}

int ingenic_vcodec_helix_destroy_srcbuf(void *data)
{
	struct ingenic_venc_ctx *ctx = data;
	struct video_frame_buffer *src_frame = NULL;
	struct ingenic_venc_q_data *q_data = NULL;
	struct ingenic_video_fmt *fmt = NULL;
	unsigned int i,j;

	q_data = ingenic_venc_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	fmt = q_data->fmt;
	src_frame = ctx->src_frame;
	for(i = 0; i < ctx->srcframe_num; i++) {
		for(j = 0; j < fmt->num_planes; j++)
			free(src_frame[i].fb_addr[j].va);
	}
	free(ctx->src_frame);

	return 0;
}

unsigned int ingenic_vcodec_helix_get_srcbuf_np(void *data)
{
	struct ingenic_venc_ctx *ctx = data;
	struct ingenic_video_fmt *fmt = NULL;
	struct ingenic_venc_q_data *q_data = NULL;

	q_data = ingenic_venc_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	fmt = q_data->fmt;

	return fmt->num_planes;
}

int ingenic_vcodec_helix_set_srcbuf(void *data, int index, unsigned int vaddr, unsigned int paddr, unsigned int size)
{
	struct ingenic_venc_ctx *ctx = data;
	struct video_frame_buffer *src_frame = ctx->src_frame;

	/*TODO mplane*/
	src_frame[index].fb_addr[0].va = (void *)vaddr;
	src_frame[index].fb_addr[0].pa = (dma_addr_t)paddr;
	src_frame[index].fb_addr[0].size = size;

	return 0;
}

int ingenic_vcodec_helix_get_srcbuf(void *data, int index, int plane, unsigned int *vaddr, unsigned int *size)
{
	struct ingenic_venc_ctx *ctx = data;
	struct video_frame_buffer *src_frame = ctx->src_frame;

	*vaddr = (unsigned int)src_frame[index].fb_addr[plane].va;
	*size = (unsigned int)src_frame[index].fb_addr[plane].size;

	return 0;
}

int ingenic_vcodec_helix_create_destbuf(void *data, int create_num)
{
	struct ingenic_venc_ctx *ctx = data;
	struct video_frame_buffer *dest_frame = NULL;
	struct ingenic_video_fmt *fmt = NULL;
	struct ingenic_venc_q_data *q_data = NULL;
	int i,j;

	q_data = ingenic_venc_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	fmt = q_data->fmt;

	ctx->dest_frame = (struct video_frame_buffer *)JZMalloc(4, sizeof(struct video_frame_buffer) * create_num);
	dest_frame = ctx->dest_frame;
	ctx->destframe_num = create_num;

	INIT_LIST_HEAD(&ctx->dest_queued_list);
	INIT_LIST_HEAD(&ctx->dest_done_list);

	for(i = 0; i < create_num; i++)
	{
		for(j = 0; j < fmt->num_planes; j++) {
			dest_frame[i].fb_addr[j].va = (void *)JZMalloc(0x1000, q_data->sizeimage[j]);
			dest_frame[i].fb_addr[j].pa = (dma_addr_t)get_phy_addr(dest_frame[i].fb_addr[j].va);
			dest_frame[i].fb_addr[j].size = q_data->sizeimage[j];
		}

		dest_frame[i].index = i;
		dest_frame[i].num_planes = fmt->num_planes;
		list_add_tail(&dest_frame[i].queue_entry, &ctx->dest_queued_list);
		sem_post(&ctx->dest_buf_queued_sem);
	}

	return create_num;
}

int ingenic_vcodec_helix_destroy_destbuf(void *data)
{
	struct ingenic_venc_ctx *ctx = data;
	struct video_frame_buffer *dest_frame = NULL;
	struct ingenic_venc_q_data *q_data = NULL;
	struct ingenic_video_fmt *fmt = NULL;
	unsigned int i,j;

	q_data = ingenic_venc_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	fmt = q_data->fmt;

	dest_frame = ctx->dest_frame;
	for(i = 0; i < ctx->destframe_num; i++) {
		for(j = 0; j < fmt->num_planes; j++)
			free(dest_frame[i].fb_addr[j].va);
	}
	free(ctx->dest_frame);

	return 0;
}

unsigned int ingenic_vcodec_helix_get_destbuf_np(void *data)
{
	struct ingenic_venc_ctx *ctx = data;
	struct ingenic_video_fmt *fmt = NULL;
	struct ingenic_venc_q_data *q_data = NULL;

	q_data = ingenic_venc_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	fmt = q_data->fmt;

	return fmt->num_planes;
}

int ingenic_vcodec_helix_set_destbuf(void *data, int index, unsigned int vaddr, unsigned int paddr, unsigned int size)
{
	struct ingenic_venc_ctx *ctx = data;
	struct video_frame_buffer *dest_frame = ctx->dest_frame;

	/*TODO num_planes*/
	dest_frame[index].fb_addr[0].va = (void *)vaddr;
	dest_frame[index].fb_addr[0].pa = (dma_addr_t)paddr;
	dest_frame[index].fb_addr[0].size = size;

	return 0;
}

int ingenic_vcodec_helix_get_destbuf(void *data, int index, unsigned int *vaddr, unsigned int *size)
{
	struct ingenic_venc_ctx *ctx = data;
	struct video_frame_buffer *dest_frame = ctx->dest_frame;

	*vaddr = (unsigned int)dest_frame[index].fb_addr[0].va;
	*size = (unsigned int)dest_frame[index].fb_addr[0].size;

	return 0;
}

int ingenic_vcodec_helix_srcbuf_wait(void *data, int block)
{
	struct ingenic_venc_ctx *ctx = data;
	int ret = 0;
	if(block)
		ret = sem_wait(&ctx->src_buf_done_sem);
	else
		ret = sem_trywait(&ctx->src_buf_done_sem);

	return ret;
}

int ingenic_vcodec_helix_srcbuf_queue(void *data, int index)
{
	struct ingenic_venc_ctx *ctx = data;
	struct video_frame_buffer *src_frame = ctx->src_frame;
	unsigned long flags = 0;
	unsigned int np = 0;
	unsigned int i = 0;

	np = ingenic_vcodec_helix_get_srcbuf_np(data);
	for(i = 0; i < np; i++)
		flush_dcache((unsigned long)src_frame[index].fb_addr[i].va, src_frame[index].fb_addr[i].size);

	spin_lock_irqsave(&ctx->src_buf_lock, flags);
	list_add_tail(&src_frame[index].queue_entry, &ctx->src_queued_list);
	spin_unlock_irqrestore(&ctx->src_buf_lock, flags);

	sem_post(&ctx->src_buf_queued_sem);

	return 0;
}

int ingenic_vcodec_helix_srcbuf_dequeue(void *data, int *index)
{
	struct ingenic_venc_ctx *ctx = data;
	struct video_frame_buffer *src_frame = NULL;
	unsigned long flags = 0;

	spin_lock_irqsave(&ctx->src_buf_lock, flags);
	if(list_empty(&ctx->src_done_list)) {
		spin_unlock_irqrestore(&ctx->src_buf_lock, flags);
		return -EINVAL;
	}
	src_frame = list_first_entry(&ctx->src_done_list, struct video_frame_buffer, done_entry);
	list_del(&src_frame->done_entry);
	spin_unlock_irqrestore(&ctx->src_buf_lock, flags);

	*index = src_frame->index;

	return 0;
}

int ingenic_vcodec_helix_destbuf_wait(void *data, int block)
{
	struct ingenic_venc_ctx *ctx = data;
	int ret = 0;
	if(block)
		ret = sem_wait(&ctx->dest_buf_done_sem);
	else
		ret = sem_trywait(&ctx->dest_buf_done_sem);

	return ret;
}

int ingenic_vcodec_helix_destbuf_queue(void *data, int index)
{
	struct ingenic_venc_ctx *ctx = data;
	struct video_frame_buffer *dest_frame = ctx->dest_frame;
	unsigned long flags = 0;

	spin_lock_irqsave(&ctx->dest_buf_lock, flags);
	list_add_tail(&dest_frame[index].queue_entry, &ctx->dest_queued_list);
	spin_unlock_irqrestore(&ctx->dest_buf_lock, flags);

	sem_post(&ctx->dest_buf_queued_sem);

	return 0;
}

int ingenic_vcodec_helix_destbuf_dequeue(void *data, int *index, unsigned int *vaddr, unsigned int *size)
{
	struct ingenic_venc_ctx *ctx = data;
	struct video_frame_buffer *dest_frame = NULL;
	unsigned long flags = 0;
	unsigned long np = 0;
	unsigned long i = 0;

	spin_lock_irqsave(&ctx->dest_buf_lock, flags);
	if(list_empty(&ctx->dest_done_list)) {
		spin_unlock_irqrestore(&ctx->dest_buf_lock, flags);
		return -EINVAL;
	}
	dest_frame = list_first_entry(&ctx->dest_done_list, struct video_frame_buffer, done_entry);
	list_del(&dest_frame->done_entry);
	spin_unlock_irqrestore(&ctx->dest_buf_lock, flags);

	*index = dest_frame->index;

	np = ingenic_vcodec_helix_get_destbuf_np(data);
	for(i = 0; i < np; i++) {
		vaddr[i] = (unsigned int)dest_frame->fb_addr[i].va;
		size[i] = (unsigned int)dest_frame->fb_addr[i].size;
	}

	return 0;
}

