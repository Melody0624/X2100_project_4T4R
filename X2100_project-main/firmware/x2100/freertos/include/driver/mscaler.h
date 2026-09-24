#ifndef _MSCALER_H_
#define _MSCALER_H_

/*
 * 31    23    15    7    0
 *     A     R    G    B
 * MSCALER_FORMAT_BGRA_8888
 * 当输出格式为BRGA格式时，每四个字节组成一个像素点，每个像素点的组成
 * 关系为字节0：B，1：G，2：R，3：A
*/

enum mscaler_fmt{
    MSCALER_FORMAT_NV12         = 0,
    MSCALER_FORMAT_NV21         = 1,

    MSCALER_FORMAT_BGRA_8888    = (0<<2)+2,
    MSCALER_FORMAT_GBRA_8888    = (1<<2)+2,
    MSCALER_FORMAT_RBGA_8888    = (2<<2)+2,
    MSCALER_FORMAT_BRGA_8888    = (3<<2)+2,
    MSCALER_FORMAT_GRBA_8888    = (4<<2)+2,
    MSCALER_FORMAT_RGBA_8888    = (5<<2)+2,

    MSCALER_FORMAT_ABGR_8888    = (8<<2)+2,
    MSCALER_FORMAT_AGBR_8888    = (9<<2)+2,
    MSCALER_FORMAT_ARBG_8888    = (10<<2)+2,
    MSCALER_FORMAT_ABRG_8888    = (11<<2)+2,
    MSCALER_FORMAT_AGRB_8888    = (12<<2)+2,
    MSCALER_FORMAT_ARGB_8888    = (13<<2)+2,

    MSCALER_FORMAT_BGR_565      = (0<<2)+3,
    MSCALER_FORMAT_GBR_565      = (1<<2)+3,
    MSCALER_FORMAT_RBG_565      = (2<<2)+3,
    MSCALER_FORMAT_BRG_565      = (3<<2)+3,
    MSCALER_FORMAT_GRB_565      = (4<<2)+3,
    MSCALER_FORMAT_RGB_565      = (5<<2)+3,
};

struct mscaler_param
{
    enum mscaler_fmt            src_fmt;             /* 输入图像格式 nv12 format / nv21 format */
    void                        *src_yaddr;          /* 输入y分量地址，必须 cache_align_malloc 对齐 */
    void                        *src_uvaddr;         /* 输入uv分量地址，16字节对齐*/
    unsigned int                src_xres;            /* 输入图像宽*/
    unsigned int                src_yres;            /* 输入图像高*/
    unsigned int                src_ystride;         /* 输入y行距要求，必须同时满足MSCALER_ALIGN对齐，没有要求必须设置为0 */
    unsigned int                src_uvstride;        /* 输入uv行距要求，必须同时满足MSCALER_ALIGN对齐，没有要求必须设置为0 */

    enum mscaler_fmt            dst_fmt;             /* 输出图像格式 nv12/nv21 format / rgb565 / argb8888 */
    void                        *dst_yaddr;          /* 输出y分量地址，必须 cache_align_malloc 对齐 */
    void                        *dst_uvaddr;         /* 输出uv分量地址，16字节对齐*/
    unsigned int                dst_xres;            /* 输出图像宽*/
    unsigned int                dst_yres;            /* 输出图像高*/
    unsigned int                dst_ystride;         /* 输出y行距要求，必须同时满足MSCALER_ALIGN对齐，没有要求必须设置为0 */
    unsigned int                dst_uvstride;        /* 输出uv行距要求，必须同时满足MSCALER_ALIGN对齐，没有要求必须设置为0 */
};

void mscaler_init(void);

int mscaler_convert(struct mscaler_param *ms_param);

int mscaler_align_size(void);

#endif
