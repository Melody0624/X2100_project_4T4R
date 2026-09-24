#include "felix_drv.h"
#include "felix_ops.h"
#include <common.h>
#include <io.h>
#include <config.h>
#include <driver/cache.h>

static int _init_buffer(struct ingenic_vdec_ctx* ctx, int dst_src, unsigned int y_size, unsigned int uv_size, int num)
{
	if(y_size <= 0)
		return -1;

	felix_buffer_t *buf = NULL;
	int j = 0,i = 0;
	if(dst_src == 0){	// 0 is creat src buf
		for(i = 0; i < num; i++){
			buf = &ctx->src_buf[i];
			buf->vaddr = (unsigned int)av_malloc(y_size + uv_size);		// malloc in kseg0
			buf->paddr = buf->vaddr & 0x7FFFFFFF;
			buf->size  = y_size + uv_size;
			buf->index = i;
			//printf("src buf->vaddr = 0x%x; buf->paddr = 0x%x; buf->size = %d; buf->index = %d======\r\n",buf->vaddr, buf->paddr, buf->size, buf->index);
			buf->frame.buf[0] = av_buffer_create((char*)buf->vaddr,buf->paddr,y_size);	// src is h264 only one plane
		}
	} else {	// dst buf create
		for(j = 0; j < num; j++){
			buf = &ctx->dst_buf[j];
			buf->vaddr = (unsigned int)av_malloc(y_size + uv_size);		// malloc in kseg0
			buf->paddr = buf->vaddr & 0x7FFFFFFF;
			buf->size  = y_size + uv_size;
			buf->index = j;
			//printf("dst buf->vaddr = 0x%x; buf->paddr = 0x%x; buf->size = %d; buf->index = %d======\r\n",buf->vaddr, buf->paddr, buf->size, buf->index);
			// only nv12 / nv21
			buf->frame.buf[0] = av_buffer_create((char*)buf->vaddr,buf->paddr,y_size);
			buf->frame.buf[1] = av_buffer_create((char*)(buf->vaddr + y_size),(buf->paddr+y_size),uv_size);
			list_add_tail(&ctx->dst_buf[j].entry, &ctx->dst_queue_list);
			sem_post(&ctx->dst_buf_queued_sem);
		}
	}

	return num;
}

void ingenic_vcodec_felix_work_thread(void *data)
{
	struct ingenic_vdec_ctx *ctx = data;
	felix_buffer_t *src_buf = NULL;
	felix_buffer_t *dst_buf = NULL;
	felix_buffer_t src_buf_t;
	unsigned int ret = 0;
	unsigned long flags = 0;

	AVPacket avpkt;
	AVCodecContext *avctx = ctx->avctx;
	H264Context *h = ctx->h;
	int got_frame = 0;
	AVFrame *f = NULL;

	//printf("%s %d waiting...\n", __func__, __LINE__);
	thread_waiter_wait(&ctx->work_waiter);
	//printf("%s %d wakeup!\n", __func__, __LINE__);

	while(1) {
		sem_wait(&ctx->src_buf_queued_sem);
		sem_wait(&ctx->dst_buf_queued_sem);

		/*src_buf_remove*/
		spin_lock_irqsave(&ctx->src_buf_lock, flags);
		src_buf = list_first_entry_or_null(&ctx->src_queue_list, felix_buffer_t, entry);
		list_del(&src_buf->entry);
		spin_unlock_irqrestore(&ctx->src_buf_lock, flags);
		/*dst_buf_remove*/
		spin_lock_irqsave(&ctx->dst_buf_lock, flags);
		dst_buf = list_first_entry_or_null(&ctx->dst_queue_list, felix_buffer_t, entry);
		if(!dst_buf){
			printf("get dst-buffer failed !!!!!!\r\n");
			list_add_tail(&src_buf->entry,&ctx->src_done_list);
			return;
		} else {
			list_del(&dst_buf->entry);
			h264_enqueue_frame(h, &dst_buf->frame);
		}

		spin_unlock_irqrestore(&ctx->dst_buf_lock, flags);

		memcpy(&src_buf_t, src_buf, sizeof(felix_buffer_t));
		avpkt.size = src_buf_t.size;
		avpkt.data_pa = src_buf_t.paddr;
		avpkt.data = (void *)src_buf_t.vaddr;

		flush_cache_all();
		ret = h264_decode_frame(avctx, &ctx->dec_frame, &got_frame, &avpkt);
		if(ret < 0){
			printf("decode failed !!!!!!\r\n");
			list_add_tail(&src_buf->entry, &ctx->src_done_list);
			return;
		}

		if(got_frame){
			f = h264_dequeue_frame(h);
			if(f){
				felix_buffer_t *out = container_of(f, felix_buffer_t, frame);
				/*dst_buf_done*/
				spin_lock_irqsave(&ctx->dst_buf_lock, flags);
				list_add_tail(&out->entry, &ctx->dst_done_list);
				spin_unlock_irqrestore(&ctx->dst_buf_lock, flags);
				sem_post(&ctx->dst_buf_done_sem);
			} else {
				printf("got frame failed !!!!!!\r\n");
				ret = -2;
			}
		}

		/*src_buf_done*/
		spin_lock_irqsave(&ctx->src_buf_lock, flags);
		list_add_tail(&src_buf->entry, &ctx->src_done_list);
		spin_unlock_irqrestore(&ctx->src_buf_lock, flags);
		sem_post(&ctx->src_buf_done_sem);
	}
}

