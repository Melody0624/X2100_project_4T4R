#include <types.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "v4l2-controls.h"
#include "videodev2.h"
#include "helix/helix_ops.h"
#include "felix/felix_ops.h"

#define TAG "x2000-codec"

static IHAL_UINT32 felix_reftimes;
static IHAL_UINT32 helix_reftimes;


unsigned int impp_fmt_to_fourcc(IMPP_PIX_FMT fmt) {
	unsigned int fourcc = 0;
	switch(fmt) {
		case IMPP_PIX_FMT_NV12:
			fourcc = V4L2_PIX_FMT_NV12;
			break;
		case IMPP_PIX_FMT_NV21:
			fourcc = V4L2_PIX_FMT_NV21;
			break;
		case IMPP_PIX_FMT_YUV420p:
			fourcc = V4L2_PIX_FMT_YUV420;
			break;
		default:
			return IHAL_RFAILED;
	}
	return fourcc;
}


/**
 * @brief 编解码器模块的初始化，在创建编解码通道之前调用
 * @retval 0    成功
 * @retval 非0 失败
 */
IHAL_INT32 IHal_CodecInit(void)
{
	if(helix_reftimes == 0 && felix_reftimes == 0) {
		ingenic_helix_init();
		ingenic_felix_init();
	}
	return 0;
}

/**
 * @brief 编解码器模块的反初始化
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_CodecDeInit(void)
{
	if(helix_reftimes == 0 && felix_reftimes == 0) {
		ingenic_helix_deinit();
		ingenic_felix_deinit();
	}

	return 0;
}

/**
 * @brief 创建一个编码解码通道
 * @param [in] type : 要创建的编解码器类型
 * @retval IHal_CodecHandle_t 成功
 * @retval IHAL_RNULL             失败
 */

IHal_CodecHandle_t *IHal_CodecCreate(CODEC_TYPE type)
{
	void *ctx = NULL;

	IHal_CodecHandle_t *ret = NULL;
	ret = malloc(sizeof(IHal_CodecHandle_t));
	if (!ret) {
		printf("codec create failed");
		return IHAL_RNULL;
	}

	switch (type) {
		case H264_ENC:
			ctx = ingenic_helix_ctx_init(V4L2_PIX_FMT_H264, 0);
			helix_reftimes++;
			break;
		case H264_DEC:
			ctx = ingenic_felix_ctx_init();
			if(!ctx)
				printf("ingenic_felix_ctx_init error!\n");
			felix_reftimes++;
			break;
		case JPEG_ENC:
			ctx = ingenic_helix_ctx_init(V4L2_PIX_FMT_JPEG, 0);
			helix_reftimes++;
			break;
		case JPEG_DEC:
			ctx = ingenic_helix_ctx_init(V4L2_PIX_FMT_JPEG, 1);
			helix_reftimes++;
			break;
		default:
			printf("not support codec type");
			return IHAL_RNULL;
	}

	ret->codectype = type;
	ret->ctx = ctx;

	return ret;
}

