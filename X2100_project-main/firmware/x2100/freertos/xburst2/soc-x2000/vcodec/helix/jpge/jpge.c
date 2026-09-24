/*
* Copyright© 2014 Ingenic Semiconductor Co.,Ltd
*
* Author: qipengzhen <aric.pzqi@ingenic.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
*/

#include "jpge.h"

#include "ht.h"
#include "qlook.h"
#include "head.h"

#include "helix/helix_buf.h"
#include "helix/helix_drv.h"

static unsigned char qt_new[128];
static unsigned int qetable[256];

static void dump_slice_info(_JPEGE_SliceInfo *s)
{
	printf("s->des_va:	%08x\n", (unsigned int)s->des_va);
	printf("s->des_pa:	%08x\n", s->des_pa);
	printf("s->ncol:	%d\n", s->ncol);
	printf("s->rsm:		%d\n", s->rsm);
	printf("s->bsa:		%08x\n", s->bsa);
	printf("s->p0a:		%08x\n", s->p0a);
	printf("s->p1a:		%08x\n", s->p1a);
	printf("s->nrsm:	%d\n", s->nrsm);
	printf("s->raw[0]:	%08x\n", s->raw[0]);
	printf("s->raw[1]:	%08x\n", s->raw[1]);
	printf("s->raw[2]:	%08x\n", s->raw[2]);
	printf("s->stride[0]:	%d\n", s->stride[0]);
	printf("s->stride[1]:	%d\n", s->stride[1]);
	printf("s->mb_height:	%d\n", s->mb_height);
	printf("s->mb_width:	%d\n", s->mb_width);
	printf("s->nmcu:	%d\n", s->nmcu);
	printf("s->raw_format:	%d(%s)\n", s->raw_format,
	       	s->raw_format == 8 ? "NV12" :
	       	s->raw_format == 12 ? "NV21" :
	       	s->raw_format == 0 ? "TILE(unsupported)" : "invalid");
}

static int jpge_fill_slice_info(struct jpge_ctx *ctx)
{
	struct jpge_params *p = &ctx->p;
	_JPEGE_SliceInfo *s = ctx->s;
	int ret = 0;

	s->des_va = ctx->desc;
	s->des_pa = ctx->desc_pa;

	s->ncol = 2;	/* unused? */
	s->rsm = 0;
	s->bsa = ctx->bs->pa + ctx->header_size;
	s->p0a = 0;
	s->p1a = 0;
	s->nrsm = 0;

	s->raw[0] = ctx->frame->fb_addr[0].pa;	/*Y*/
	s->raw[1] = ctx->frame->fb_addr[1].pa;	/*U for 420p or UV for nv12*/
	s->raw[2] = ctx->frame->fb_addr[2].pa;	/*V for 420p*/

	s->stride[0] = s->stride[1] = p->width;

	s->mb_height = (p->height + 15) / 16;
	s->mb_width = p->width / 16;
	s->nmcu = s->mb_height * s->mb_width - 1;
	s->raw_format = p->format;

	return ret;
}

static int jpge_gen_header(struct jpge_ctx *ctx)
{
	struct ingenic_vcodec_mem *bs = ctx->bs;
	struct jpge_params *p = &ctx->p;
	char *pbuf = bs->va;
	char *ptr = NULL;
	int i,j;
	int header_size;
	int padsize;

	/* SOI 文件开始 */
	*pbuf++ = 0xff;
	*pbuf++ = M_SOI;

	/* DQT -- 0 */
	*pbuf++ = 0xff;
	*pbuf++ = M_DQT;
	*pbuf++ = 0x0;
	*pbuf++ = 0x43;
	*pbuf++ = 0x0;

	ptr = (char *)&qt_new[0];
	for(i = 0; i < 64; i++)
		*pbuf++ = *ptr++;

	/* DQT -- 1 */
	*pbuf++ = 0xff;
	*pbuf++ = M_DQT;
	*pbuf++ = 0;
	*pbuf++ = 0x43;
	*pbuf++ = 0x01;
	for(i = 0; i < 64; i++) {
		*pbuf++ = *ptr++;
	}

	/* DHT -- lumia DC/AC, Chromia DC/AC */
	header_size = JPG_HUFFMAN_TABLE_LENGTH + 2;
	*pbuf++ = 0xff;
	*pbuf++ = M_DHT;
	*pbuf++ = (header_size & 0xff00) >> 8;;
	*pbuf++ = header_size & 0xff;
	memmove(pbuf, &jpeg_huffman_table, JPG_HUFFMAN_TABLE_LENGTH);
	pbuf += JPG_HUFFMAN_TABLE_LENGTH;

	/* SOF */
	*pbuf++ = 0xff;
	*pbuf++ = M_SOF0;
	*pbuf++ = 0x0;
	*pbuf++ = 0x11;				//Lf = 17
	*pbuf++ = 8;				//8bit sample
	*pbuf++ = (p->height & 0xff00) >> 8;	//Y=height
	*pbuf++ = p->height & 0xff;
	*pbuf++ = (p->width & 0xff00) >> 8;	//X=width
	*pbuf++ = p->width & 0xff;
	*pbuf++ = 3;				//Nf=3, number of component, Y U V
	*pbuf++ = 1;				//采样系数, Component Y 设置.
	*pbuf++ = 0x22;				//Hori:2 Vertical:2 水平采样系数和垂直采样系数.
	*pbuf++ = 0x00;				//使用量化表0.
	*pbuf++ = 2;				//Component Cb.
	*pbuf++ = 0x11;				//H:1 V:1
	*pbuf++ = 0x1;				//使用量化表1.
	*pbuf++ = 3;				//Component Cr.
	*pbuf++ = 0x11;				//H:1 V:1
	*pbuf++ = 0x1;				//使用量化表1.

	/* 添加注释,填充header到256字节对齐. 已知COM段11字节， 已知SOS段14字节 */
	header_size = pbuf - (char *)bs->va + 11 + 14;
	padsize = 256 - header_size % 256;

	/* COM 注释 */
	header_size = padsize + 9;
	*pbuf++ = 0xff;
	*pbuf++ = M_COM;
	*pbuf++ = (header_size & 0xff00) >> 8;
	*pbuf++ = header_size & 0xff;
	*pbuf++ = 'I';
	*pbuf++ = 'N';
	*pbuf++ = 'G';
	*pbuf++ = 'E';
	*pbuf++ = 'N';
	*pbuf++ = 'I';
	*pbuf++ = 'C';

	memset(pbuf, 0, padsize);
	pbuf += padsize;

	/* SOS */
	*pbuf++ = 0xff;
	*pbuf++ = M_SOS;
	//Ls = 12
	*pbuf++ = 0x0;
	*pbuf++ = 0xC;
	//Ns
	*pbuf++ = 0x3;
	//Cs1 - Y
	*pbuf++ = 0x1;
	//Td, Ta
	*pbuf++ = 0x00;
	//Cs2 - U
	*pbuf++ = 0x2;
	//Td, Ta
	*pbuf++ = 0x11;
	//Cs3 - V
	*pbuf++ = 0x3;
	//Td, Ta
	*pbuf++ = 0x11;
	//Ss
	*pbuf++ = 0x00;
	//Se
	*pbuf++ = 0x3f;
	//Ah, Al
	*pbuf++ = 0x0;

	ctx->header_size = pbuf - (char *)bs->va;
	return 0;
}

