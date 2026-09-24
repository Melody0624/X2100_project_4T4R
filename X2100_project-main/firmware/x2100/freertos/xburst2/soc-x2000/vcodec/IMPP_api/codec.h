/**
 * @file codec.h
 * @author ingenic-team
 * @brief
 * @version 0.1
 * @date 2021-12-06
 *
 * @copyright Copyright (c) ingenic 2021
 *
 */
#ifndef __CODEC_H__
#define __CODEC_H__

#include "impp.h"

/**
 * @defgroup group_Codec Codec模块
 * @{
 */

/**
 * @addtogroup group_Codec_data_type 数据类型定义
 * @{
 */

/**
 * @brief 码率控制模式
 */
typedef enum __impp_rc_mode {
        IMPP_ENC_RC_MODE_FIXQP,
        IMPP_ENC_RC_MODE_CBR,
        IMPP_ENC_RC_MODE_VBR,
        IMPP_ENC_RC_MODE_CAPPED_VBR,
        IMPP_ENC_RC_MODE_CAPPED_QUALITY,
} IMPP_RC_MODE;

/**
 * @brief 编解码器类型
 */
typedef enum __codec_type {
        H264_ENC = 0x01,                                              /*!< h264编码   */
        H264_DEC = 0x02,                                              /*!< h264解码   */
        H265_ENC = 0x03,                                              /*!< h265编码   */
        H265_DEC = 0x04,                                              /*!< h265解码   */
        JPEG_ENC = 0x05,                                              /*!< jpeg编码   */
        JPEG_DEC = 0x06,                                              /*!< jpeg解码   */
        AUDIO_G711A = 0x10,
} CODEC_TYPE;

/**
 * @brief JPEG编码参数
 */
typedef struct __jpegenc_param {
        IHAL_INT32 initialQp;                               /*!< 初始量化参数     */
        IHAL_INT32 quality;                                 /*!< 质量参数         */
        IHAL_INT32 enc_width;                               /*!< 编码宽度         */
        IHAL_INT32 enc_height;                              /*!< 编码高度         */
        IHAL_INT32 src_width;                               /*!< 源图像宽度       */
        IHAL_INT32 src_height;                              /*!< 源图像高度       */
        IMPP_PIX_FMT src_fmt;                               /*!< 源图像像素格式   */
} IHal_JpegEnc_Param;

/**
 * @brief JPEG解码参数
 */
typedef struct __jpegdec_param {
	IHAL_INT32 src_width;                               /*!< 源图像宽度       */
	IHAL_INT32 src_height;                              /*!< 源图像高度       */
	IHAL_INT32 quality;                                 /*!< 质量参数         */
	IMPP_PIX_FMT dst_fmt;                               /*!< 目标像素格式     */
} IHal_JpegDec_Param;

/**
 * @brief 控制命令
 */
typedef enum {
        ENCODER_IDR_REQUEST,
} IHal_ControlCmd_t;

/**
 * @brief 编解码器控制参数
 */
typedef struct {
        IHal_ControlCmd_t cmd;                          /*!< 控制命令          */
        IHAL_UINT32 priv[0];
} IHal_CodecControlParam_t;

/**
 * @brief H264编码参数
 */
typedef struct __h264e_param {
        IMPP_RC_MODE rc_mode;                               /*!< 码率控制模式     */
        IHAL_INT32 target_bitrate;                  /*!< 目标码率         */
        IHAL_INT32 max_bitrate;                             /*!< 最大码率         */
        IHAL_INT32 gop_len;                 /*!< GOP长度          */
        IHAL_INT32 initial_Qp;                              /*!< 初始量化参数     */
        IHAL_INT32 IFrameQp;                            /*!< I帧量化参数      */
        IHAL_INT32 PFrameQp;                            /*!< P帧量化参数      */
        IHAL_INT32 mini_Qp;                                 /*!< 最小量化参数     */
        IHAL_INT32 max_Qp;                                  /*!< 最大量化参数     */
        IHAL_INT32 level;                                   /*!< 编码的Level      */
        IHAL_INT32 maxPSNR;
        IHAL_INT32 freqIDR;                                     /*!< I帧频率          */
        IHAL_INT32 maxPictureSize;
        IHAL_INT32 frameRateNum;                        /*!< 帧率分母         */
        IHAL_INT32 frameRateDen;                        /*!< 帧率分子         */
        IHAL_INT32 enc_width;                               /*!< 编码宽度         */
        IHAL_INT32 enc_height;                              /*!< 编码高度         */
        IHAL_INT32 src_width;                               /*!< 源图像宽度       */
        IHAL_INT32 src_height;                              /*!< 源图像高度       */
        IMPP_PIX_FMT src_fmt;                               /*!< 源图像像素格式   */
} IHal_H264E_Param;