/**
 * @brief 销毁一个编码解码通道
 * @param [in] handle : Codec handle
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_CodecDestroy(IHal_CodecHandle_t *handle)
{
	CODEC_TYPE type = handle->codectype;
	switch (type) {
		case H264_ENC:
		case JPEG_ENC:
		case JPEG_DEC:
			ingenic_helix_ctx_deinit(handle->ctx);
			helix_reftimes--;
			break;
		case H264_DEC:
			ingenic_felix_ctx_deinit(handle->ctx);
			felix_reftimes--;
			break;
		default:
			printf("not support codec type");
			return IHAL_RFAILED;

	}

	free(handle);

	return IHAL_ROK;
}

/**
 * @brief 设置编解码器的参数，需要在编解码器启动之前调用一次
 * @param [in] handle : Codec handle
 * @param [in] param  : 要设置的编解码器参数
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_SetParams(IHal_CodecHandle_t *handle, IHal_CodecParam *param)
{

	IHal_H264E_Param h264e_param = param->codecparam.h264e_param;
	IHal_JpegEnc_Param jpegenc_param = param->codecparam.jpegenc_param;
	IHal_JpegDec_Param jpegdec_param = param->codecparam.jpegdec_param;
	IHal_H264D_Param h264d_param = param->codecparam.h264d_param;
	int width = 0;
	int height = 0;
	unsigned int fourcc = 0;

	/*param set*/
	switch(handle->codectype) {
		case H264_ENC:
			ingenic_vcodec_helix_set_param(handle->ctx, V4L2_CID_MPEG_VIDEO_BITRATE, h264e_param.max_bitrate);
			ingenic_vcodec_helix_set_param(handle->ctx, V4L2_CID_MPEG_VIDEO_HEADER_MODE, V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME);
			ingenic_vcodec_helix_set_param(handle->ctx, V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP, h264e_param.IFrameQp);
			ingenic_vcodec_helix_set_param(handle->ctx, V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP, h264e_param.PFrameQp);
			ingenic_vcodec_helix_set_param(handle->ctx, V4L2_CID_MPEG_VIDEO_H264_MIN_QP, h264e_param.mini_Qp);
			ingenic_vcodec_helix_set_param(handle->ctx, V4L2_CID_MPEG_VIDEO_H264_MAX_QP, h264e_param.max_Qp);
			ingenic_vcodec_helix_set_param(handle->ctx, V4L2_CID_MPEG_VIDEO_H264_PROFILE, V4L2_MPEG_VIDEO_H264_PROFILE_MAIN);
			ingenic_vcodec_helix_set_param(handle->ctx, V4L2_CID_MPEG_VIDEO_H264_LEVEL, V4L2_MPEG_VIDEO_H264_LEVEL_3_0);
			ingenic_vcodec_helix_set_param(handle->ctx, V4L2_CID_MPEG_VIDEO_GOP_SIZE, h264e_param.gop_len);
			ingenic_vcodec_helix_set_param(handle->ctx, V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE, 1);
			ingenic_vcodec_helix_set_param(handle->ctx, V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE, 0);

			switch(h264e_param.rc_mode) {
				case IMPP_ENC_RC_MODE_VBR:
					ingenic_vcodec_helix_set_param(handle->ctx, V4L2_CID_MPEG_VIDEO_BITRATE_MODE, V4L2_MPEG_VIDEO_BITRATE_MODE_VBR);
					break;
				case IMPP_ENC_RC_MODE_FIXQP:
					ingenic_vcodec_helix_set_param(handle->ctx, V4L2_CID_MPEG_VIDEO_BITRATE_MODE, V4L2_MPEG_VIDEO_BITRATE_MODE_CQ);
					break;
				case IMPP_ENC_RC_MODE_CBR:
				default:
					ingenic_vcodec_helix_set_param(handle->ctx, V4L2_CID_MPEG_VIDEO_BITRATE_MODE, V4L2_MPEG_VIDEO_BITRATE_MODE_CBR);
					break;
			}

			width = h264e_param.src_width;
			height = h264e_param.src_height;
			fourcc = impp_fmt_to_fourcc(h264e_param.src_fmt);

			ingenic_vcodec_helix_set_fmt_out(handle->ctx, width, height, fourcc);
			ingenic_vcodec_helix_set_fmt_cap(handle->ctx, width, height, V4L2_PIX_FMT_H264);
			break;
		case H264_DEC:
			width = h264d_param.src_width;
			height = h264d_param.src_height;
			switch(h264d_param.dst_fmt) {
				case IMPP_PIX_FMT_NV12:
					ingenic_vcodec_felix_set_param(handle->ctx, width, height, 2/*VPU_FORMAT_NV12*/);
					break;
				case IMPP_PIX_FMT_NV21:
					ingenic_vcodec_felix_set_param(handle->ctx, width, height, 3/*VPU_FORMAT_NV21*/);
					break;
				default:
					printf("dst_fmt type(%d) error ", h264d_param.dst_fmt);
					return IHAL_RFAILED;
			}
			break;
		case JPEG_ENC:
			ingenic_vcodec_helix_set_param(handle->ctx, V4L2_CID_JPEG_COMPRESSION_QUALITY, jpegenc_param.quality);

			width = jpegenc_param.src_width;
			height = jpegenc_param.src_height;
			fourcc = impp_fmt_to_fourcc(jpegenc_param.src_fmt);

			ingenic_vcodec_helix_set_fmt_out(handle->ctx, width, height, fourcc);
			ingenic_vcodec_helix_set_fmt_cap(handle->ctx, width, height, V4L2_PIX_FMT_JPEG);
			break;
		case JPEG_DEC:
			width = jpegdec_param.src_width;
			height = jpegdec_param.src_height;
			fourcc = impp_fmt_to_fourcc(jpegdec_param.dst_fmt);

			ingenic_vcodec_helix_set_fmt_out(handle->ctx, width, height, V4L2_PIX_FMT_JPEG);
			ingenic_vcodec_helix_set_fmt_cap(handle->ctx, width, height, fourcc);
			break;
		default:
			break;
	}

	return IHAL_ROK;
}