static void jpeg_gen_qt(int qt_factor)
{
	int i;
	int temp;
	static int save_qt_factor = -1;

	if (qt_factor == save_qt_factor)
		return;

	save_qt_factor = qt_factor;

	for (i = 0; i < ARRAY_SIZE(jpeg_quant_table); i++){
		temp = ((int) jpeg_quant_table[i] * qt_factor + 50) / 100;
		if (temp <= 0) temp = 1;
		if (temp > 255) temp = 255;

		qt_new[i] = temp;
		qetable[i] = qlook[temp];
	}
}

int jpeg_encoder_encode(struct jpge_ctx *ctx, struct video_frame_buffer *frame, struct ingenic_vcodec_mem *bs)
{
	int ret = 0;
	char *pbuf = NULL;

	ingenic_vpu_lock();

	ctx->frame = frame;
	bs->va = (void *)((unsigned long) bs->va | 0xa0000000);
	ctx->bs = bs;

	jpeg_gen_qt(ctx->p.compr_quality);

	ret = jpge_gen_header(ctx);

	jpge_fill_slice_info(ctx);
	//dump_slice_info(ctx->s);

	JPEGE_SliceInit(ctx->s, qetable);

	flush_dcache((unsigned long)ctx->desc, ctx->vdma_chain_len);
	ret = ingenic_vpu_start(ctx->priv);
	if(ret < 0)
		goto unlock_vpu;

	/* Modify bslen.
		total bslen = header_size + vpu encoded bslen.
	*/
	ctx->bslen = ctx->header_size + ctx->bslen;

	pbuf = bs->va + ctx->bslen;
	/* EOI */
	*pbuf++ = 0xff;
	*pbuf++ = 0xd9;

	ctx->bslen += 2;

	invalidate_dcache((unsigned long)(bs->va), ALIGN(ctx->bslen, 64));

unlock_vpu:
	ingenic_vpu_unlock();

	return ret;
}


int jpeg_encoder_set_fmt(struct jpge_ctx *ctx, int width, int height, int format)
{
	struct jpge_params *p = &ctx->p;
	int ret = 0;

	p->width = width;
	p->height = height;

	switch(format) {
		case HELIX_NV12_MODE:
		case HELIX_NV21_MODE:
			p->format = format;
			break;
		default:
			printf("Unsupported pix fmt: %x\n", format);
			ret = -EINVAL;
			break;
	}

	return ret;
}


int jpeg_encoder_alloc_workbuf(struct jpge_ctx *ctx)
{
	struct ingenic_venc_ctx *venc_ctx = ctx->priv;
	int ret = 0;

	ctx->desc = JZMalloc(0x1000, ctx->vdma_chain_len);
	ctx->desc_pa = (unsigned int)get_phy_addr(ctx->desc);
	if(!ctx->desc) {
		printf("Failed to alloc desc memory!\n");
		ret = -ENOMEM;
		goto err_desc;
	}


	return ret;
err_desc:
	return ret;
}

int jpeg_encoder_free_workbuf(struct jpge_ctx *ctx)
{
	struct ingenic_venc_ctx *venc_ctx = ctx->priv;
	free(ctx->desc);

	return 0;
}


int jpeg_encoder_init(struct jpge_ctx *ctx)
{
	struct jpge_params *p = NULL;
	_JPEGE_SliceInfo *s = NULL;

	s = kzalloc(sizeof(_JPEGE_SliceInfo));
	if(!s) {
		return -ENOMEM;
	}

	p = &ctx->p;
	p->compr_quality = 0;
	ctx->s = s;
	ctx->vdma_chain_len = 40960 + 256;

	return 0;
}


int jpeg_encoder_deinit(struct jpge_ctx *ctx)
{
	if(!ctx)
		return 0;

	if(ctx->s) {
		free(ctx->s);
	}


	return 0;
}

void jpeg_encoder_set_priv(struct jpge_ctx *ctx, void *data)
{
	ctx->priv = data;
}