extern struct vpu_ops ingenic_vpu_ops;
extern int vpu_on(void);

int ingenic_vcodec_felix_set_param(void *data, int width, int height, int format)
{
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)data;
	int ret = 0;
	ctx->sWidth = width;
	ctx->sHeight = height;

	ctx->avctx->width = width;
	ctx->avctx->height = height;

	h264_set_vpu_ops(ctx->h, ctx, &ingenic_vpu_ops);

	// VPU ON
	vpu_on();

	h264_set_output_format(ctx->h, format, width, height);

	return 0;
}

int ingenic_vcodec_felix_get_param(void *data, unsigned int id, int *value)
{
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)data;
	H264Context *h = ctx->h;
	int ret = 0;
	switch(id){
		case FELIX_FORMAT:
			*value = h->format;
			break;
		case FELIX_WIDTH:
			*value = h->coded_width;
			break;
		case FELIX_HEIGHT:
			*value = h->coded_height;
			break;
		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}

int ingenic_vcodec_felix_start(void *data)
{
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)data;
	ctx->state = INGENIC_STATE_RUNNING;

	thread_waiter_wakeup(&ctx->work_waiter);
	return 0;
}

int ingenic_vcodec_felix_stop(void *data)
{
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)data;
	ctx->state = INGENIC_STATE_IDLE;

	thread_delete(ctx->work_thread);
	sem_destroy(&ctx->src_buf_queued_sem);
	sem_destroy(&ctx->src_buf_done_sem);

	sem_destroy(&ctx->dst_buf_queued_sem);
	sem_destroy(&ctx->dst_buf_done_sem);

	return 0;
}

int ingenic_vcodec_felix_create_srcbuf(void *data, int create_num)
{
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)data;
	int width = ctx->sWidth;
	int height = ctx->sHeight;
	int ret = 0;

	INIT_LIST_HEAD(&ctx->src_done_list);
	INIT_LIST_HEAD(&ctx->src_queue_list);
	// src_buf create
	ret = _init_buffer(ctx, 0, width * height * 4 / 3, 0, create_num);
	flush_cache_all();

	return ret;
}

int ingenic_vcodec_felix_set_srcbuf(void *data, int index, unsigned int vaddr, unsigned int paddr)
{
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)data;
	felix_buffer_t *src_buf = &ctx->src_buf[index];
	int width = ctx->sWidth;
	int height = ctx->sHeight;

	src_buf->vaddr = vaddr;
	src_buf->paddr = paddr;
	src_buf->size = width * height * 4 / 3;

	return 0;
}

int ingenic_vcodec_felix_get_srcbuf(void *data, int index, unsigned int *vaddr, unsigned int *size)
{
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)data;
	felix_buffer_t *src_buf = &ctx->src_buf[index];

	*vaddr = (unsigned int)src_buf->vaddr;
	*size = (unsigned int)src_buf->size;

	return 0;
}

int ingenic_vcodec_felix_create_dstbuf(void *data, int create_num)
{
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)data;
	H264Context *h = ctx->h;
	int width = ctx->sWidth;
	int height = ctx->sHeight;
	int ret = 0;

	INIT_LIST_HEAD(&ctx->dst_queue_list);
	INIT_LIST_HEAD(&ctx->dst_done_list);

	ret = _init_buffer(ctx, 1, width * height, width * height / 2, create_num);
	flush_cache_all();

	return ret;
}