/**
 * @brief 获取编解码器的参数
 * @param [in]  handle : Codec handle
 * @param [out] param  : 被设置的编解码器参数
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_GetParams(IHal_CodecHandle_t *handle, IHal_CodecParam *param)
{
	IHal_H264E_Param h264e_param = {0};
	IHal_H264D_Param h264d_param = {0};

	switch(handle->codectype) {
		case H264_ENC:
			ingenic_vcodec_helix_get_param(handle->ctx, V4L2_CID_MPEG_VIDEO_BITRATE, &h264e_param.max_bitrate);
			ingenic_vcodec_helix_get_param(handle->ctx, V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP, &h264e_param.IFrameQp);
			ingenic_vcodec_helix_get_param(handle->ctx, V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP, &h264e_param.PFrameQp);
			ingenic_vcodec_helix_get_param(handle->ctx, V4L2_CID_MPEG_VIDEO_H264_MIN_QP, &h264e_param.mini_Qp);
			ingenic_vcodec_helix_get_param(handle->ctx, V4L2_CID_MPEG_VIDEO_H264_MAX_QP, &h264e_param.max_Qp);
			ingenic_vcodec_helix_get_param(handle->ctx, V4L2_CID_MPEG_VIDEO_GOP_SIZE, &h264e_param.gop_len);

			memcpy(&param->codecparam.h264e_param, &h264e_param, sizeof(h264e_param));
			break;
		case H264_DEC:
			ingenic_vcodec_felix_get_param(handle->ctx, FELIX_FORMAT,(void *)&h264d_param.dst_fmt);
			ingenic_vcodec_felix_get_param(handle->ctx, FELIX_WIDTH,&h264d_param.src_width);
			ingenic_vcodec_felix_get_param(handle->ctx, FELIX_HEIGHT,&h264d_param.src_height);
			memcpy(&param->codecparam.h264d_param, &h264d_param, sizeof(h264d_param));
			break;
		case JPEG_ENC:
		case JPEG_DEC:
		default:
			break;
	}

	return IHAL_ROK;
}

/**
 * @brief 启动编解码器
 * @param [in] handle : Codec handle
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_Start(IHal_CodecHandle_t *handle)
{
	switch(handle->codectype) {
		case H264_ENC:
		case JPEG_ENC:
		case JPEG_DEC:
			ingenic_vcodec_helix_start(handle->ctx);
			break;
		case H264_DEC:
			ingenic_vcodec_felix_start(handle->ctx);
			break;
		default:
			break;
	}

	return IHAL_ROK;
}

/**
 * @brief 停止编解码器
 * @param [in] handle : Codec handle
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_Stop(IHal_CodecHandle_t *handle)
{
	unsigned int ret = 0;
	switch(handle->codectype) {
		case H264_ENC:
		case JPEG_ENC:
		case JPEG_DEC:
			ret = ingenic_vcodec_helix_stop(handle->ctx);
			break;
		case H264_DEC:
			ret = ingenic_vcodec_felix_stop(handle->ctx);
		default:
			break;
	}

	return ret;
}

/**
 * @brief 创建编解码源数据缓冲区
 * @param [in] handle     : Codec handle
 * @param [in] buftype    : 缓冲区类型
 * @param [in] create_num : 需要创建的缓冲区个数
 * @retval 实际创建的缓冲区个数 成功
 * @retval 错误码                            失败
 */
