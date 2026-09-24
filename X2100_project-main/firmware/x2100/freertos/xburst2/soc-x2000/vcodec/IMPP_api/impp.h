/**
 * @file impp.h
 * @author ingenic-team
 * @brief
 * @version 0.1
 * @date 2021-12-02
 *
 * @copyright Copyright ingenic (c) 2021
 *
 */

#ifndef __IMPP_H__
#define __IMPP_H__

/**
 * @defgroup group_IMPP 通用数据类型定义
 * @{
 */

#define IHAL_ROK        0
#define IHAL_RERR       1
#define IHAL_RFAILED    2
#define IHAL_RNULL      NULL


/* impp wait type */
#define IMPP_NO_WAIT            0                                       /*!< 非阻塞等待   */
#define IMPP_WAIT_FOREVER   0xFFFFFFFF                  /*!< 阻塞等待     */

typedef char            IHAL_INT8;
typedef unsigned char   IHAL_UINT8;
typedef short           IHAL_INT16;
typedef unsigned short  IHAL_UINT16;
typedef int             IHAL_INT32;
typedef unsigned int    IHAL_UINT32;
typedef long long             IHAL_INT64;
typedef unsigned long long    IHAL_UINT64;


/**
 * @brief 像素格式
 */
typedef enum {
        IMPP_PIX_FMT_NV12,
        IMPP_PIX_FMT_NV21,
        IMPP_PIX_FMT_NV16,
        IMPP_PIX_FMT_YUV422,
        IMPP_PIX_FMT_YUV422P,
        IMPP_PIX_FMT_I420,
        IMPP_PIX_FMT_I422,
        IMPP_PIX_FMT_YUV420p,
        IMPP_PIX_FMT_RAW8,
        IMPP_PIX_FMT_RAW10,
        IMPP_PIX_FMT_RGBA_8888,
        IMPP_PIX_FMT_RGBX_8888,
        IMPP_PIX_FMT_BGRA_8888,
        IMPP_PIX_FMT_BGRX_8888,
        IMPP_PIX_FMT_ABGR_8888,
        IMPP_PIX_FMT_ARGB_8888,
        IMPP_PIX_FMT_RGB_888,
        IMPP_PIX_FMT_BGR_888,
        IMPP_PIX_FMT_RGB_565,
        IMPP_PIX_FMT_RGB_555,
        IMPP_PIX_FMT_RGBA_5551,
        IMPP_PIX_FMT_BGRA_5551,
        IMPP_PIX_FMT_ARGB_1555,
        IMPP_PIX_FMT_HSV,

        /* Camera Raw8 */
        IMPP_PIX_FMT_SBGGR8,
        IMPP_PIX_FMT_SGBRG8,
        IMPP_PIX_FMT_SGRBG8,
        IMPP_PIX_FMT_SRGGB8,

        /* Camera Raw10 */
        IMPP_PIX_FMT_SBGGR10,
        IMPP_PIX_FMT_SGBRG10,
        IMPP_PIX_FMT_SGRBG10,
        IMPP_PIX_FMT_SRGGB10,

        /* Camera Raw12 */
        IMPP_PIX_FMT_SBGGR12,
        IMPP_PIX_FMT_SGBRG12,
        IMPP_PIX_FMT_SGRBG12,
        IMPP_PIX_FMT_SRGGB12,

        /* Y only.*/
        IMPP_PIX_FMT_GREY,
        IMPP_PIX_FMT_Y4,
        IMPP_PIX_FMT_Y6,
        IMPP_PIX_FMT_Y12,
        IMPP_PIX_FMT_Y16,

        /* YUV422. */
        IMPP_PIX_FMT_YUYV,
        IMPP_PIX_FMT_YYUV,
        IMPP_PIX_FMT_YVYU,
        IMPP_PIX_FMT_VYUY,

} IMPP_PIX_FMT;


/**
 * @brief 采样格式
 */
typedef enum {
        IMPP_SAMPLE_FMT_S8,                                     /*!< 有符号8位采样       */
        IMPP_SAMPLE_FMT_U8,                                     /*!< 无符号8位采样       */
        IMPP_SAMPLE_FMT_S16,                            /*!< 有符号16位采样      */
        IMPP_SAMPLE_FMT_U16,                            /*!< 无符号16位采样      */
        IMPP_SAMPLE_FMT_S24,                            /*!< 有符号24位采样      */
        IMPP_SAMPLE_FMT_U24,                            /*!< 无符号24位采样      */
        IMPP_SAMPLE_FMT_S32,                            /*!< 有符号32位采样      */
        IMPP_SAMPLE_FMT_U32,                            /*!< 无符号32位采样      */
} IMPP_SAMPLE_FMT_t;

/**
 * @brief 缓冲区类型
 */
typedef enum __impp_buffer_type {
        IMPP_INTERNAL_BUFFER,                               /*!< 内部缓冲区           */
        IMPP_EXT_DMABUFFER,                                 /*!< 外部dma-buf          */
        IMPP_EXT_USERBUFFER,                            /*!< 外部用户空间缓冲区   */
} IMPP_BUFFER_TYPE;


/**
 * @brief 帧信息
 */
typedef struct {
        IHAL_INT32 fd;                                          /*!< 用于dma-buf         */
        IHAL_INT32 width;                                       /*!< 宽度                */
        IHAL_INT32 height;                                      /*!< 高度                */
        IHAL_UINT32 paddr;                                      /*!< 物理地址            */
        IHAL_UINT32 vaddr;                                      /*!< 虚拟地址            */
        IHAL_UINT32 size;                                       /*!< 帧大小              */
        IMPP_PIX_FMT fmt;                                       /*!< 像素格式            */
        IHAL_INT32 index;
} IMPP_FrameInfo_t;

/**
 * @brief 缓冲区信息
 */
typedef struct {
        IHAL_INT32 fd;                                          /*!< 用于dma-buf         */
        IHAL_UINT32 paddr;                                      /*!< 物理地址            */
        IHAL_UINT32 vaddr;                                      /*!< 虚拟地址            */
        IHAL_UINT32 size;                                       /*!< 缓冲区大小          */
	IHAL_UINT32 byteused;
        IHAL_INT32 index;
        struct {
                IHAL_INT32 numPlanes;
                IHAL_INT32  fd[3];
                IHAL_UINT32 vaddr[3];
                IHAL_UINT32 len[3];
        } mplane;
} IMPP_BufferInfo_t;

/**
 * @}
 */

#endif  // __IMPP_H__