/**
 * @brief H264解码参数
 */
typedef struct __h264d_param {
        IHAL_INT32 need_split;
        IHAL_INT32 src_width;                               /*!< 源图像宽度       */
        IHAL_INT32 src_height;                              /*!< 源图像高度       */
        IMPP_PIX_FMT dst_fmt;                               /*!< 目标像素格式     */
} IHal_H264D_Param;

/**
 * @brief H265编码参数
 */
typedef struct __h265e_param {
        IMPP_RC_MODE rc_mode;                           /*!< 码率控制模式     */
        IHAL_INT32 target_bitrate;                  /*!< 目标码率         */
        IHAL_INT32 max_bitrate;                             /*!< 最大码率         */
        IHAL_INT32 gop_len;                                 /*!< GOP长度          */
        IHAL_INT32 initial_Qp;                              /*!< 初始量化参数     */
        IHAL_INT32 mini_Qp;                                 /*!< 最小量化参数     */
        IHAL_INT32 max_Qp;                                  /*!< 最大量化参数     */
        IHAL_INT32 level;                                   /*!< 编码的Level      */
        IHAL_INT32 freqIDR;                                     /*!< I帧频率          */
        IHAL_INT32 maxPSNR;
        IHAL_INT32 maxPictureSize;
        IHAL_INT32 frameRateNum;                        /*!< 帧率分母         */
        IHAL_INT32 frameRateDen;                        /*!< 帧率分子         */
        IHAL_INT32 enc_width;                               /*!< 编码宽度         */
        IHAL_INT32 enc_height;                              /*!< 编码高度         */
        IHAL_INT32 src_width;                               /*!< 源图像宽度       */
        IHAL_INT32 src_height;                              /*!< 源图像高度       */
        IMPP_PIX_FMT src_fmt;                               /*!< 源图像像素格式   */
} IHal_H265E_Param;

/**
 * @brief H265解码参数
 */
typedef struct __h265d_param {
        IHAL_INT32 need_split;
        IHAL_INT32 src_width;                               /*!< 源图像宽度       */
        IHAL_INT32 src_height;                              /*!< 源图像高度       */
} IHal_H265D_Param;


/**
 * @brief 编解码器参数
 */
typedef struct __codec_param {
        CODEC_TYPE codec_type;                                                                /*!< 编解码器类型  */
        union {
                IHal_JpegEnc_Param          jpegenc_param;                          /*!< jpeg编码参数  */
                IHal_JpegDec_Param          jpegdec_param;                          /*!< jpeg解码参数  */
                IHal_H264E_Param        h264e_param;                                /*!< h264编码参数  */
                IHal_H264D_Param        h264d_param;                                /*!< h264解码参数  */
                IHal_H265E_Param        h265e_param;                                /*!< h265编码参数  */
                IHal_H265D_Param        h265d_param;                                /*!< h265解码参数  */
        } codecparam;
} IHal_CodecParam;

/**
 * @brief H264编码NALU类型
 */
typedef enum _H264E_NALU_TYPE_E {
        H264E_NALU_BSLICE = 0,
        H264E_NALU_PSLICE = 1,
        H264E_NALU_ISLICE = 2,
        H264E_NALU_IDRSLICE = 5,
        H264E_NALU_SEI = 6,
        H264E_NALU_SPS = 7,
        H264E_NALU_PPS = 8,
        H264E_NALU_BUTT
} H264E_NALU_TYPE_E;


/**
 * @brief H265编码NALU类型
 */
typedef enum _H265E_NALU_TYPE_E {
        H265E_NALU_BSLICE = 0,
        H265E_NALU_PSLICE = 1,
        H265E_NALU_ISLICE = 2,
        H265E_NALU_IDRSLICE = 19,
        H265E_NALU_VPS = 32,
        H265E_NALU_SPS = 33,
        H265E_NALU_PPS = 34,
        H265E_NALU_SEI = 39,
        H265E_NALU_BUTT
} H265E_NALU_TYPE_E;


/**
 * @brief 编码器NAL类型
 */