IHAL_INT32 IHal_Codec_CreateSrcBuffers(IHal_CodecHandle_t *handle, IMPP_BUFFER_TYPE buftype, IHAL_INT32 create_num)
{
	IHAL_INT32 ret = 0;
	switch(handle->codectype) {
		case H264_ENC:
		case JPEG_ENC:
		case JPEG_DEC:
			ret = ingenic_vcodec_helix_create_srcbuf(handle->ctx, create_num);
			break;
		case H264_DEC:
			ret = ingenic_vcodec_felix_create_srcbuf(handle->ctx, create_num);
			break;
		default:
			break;
	}

	return ret;
}

/**
 * @brief 设置编解码源数据缓冲区，用于共享缓冲区，在编解码器启动之前进行调用
 * @param [in] handle   : Codec handle
 * @param [in] index    : 缓冲区序号
 * @param [in] sharebuf : 缓冲区信息
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_SetSrcBuffer(IHal_CodecHandle_t *handle, int index, IMPP_BufferInfo_t *sharebuf)
{
	IHAL_INT32 ret = 0;
	switch(handle->codectype) {
		case H264_ENC:
		case JPEG_ENC:
		case JPEG_DEC:
			ret = ingenic_vcodec_helix_set_srcbuf(handle->ctx, index, sharebuf->vaddr, sharebuf->paddr, sharebuf->size);
			break;
		case H264_DEC:
			ret = ingenic_vcodec_felix_set_srcbuf(handle->ctx, index, sharebuf->vaddr, sharebuf->paddr);
			break;
		default:
			break;
	}

	return ret;
}

/**
 * @brief 获取编解码源数据缓冲区，用于共享缓冲区
 * @param [in]  handle   : Codec handle
 * @param [in]  index    : 缓冲区序号
 * @param [out] sharebuf : 缓冲区信息
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_GetSrcBuffer(IHal_CodecHandle_t *handle, int index, IMPP_BufferInfo_t *sharebuf)
{
	unsigned int vaddr = 0;
	unsigned int size = 0;
	unsigned int np = 0;
	unsigned int i = 0;

	switch(handle->codectype) {
		case H264_ENC:
		case JPEG_ENC:
		case JPEG_DEC:
			/*TODO num_planes*/
			np = ingenic_vcodec_helix_get_srcbuf_np(handle->ctx);

			for(i = 0; i < np; i++) {
				ingenic_vcodec_helix_get_srcbuf(handle->ctx, index, i, &vaddr, &size);
				sharebuf->mplane.vaddr[i] = vaddr;
				sharebuf->mplane.len[i] = size;
			}

			sharebuf->index = index;
			break;
		case H264_DEC:
			ingenic_vcodec_felix_get_srcbuf(handle->ctx, index, &vaddr, &size);
			sharebuf->mplane.vaddr[0] = vaddr;
			sharebuf->mplane.len[0] = size;
			sharebuf->index = index;
			sharebuf->mplane.numPlanes = 1;
			break;
		default:
			break;
	}

	return IHAL_ROK;
}

/**
 * @brief 创建编解码器输出数据缓冲区
 * @param [in] handle     : Codec handle
 * @param [in] buftype    : 缓冲区类型
 * @param [in] create_num : 需要创建的缓冲区个数
 * @retval 实际创建的缓冲区个数       成功
 * @retval 错误码                            失败
 * @attention 仅用于解码器输出缓冲区类型，编码器默认为内部缓冲区（ #IMPP_INTERNAL_BUFFER ）
 */
IHAL_INT32 IHal_Codec_CreateDstBuffer(IHal_CodecHandle_t *handle, IMPP_BUFFER_TYPE buftype, IHAL_INT32 create_num)
{
	IHAL_INT32 ret = 0;
	switch(handle->codectype) {
		case H264_ENC:
		case JPEG_ENC:
		case JPEG_DEC:
			ret = ingenic_vcodec_helix_create_destbuf(handle->ctx, create_num);
			break;
		case H264_DEC:
			ret = ingenic_vcodec_felix_create_dstbuf(handle->ctx, create_num);
			break;
		default:
			break;
	}

	return ret;
}

/**
 * @brief 设置编解码器输出数据缓冲区，用于共享缓冲区，在编解码器启动之前调用
 * @param [in] handle   : Codec handle
 * @param [in] index    : 缓冲区序号
 * @param [in] sharebuf : 缓冲区信息
 * @retval 0    成功
 * @retval 非0  失败
 * @attention 仅用于解码器缓冲区，编码器默认为内部缓冲区（ #IMPP_INTERNAL_BUFFER ）
 */
