#ifndef __JZ_VPU_REG_H__
#define __JZ_VPU_REG_H__

#define CPM_BASE 0x10000000
#define CPM_SIZE 0x00001000
/********************************************
  CPM (CPM)
*********************************************/
#define REG_CPM_SRBC 0xC4
#define CPM_VPU_SR           (0x1<<31)
#define CPM_VPU_STP          (0x1<<30)
#define CPM_VPU_ACK          (0x1<<29)
#define CPM_VPU1_SR           (0x1<<28)
#define CPM_VPU1_STP          (0x1<<27)
#define CPM_VPU1_ACK          (0x1<<26)

#define REG_CPM_CLKGR 0x20
#define REG_CPM_CLKGR1 0x28
#define CPM_CLKGR_JPEG           (0x1<<12)

#define REG_CPM_LCR 0x4


struct vpu_struct{
    unsigned int vpu_base;
    unsigned int cpm_base;
    int vpu_fd;
};

typedef struct {
    uint8_t *buf[3];            /* Y,U,V */
    uint8_t *BitStreamBuf;
#ifdef CHECK_RESULT
    uint8_t *soft_buf;
    uint8_t *soft_bts;
#endif
    unsigned int des_va;    /* descriptor virtual address */
    unsigned int des_pa;    /* descriptor physical address */
    int format;             /* HELIX_NV12_MODE,HELIX_NV21_MODE */
    int width;
    int height;
    int ql_sel;
    int bslen;
} YUYV_INFO;


struct jz_jpeg_encode{
    void *vpu;
    YUYV_INFO yuyv_info;
    unsigned int bs_size;
    unsigned int *input_yuyv;
};

/* dst jpeg buffer(yuv420) size max is w*h*1.5 */
static int inline jz_jpeg_encode_get_BitStreamBuf_size(struct jz_jpeg_encode *jz_jpeg)
{
    int size;
    size = jz_jpeg->yuyv_info.width * jz_jpeg->yuyv_info.height;
    size = (size*3)/2;
    return size;
}

/* src yuv422 buffer size max is w*h*2 */
static int inline jz_jpeg_encode_get_yuv422_buffer_size(struct jz_jpeg_encode *jz_jpeg)
{
    int size;
    size = jz_jpeg->yuyv_info.width * jz_jpeg->yuyv_info.height;
    size = size*2;
    return size;
}
static int inline jz_jpeg_encode_get_temp_yuv422_buffer_size(struct jz_jpeg_encode *jz_jpeg)
{
    return jz_jpeg_encode_get_temp_yuv422_buffer_size(jz_jpeg);
}

static int inline jz_jpeg_encode_get_y_buffer_size(struct jz_jpeg_encode *jz_jpeg)
{
    int size;
    size = jz_jpeg->yuyv_info.width * jz_jpeg->yuyv_info.height;
    return size;
}
static int inline jz_jpeg_encode_get_uv_buffer_size(struct jz_jpeg_encode *jz_jpeg)
{
    int size;
    size = jz_jpeg->yuyv_info.width * jz_jpeg->yuyv_info.height;
    size /= 2;
    return size;
}


/* void* vpu_init(); */
/* void vpu_exit(void *vpu); */
/* int jz_start_hw_compress(void *handle,unsigned int des_va,unsigned int des_pa); */
#endif    /* __JZ_VPU_REG_H__ */