typedef union nal_type {
        H264E_NALU_TYPE_E h264e_nalu_type;
        H265E_NALU_TYPE_E h265e_nalu_type;
} EncoderNalType;

/**
 * @brief 码流包信息
 */
typedef struct _encoder_pack {
        unsigned int offset;                                            /*!< 偏移             */
        int length;                                                                     /*!< 码流包长度       */
        int frameEnd;                                                           /*!< 结束帧           */
        int sliceType;                                                          /*!< Slice类型        */
        EncoderNalType nalType;                                         /*!< 编码器NAL类型    */
} EncoderPack;


/**
 * @brief 码流信息
 */
typedef struct _stream_info {
        IHAL_UINT32 seq;                                                        /*!< 序列号            */
        IHAL_UINT32 packcnt;                                            /*!< 码流包个数        */
        EncoderPack *pack;                                                      /*!< 码流包信息        */
} IHAL_StreamPackInfo_t;

/**
 * @brief 编解码器输出流信息
 */
typedef struct {
        IHAL_UINT32 vaddr;                                                              /*!< 虚拟地址      */
        IHAL_UINT32 paddr;                                                              /*!< 物理地址      */
        IHAL_INT32  fd;                                                                 /*!< 用于dma-buf   */
        IHAL_UINT32 size;                                                               /*!< 码流大小      */
        IHAL_INT32  index;
        /* buffer is mplane */
        struct {
                IHAL_INT32 numPlanes;
                IHAL_INT32  fd[3];
                IHAL_UINT32 vaddr[3];
                IHAL_UINT32 len[3];
        } mp;
        IHAL_StreamPackInfo_t pack;                                             /*!< 码流信息      */
} IHAL_CodecStreamInfo_t;


/**
 * @brief Codec handle
 */
struct IHal_CodecHandle {
        CODEC_TYPE codectype;                                                   /*!< 编解码器类型   */
        void *ctx;
};

typedef struct IHal_CodecHandle IHal_CodecHandle_t;

/**
 * @}
 */

/**
 * @addtogroup group_Codec_API API定义
 * @{
 */

/**
 * @brief 编解码器模块的初始化，在创建编解码通道之前调用
 * @retval 0    成功
 * @retval 非0 失败
 */
IHAL_INT32 IHal_CodecInit(void);

/**
 * @brief 编解码器模块的反初始化
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_CodecDeInit(void);

/**
 * @brief 创建一个编码解码通道
 * @param [in] type : 要创建的编解码器类型
 * @retval IHal_CodecHandle_t 成功
 * @retval IHAL_RNULL             失败
 */
IHal_CodecHandle_t *IHal_CodecCreate(CODEC_TYPE type);

/**
 * @brief 销毁一个编码解码通道
 * @param [in] handle : Codec handle
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_CodecDestroy(IHal_CodecHandle_t *handle);

/**
 * @brief 设置编解码器的参数，需要在编解码器启动之前调用一次
 * @param [in] handle : Codec handle
 * @param [in] param  : 要设置的编解码器参数
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_SetParams(IHal_CodecHandle_t *handle, IHal_CodecParam *param);

/**
 * @brief 获取编解码器的参数
 * @param [in]  handle : Codec handle
 * @param [out] param  : 被设置的编解码器参数
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_GetParams(IHal_CodecHandle_t *handle, IHal_CodecParam *param);

/**
 * @brief 启动编解码器
 * @param [in] handle : Codec handle
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_Start(IHal_CodecHandle_t *handle);

/**
 * @brief 停止编解码器
 * @param [in] handle : Codec handle
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_Stop(IHal_CodecHandle_t *handle);

/**
 * @brief 创建编解码源数据缓冲区
 * @param [in] handle     : Codec handle
 * @param [in] buftype    : 缓冲区类型
 * @param [in] create_num : 需要创建的缓冲区个数
 * @retval 实际创建的缓冲区个数 成功
 * @retval 错误码                            失败
 */
IHAL_INT32 IHal_Codec_CreateSrcBuffers(IHal_CodecHandle_t *handle, IMPP_BUFFER_TYPE buftype, IHAL_INT32 create_num);