IHAL_INT32 IHal_Codec_SetDstBuffer(IHal_CodecHandle_t *handle, int index, IMPP_BufferInfo_t *sharebuf)
{
	IHAL_INT32 ret = 0;
	switch(handle->codectype) {
		case H264_ENC:
		case JPEG_ENC:
		case JPEG_DEC:
			ret = ingenic_vcodec_helix_set_destbuf(handle->ctx, index, sharebuf->vaddr, sharebuf->paddr, sharebuf->size);
			break;
		case H264_DEC:
			ret = ingenic_vcodec_felix_set_dstbuf(handle->ctx, index, sharebuf->vaddr, sharebuf->paddr, sharebuf->size);
			break;
		default:
			break;
	}

	return ret;
}

/**
 * @brief 获取编解码器输出数据缓冲区，用于共享缓冲区
 * @param [in]  handle   : Codec handle
 * @param [in]  index    : 缓冲区序号
 * @param [out] sharebuf : 缓冲区信息
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_GetDstBuffer(IHal_CodecHandle_t *handle, int index, IMPP_BufferInfo_t *sharebuf)
{
	unsigned int vaddr = 0;
	unsigned int size = 0;

	switch(handle->codectype) {
		case H264_ENC:
		case JPEG_ENC:
		case JPEG_DEC:
			/*TODO num_planes*/
			ingenic_vcodec_helix_get_destbuf(handle->ctx, index, &vaddr, &size);
			sharebuf->mplane.vaddr[0] = vaddr;
			sharebuf->mplane.len[0] = size;
			break;
		case H264_DEC:
			ingenic_vcodec_felix_get_dstbuf(handle->ctx, 0, index, &vaddr, &size);
			sharebuf->mplane.vaddr[0] = vaddr;
			sharebuf->mplane.len[0] = size;
			ingenic_vcodec_felix_get_dstbuf(handle->ctx, 1, index, &vaddr, &size);
			sharebuf->mplane.vaddr[1] = vaddr;
			sharebuf->mplane.len[1] = size;

			break;
		default:
			break;
	}

	return IHAL_ROK;
}