int ingenic_vcodec_felix_set_dstbuf(void *data, int index, unsigned int vaddr, unsigned int paddr, unsigned int size)
{
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)data;
	felix_buffer_t *dst_buf = &ctx->dst_buf[index];

	dst_buf->vaddr = vaddr;
	dst_buf->paddr = paddr;
	dst_buf->size = size;

	return 0;
}

int ingenic_vcodec_felix_get_dstbuf(void *data, int np, int index, unsigned int *vaddr, unsigned int *size)
{
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)data;
	felix_buffer_t *dst_buf = &ctx->dst_buf[index];
	AVFrame *f = &dst_buf->frame;

	if(np == 0){
		*vaddr = (unsigned int)f->buf[0]->buffer;
		*size = f->buf[0]->size;
	}else if(np == 1){
		*vaddr = (unsigned int)f->buf[1]->buffer;
		*size = f->buf[1]->size;
	}

	return 0;
}

int ingenic_vcodec_felix_srcbuf_wait(void *data, int block)
{
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)data;
	int ret = 0;
	if(block)
		ret = sem_wait(&ctx->src_buf_done_sem);
	else
		ret = sem_trywait(&ctx->src_buf_done_sem);

	return ret;
}

int ingenic_vcodec_felix_srcbuf_queue(void *data, int index)
{
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)data;
	felix_buffer_t *src_buf = &ctx->src_buf[index];
	unsigned long flags = 0;

	//flush_cache_all();

	spin_lock_irqsave(&ctx->src_buf_lock, flags);
	list_add_tail(&src_buf->entry, &ctx->src_queue_list);
	spin_unlock_irqrestore(&ctx->src_buf_lock, flags);

	sem_post(&ctx->src_buf_queued_sem);

	return 0;

}

int ingenic_vcodec_felix_srcbuf_dequeue(void *data, int *index)
{
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)data;
	felix_buffer_t *src_buf = NULL;
	unsigned long flags = 0;

	spin_lock_irqsave(&ctx->src_buf_lock, flags);
	src_buf = list_first_entry_or_null(&ctx->src_done_list, felix_buffer_t, entry);
	if(!src_buf){
		printf("no src done buf\n");
		spin_unlock_irqrestore(&ctx->src_buf_lock, flags);
		return -EINVAL;
	}
	list_del(&src_buf->entry);
	spin_unlock_irqrestore(&ctx->src_buf_lock, flags);

	*index = src_buf->index;

	return 0;
}

int ingenic_vcodec_felix_dstbuf_wait(void *data, int block)
{
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)data;
	int ret = 0;
	if(block)
		ret = sem_wait(&ctx->dst_buf_done_sem);
	else
		ret = sem_trywait(&ctx->dst_buf_done_sem);

	return ret;

}

int ingenic_vcodec_felix_dstbuf_queue(void *data, int index)
{
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)data;
	felix_buffer_t *dst_buf = &ctx->dst_buf[index];
	unsigned long flags = 0;

	spin_lock_irqsave(&ctx->dst_buf_lock, flags);
	list_add_tail(&dst_buf->entry, &ctx->dst_queue_list);
	spin_unlock_irqrestore(&ctx->dst_buf_lock, flags);

	sem_post(&ctx->dst_buf_queued_sem);

	return 0;
}

int ingenic_vcodec_felix_dstbuf_dequeue(void *data, int num, int *index, unsigned int *vaddr, unsigned int *size)
{
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)data;
	felix_buffer_t *dst_buf = NULL;
	unsigned long flags = 0;
	unsigned int width = 0;
	unsigned int height = 0;
	int i;

	width = ctx->sWidth;
	height = ctx->sHeight;

	spin_lock_irqsave(&ctx->dst_buf_lock, flags);
	dst_buf = list_first_entry_or_null(&ctx->dst_done_list, felix_buffer_t, entry);
	if(!dst_buf){
		printf("no dst done buf\n");
		spin_unlock_irqrestore(&ctx->dst_buf_lock, flags);
		return -EINVAL;
	}
	list_del(&dst_buf->entry);
	spin_unlock_irqrestore(&ctx->dst_buf_lock, flags);

	AVFrame *f = &dst_buf->frame;
	*index = dst_buf->index;
	for(i = 0; i < num; i++) {
		vaddr[i] = (unsigned int)f->buf[i]->buffer;
		size[i] = f->buf[i]->size;
	}

	return 0;
}