/**
 * @brief 设置编解码源数据缓冲区，用于共享缓冲区，在编解码器启动之前进行调用
 * @param [in] handle   : Codec handle
 * @param [in] index    : 缓冲区序号
 * @param [in] sharebuf : 缓冲区信息
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_SetSrcBuffer(IHal_CodecHandle_t *handle, int index, IMPP_BufferInfo_t *sharebuf);

/**
 * @brief 获取编解码源数据缓冲区，用于共享缓冲区
 * @param [in]  handle   : Codec handle
 * @param [in]  index    : 缓冲区序号
 * @param [out] sharebuf : 缓冲区信息
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_GetSrcBuffer(IHal_CodecHandle_t *handle, int index, IMPP_BufferInfo_t *sharebuf);

/**
 * @brief 创建编解码器输出数据缓冲区
 * @param [in] handle     : Codec handle
 * @param [in] buftype    : 缓冲区类型
 * @param [in] create_num : 需要创建的缓冲区个数
 * @retval 实际创建的缓冲区个数       成功
 * @retval 错误码                            失败
 * @attention 仅用于解码器输出缓冲区类型，编码器默认为内部缓冲区（ #IMPP_INTERNAL_BUFFER ）
 */
IHAL_INT32 IHal_Codec_CreateDstBuffer(IHal_CodecHandle_t *handle, IMPP_BUFFER_TYPE buftype, IHAL_INT32 create_num);

/**
 * @brief 设置编解码器输出数据缓冲区，用于共享缓冲区，在编解码器启动之前调用
 * @param [in] handle   : Codec handle
 * @param [in] index    : 缓冲区序号
 * @param [in] sharebuf : 缓冲区信息
 * @retval 0    成功
 * @retval 非0  失败
 * @attention 仅用于解码器缓冲区，编码器默认为内部缓冲区（ #IMPP_INTERNAL_BUFFER ）
 */
IHAL_INT32 IHal_Codec_SetDstBuffer(IHal_CodecHandle_t *handle, int index, IMPP_BufferInfo_t *sharebuf);

/**
 * @brief 获取编解码器输出数据缓冲区，用于共享缓冲区
 * @param [in]  handle   : Codec handle
 * @param [in]  index    : 缓冲区序号
 * @param [out] sharebuf : 缓冲区信息
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_GetDstBuffer(IHal_CodecHandle_t *handle, int index, IMPP_BufferInfo_t *sharebuf);

/**
 * @brief 等待编解码器源数据缓冲区可用
 * @param [in] handle    : Codec handle
 * @param [in] wait_type : 等待类型（ #IMPP_NO_WAIT 或 #IMPP_WAIT_FOREVER ）
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_WaitSrcAvailable(IHal_CodecHandle_t *handle, IHAL_INT32 wait_type);

/**
 * @brief 释放编解码器的源数据缓冲区
 * @param [in] handle : Codec handle
 * @param [in] buf    : 缓冲区信息
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_QueueSrcBuffer(IHal_CodecHandle_t *handle, IMPP_BufferInfo_t *buf);

/**
 * @brief 获取编解码器的源数据缓冲区
 * @param [in]  handle : Codec handle
 * @param [out] buf    : 缓冲区信息
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_DequeueSrcBuffer(IHal_CodecHandle_t *handle, IMPP_BufferInfo_t *buf);

/**
 * @brief 等待编解码器输出数据缓冲区可用
 * @param [in] handle    : Codec handle
 * @param [in] wait_type : 等待类型（ #IMPP_NO_WAIT 或 #IMPP_WAIT_FOREVER ）
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_WaitDstAvailable(IHal_CodecHandle_t *handle, IHAL_INT32 wait_type);

/**
 * @brief 归还编解码器的输出数据缓冲区
 * @param [in] handle : Codec handle
 * @param [in] buf    : 编解码器输出流信息
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_QueueDstBuffer(IHal_CodecHandle_t *handle, IHAL_CodecStreamInfo_t *buf);

/**
 * @brief 获取编解码器的输出数据缓冲区
 * @param [in]  handle : Codec handle
 * @param [out] buf    : 编解码器输出流信息
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_DequeueDstBuffer(IHal_CodecHandle_t *handle, IHAL_CodecStreamInfo_t *buf);

/**
 * @brief 设置编解码器的控制参数
 * @param [in] handle : Codec handle
 * @param [in] param  : 编解码器控制参数
 * @retval 0    成功
 * @retval 非0  失败
 */
IHAL_INT32 IHal_Codec_Control(IHal_CodecHandle_t *handle, IHal_CodecControlParam_t *param);

/**
 * @}
 */

/**
 * @}
 */

#endif  // __CODEC_H__