/**
 * @brief 等待编解码器源数据缓冲区可用
 * @param [in] handle    : Codec handle
 * @param [in] wait_type : 等待类型（ #IMPP_NO_WAIT 或 #IMPP_WAIT_FOREVER ）
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_WaitSrcAvailable(IHal_CodecHandle_t *handle, IHAL_INT32 wait_type)
{
	unsigned int ret = 0;

	switch(handle->codectype) {
		case H264_ENC:
		case JPEG_ENC:
		case JPEG_DEC:
			if(wait_type == IMPP_WAIT_FOREVER)
				ret = ingenic_vcodec_helix_srcbuf_wait(handle->ctx, 1);
			else
				ret = ingenic_vcodec_helix_srcbuf_wait(handle->ctx, 0);
			break;
		case H264_DEC:
			if(wait_type == IMPP_WAIT_FOREVER)
				ret = ingenic_vcodec_felix_srcbuf_wait(handle->ctx, 1);
			else
				ret = ingenic_vcodec_felix_srcbuf_wait(handle->ctx, 0);
			break;
		default:
			break;
	}

	return ret;
}

/**
 * @brief 释放编解码器的源数据缓冲区
 * @param [in] handle : Codec handle
 * @param [in] buf    : 缓冲区信息
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_QueueSrcBuffer(IHal_CodecHandle_t *handle, IMPP_BufferInfo_t *buf)
{
	IHAL_INT32 ret = IHAL_ROK;

	switch(handle->codectype) {
		case H264_ENC:
		case JPEG_ENC:
		case JPEG_DEC:
			ret = ingenic_vcodec_helix_srcbuf_queue(handle->ctx, buf->index);
			break;
		case H264_DEC:
			ret = ingenic_vcodec_felix_srcbuf_queue(handle->ctx, buf->index);
			break;
		default:
			break;
	}

	return ret;
}

/**
 * @brief 获取编解码器的源数据缓冲区
 * @param [in]  handle : Codec handle
 * @param [out] buf    : 缓冲区信息
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_DequeueSrcBuffer(IHal_CodecHandle_t *handle, IMPP_BufferInfo_t *buf)
{
	int index = 0;
	switch(handle->codectype) {
		case H264_ENC:
		case JPEG_ENC:
		case JPEG_DEC:
			ingenic_vcodec_helix_srcbuf_dequeue(handle->ctx, &index);
			buf->index = index;
			break;
		case H264_DEC:
			ingenic_vcodec_felix_srcbuf_dequeue(handle->ctx, &index);
			buf->index = index;
			break;
		default:
			break;
	}

	return IHAL_ROK;
}

/**
 * @brief 等待编解码器输出数据缓冲区可用
 * @param [in] handle    : Codec handle
 * @param [in] wait_type : 等待类型（ #IMPP_NO_WAIT 或 #IMPP_WAIT_FOREVER ）
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_WaitDstAvailable(IHal_CodecHandle_t *handle, IHAL_INT32 wait_type)
{
	unsigned int ret = 0;

	switch(handle->codectype) {
		case H264_ENC:
		case JPEG_ENC:
		case JPEG_DEC:
			if(wait_type == IMPP_WAIT_FOREVER) {
				ret = ingenic_vcodec_helix_destbuf_wait(handle->ctx, 1);
			}
			else
				ret = ingenic_vcodec_helix_destbuf_wait(handle->ctx, 0);
			break;
		case H264_DEC:
			if(wait_type == IMPP_WAIT_FOREVER)
				ret = ingenic_vcodec_felix_dstbuf_wait(handle->ctx, 1);
			else
				ret = ingenic_vcodec_felix_dstbuf_wait(handle->ctx, 0);
			break;
		default:
			break;
	}

	return ret;
}

/**
 * @brief 归还编解码器的输出数据缓冲区
 * @param [in] handle : Codec handle
 * @param [in] buf    : 编解码器输出流信息
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_QueueDstBuffer(IHal_CodecHandle_t *handle, IHAL_CodecStreamInfo_t *buf)
{
	IHAL_INT32 ret = IHAL_ROK;

	switch(handle->codectype) {
		case H264_ENC:
		case JPEG_ENC:
		case JPEG_DEC:
			ret = ingenic_vcodec_helix_destbuf_queue(handle->ctx, buf->index);
			break;
		case H264_DEC:
			ret = ingenic_vcodec_felix_dstbuf_queue(handle->ctx, buf->index);
			break;
		default:
			break;
	}

	return ret;
}

/**
 * @brief 获取编解码器的输出数据缓冲区
 * @param [in]  handle : Codec handle
 * @param [out] buf    : 编解码器输出流信息
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_DequeueDstBuffer(IHal_CodecHandle_t *handle, IHAL_CodecStreamInfo_t *buf)
{
	int index = 0;
	unsigned int vaddr[3] = {0};
	unsigned int size[3] = {0};
	unsigned int np = 0;
	unsigned int i = 0;

	switch(handle->codectype) {
		case H264_ENC:
		case JPEG_ENC:
		case JPEG_DEC:
			np = ingenic_vcodec_helix_get_destbuf_np(handle->ctx);
			ingenic_vcodec_helix_destbuf_dequeue(handle->ctx, &index, vaddr, size);
			if(np == 1) {
				buf->index = index;
				buf->vaddr = vaddr[0];
				buf->size = size[0];
			} else {
				buf->index = index;
				for(i = 0; i < np; i++) {
					buf->mp.vaddr[i] = vaddr[i];
					buf->mp.len[i] = size[i];
				}
				buf->mp.numPlanes = np;
			}
			break;
		case H264_DEC:
			np = 2;	/* y_data and uv_data*/
			ingenic_vcodec_felix_dstbuf_dequeue(handle->ctx, np, &index, vaddr, size);
			for(i = 0; i < np; i++) {
				buf->mp.vaddr[i] = vaddr[i];
				buf->mp.len[i] = size[i];
			}
			buf->index = index;
			break;
		default:
			break;
	}

	return IHAL_ROK;
}

/**
 * @brief 设置编解码器的控制参数
 * @param [in] handle : Codec handle
 * @param [in] param  : 编解码器控制参数
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_Control(IHal_CodecHandle_t *handle, IHal_CodecControlParam_t *param)
{

	return IHAL_RFAILED;
}

