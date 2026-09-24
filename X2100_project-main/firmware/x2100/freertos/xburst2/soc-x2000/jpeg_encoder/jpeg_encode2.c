#include <common.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <asm/addrspace.h>
#include <driver/cache.h>
#include <sys/time.h>

#include <malloc.h>

#include <jpeg-hw.h>
#include <qlook.h>
#include <ht.h>
#include <head.h>
#include <jpeg_private.h>

#include <vpu_common.h>
#include "vpu_common.c"
#include "api/helix_jpeg_enc.c"

/* #define JPGE_TILE_MODE  0 */
/* #define JPGE_NV12_MODE  8 */
/* #define JPGE_NV21_MODE  12 */

static unsigned char qt_new[128];
static unsigned int qetable[128];


void *JZMalloc(int align, int size)
{
    return memalign(align, ALIGN(size, align));
}

unsigned int get_phy_addr(unsigned int vaddr)
{
    return CPHYSADDR(vaddr);
}

/*************************************************************************************
 Put the Vector Vaule in JPEG Encoder Software Struct
*************************************************************************************/
static int JPEGE_SW_Structa_Pad(JPEGE_SwInfo *swinfo, YUYV_INFO *yuyv_info)
{
    int width, height;

    width = yuyv_info->width;
    height = yuyv_info->height;

    swinfo->YBuf[0]              = yuyv_info->buf[0];
    swinfo->YBuf[1]              = yuyv_info->buf[1];
    swinfo->YBuf[2]              = yuyv_info->buf[2];
    swinfo->BitStreamBuf      = yuyv_info->BitStreamBuf;
    swinfo->des_va            = yuyv_info->des_va;//VDMA Chain first address.
    swinfo->des_pa            = yuyv_info->des_pa;
    swinfo->InDaMd            = HELIX_NV21_MODE;
    swinfo->nmcu              = width * height / 256 - 1;
    swinfo->nrsm              = 0;
    swinfo->rsm               = 0;
    swinfo->ncol              = 2; /* yuv(3) - 1 */
    swinfo->ql_sel            = yuyv_info->ql_sel;
    swinfo->huffenc_sel       = 0;
    swinfo->width             = width;
    swinfo->height            = height;
    swinfo->format            = yuyv_info->format;
    return 0;
}

/*************************************************************************************
 Padding JPEG Encoder Software and Hardware used Struct
*************************************************************************************/
//static int jpge_fill_slice_info(struct jpge_ctx *ctx)
static int JPEGE_Struct_Pad(_JPEGE_SliceInfo *s, JPEGE_SwInfo *swinfo)
{
    //struct jpge_params *p = &ctx->p;
    //_JPEGE_SliceInfo *s = ctx->s;
    int ret = 0;
    int width, height;

    width = swinfo->width;
    height = swinfo->height;

    s->des_va      = (uint32_t *)swinfo->des_va;
    s->des_pa      = (uint32_t *)swinfo->des_pa;

    s->ncol = 2;	/* unused? */
    s->rsm = 0;
    s->bsa = get_phy_addr((uint32_t)swinfo->BitStreamBuf);
    s->p0a = 0;
    s->p1a = 0;
    s->nrsm = 0;

    s->raw[0] = get_phy_addr((uint32_t)swinfo->YBuf[0]);	/*Y*/
    s->raw[1] = get_phy_addr((uint32_t)swinfo->YBuf[1]);	/*U for 420p or UV for nv12*/
    s->raw[2] = get_phy_addr((uint32_t)swinfo->YBuf[2]);	/*V for 420p*/

    s->stride[0] = s->stride[1] = width;

    s->mb_height = (height + 15) / 16;
    s->mb_width = width / 16;
    s->nmcu = s->mb_height * s->mb_width - 1;
    s->raw_format = swinfo->format; /* NV12/NV21 */
    s->ql_sel = swinfo->ql_sel;
    s->huffenc_sel = 0;	/*only one huffenc table?*/

    return ret;
}

static int jpge_struct_init(struct jz_jpeg_encode *jz_jpeg)
{
    _JPEGE_SliceInfo vpu_s;
    JPEGE_SwInfo swinfo;
    YUYV_INFO *yuyv_info = &jz_jpeg->yuyv_info;
    /* 1. Put the vector value in swinfo */
    /*printf("sw struct padding\n");*/
    JPEGE_SW_Structa_Pad(&swinfo, yuyv_info);

    /*printf("struct padding\n");*/
    /* 2. Padding the JPEG Encoder API struct and swinfo */
    JPEGE_Struct_Pad(&vpu_s, &swinfo);

    /* 3. Write the JPEG Encoder VDMA chain */
    /*printf("write vdma chn\n");*/
    JPEGE_Legacy_SliceInit(&vpu_s, qetable);

    /* 6. refresh cache */
    /*printf("refresh cache\n");*/
    return 0;
}

void* hw_jpeg_encode_init(int width,int height)
{
    struct jz_jpeg_encode *jz_jpeg;
    jz_jpeg = malloc(sizeof(struct jz_jpeg_encode));
    jz_jpeg->vpu = vpu_init();

    if(!jz_jpeg->vpu){
        fprintf(stderr,"VPU init failure!\n");
    }
    /* 	ctx->vdma_chain_len = 40960 + 256; */
    jz_jpeg->yuyv_info.des_va = (unsigned int)JZMalloc(JPEG_DMA_DESC_ALIGN, JPEG_DMA_DESC_SIZE);/*The descriptor actual size required is 5000-6000*/

    if(!jz_jpeg->yuyv_info.des_va)
        fprintf(stderr,"Alloc jz_jpeg->yuyv_info.des_va memory failure!\n");
    jz_jpeg->yuyv_info.width = width;
    jz_jpeg->yuyv_info.height = height;
    jz_jpeg->yuyv_info.BitStreamBuf = JZMalloc(JPEG_DMA_DESC_ALIGN, jz_jpeg_encode_get_BitStreamBuf_size(jz_jpeg));
    if(!jz_jpeg->yuyv_info.BitStreamBuf)
        fprintf(stderr,"Alloc jz_jpeg->yuyv_info.BitStreamBuf memory failure!\n");
#if 0
    /* temp_buf YUV422 buffer for YUV420 src */
    jz_jpeg->input_yuyv = (unsigned int *)JZMalloc(128,width*height*2);
    if(!jz_jpeg->input_yuyv)
        fprintf(stderr,"Alloc input buffer fail!\n");
#else
    jz_jpeg->input_yuyv = NULL;
#endif
    printf("jz_jpeg->yuyv_info.des_va=%p\n", (void *)jz_jpeg->yuyv_info.des_va);
    printf("jz_jpeg->yuyv_info.BitStreamBuf=%p\n", (void *)jz_jpeg->yuyv_info.BitStreamBuf);
    printf("jz_jpeg->input_yuyv=%p\n", (void *)jz_jpeg->input_yuyv);

    return (void *)jz_jpeg;
}


void hw_jpeg_encode_deinit(void *handle){
    struct jz_jpeg_encode *jz_jpeg = (struct jz_jpeg_encode *)handle;

    vpu_exit(jz_jpeg->vpu);
    free(jz_jpeg);

    /* jz47_free_alloc_mem(); */
    return;
}

static int copy_image(struct jz_jpeg_encode *jz_jpeg,unsigned char *buf)
{
    int i, j;
    char *ptr;
    YUYV_INFO *yuyv_info = &jz_jpeg->yuyv_info;
    int width = yuyv_info->width;
    int height = yuyv_info->height;
    unsigned char *bs = yuyv_info->BitStreamBuf;
    int bs_len = jz_jpeg->bs_size;
    int count = 0;
    /*************** SOI *****************/
    buf[count++] = 0xFF;
    buf[count++] = M_SOI;
    /************** DQT --0 **************/
    buf[count++] = 0xFF;
    buf[count++] = M_DQT;
    //Lq=64*2+3
    buf[count++] = 0x0;
    buf[count++] = 0x43;
    //Pq=0 Tq=0
    buf[count++] = 0x00;
    //Qk
    ptr = (char *)qt_new;
    for(i=0; i<64; i++)
        buf[count++] = *ptr++;

    /************** DQT --1 *************/
    buf[count++] = 0xFF;
    buf[count++] = M_DQT;
    //Lq=64*2+3
    buf[count++] = 0x0;
    buf[count++] = 0x43;
    //Pq=0 Tq=1
    buf[count++] = 0x01;
    //Qk
    for(i=0; i<64; i++)
        buf[count++] = *ptr++;

    /************* SOF ***************/
    buf[count++] = 0xFF;
    buf[count++] = M_SOF0;
    //Lf=17
    buf[count++] = 0x0;
    buf[count++] = 0x11;
    //P=8 --> 8-bit sample
    buf[count++] = 0x8;
    //Y=height
    buf[count++] = (height & 0xFF00)>>8;
    buf[count++] = height & 0xFF;
    //X=width
    buf[count++] = (width & 0xFF00)>>8;
    buf[count++] = width & 0xFF;
    //Nf=3 (number of components)
    buf[count++] = 0x3;
    //C1 --> Y
    buf[count++] = 0x1;
    //H=2,V=2
    buf[count++] = 0x22;
    //Tq --> QT0
    buf[count++] = 0x0;
    //C2 --> U
    buf[count++] = 0x2;
    //H=1,V=1
    buf[count++] = 0x11;
    //Tq --> QT1
    buf[count++] = 0x1;
    //C3 --> V
    buf[count++] = 0x3;
    //H=1,V=1
    buf[count++] = 0x11;
    //Tq --> QT1
    buf[count++] = 0x1;
    /**************** DHT **************/
    for(j=0; j<4; j++){
        buf[count++] = 0xFF;
        buf[count++] = M_DHT;
        //Lh
        int size = ht_size[j];
        buf[count++] = (size & 0xFF00)>>8;
        buf[count++] = size & 0xFF;
        //Tc Th
        buf[count++] = dht_sel[j];
        //Li
        for(i=0; i<16; i++)
            buf[count++] = ht_len[j][i];
        //Vij
        for(i=0; i<16; i++){
            int m;
            for(m=0; m<ht_len[j][i]; m++)
                buf[count++] = ht_val[j][i][m];
        }
    }
#if 0
    /*添加0xff,填充header到256字节对齐. 已知SOS 段14字节*/
    header_size = pbuf - (char *)bs->va + 14;
    padsize = 256 - header_size % 256;

    memset(pbuf, 0xff, padsize);
    pbuf += padsize;
#else
	int header_size;
	int padsize;
    /*添加0xff,填充header到256字节对齐. 已知SOS 段14字节*/
    header_size = count + 14;
    padsize = 256 - header_size % 256;
    printf("", count, padsize);
    memset(&buf[count], 0xff, padsize);
    count += padsize;
#endif
    /************** SOS ****************/
    buf[count++] = 0xFF;
    buf[count++] = M_SOS;
    //Ls = 12
    buf[count++] = 0x0;
    buf[count++] = 0xC;
    //Ns
    buf[count++] = 0x3;
    //Cs1 - Y
    buf[count++] = 0x1;
    //Td, Ta
    buf[count++] = 0x00;
    //Cs2 - U
    buf[count++] = 0x2;
    //Td, Ta
    buf[count++] = 0x11;
    //Cs3 - V
    buf[count++] = 0x3;
    //Td, Ta
    buf[count++] = 0x11;
    //Ss
    buf[count++] = 0x00;
    //Se
    buf[count++] = 0x3f;
    //Ah, Al
    buf[count++] = 0x0;
    memcpy(&buf[count],bs,bs_len);
    buf[count + bs_len] = 255;
    buf[count + bs_len + 1] = 217;
    return count + bs_len + 2;
}


static int genhead(FILE *fp, YUYV_INFO *yuyv_info)
{
    int i, j;
    char *ptr;
    int width = yuyv_info->width;
    int height = yuyv_info->height;
    /*************** SOI *****************/
    fputc(0xFF, fp);
    fputc(M_SOI, fp);
    /************** DQT --0 **************/
    fputc(0xFF, fp);
    fputc(M_DQT, fp);
    //Lq=64*2+3
    fputc(0x0, fp);
    fputc(0x43, fp);
    //Pq=0 Tq=0
    fputc(0x00, fp);
    //Qk
    ptr = (char *)qt_new;
    for(i=0; i<64; i++)
        fputc(*ptr++, fp);

    /************** DQT --1 *************/
    fputc(0xFF, fp);
    fputc(M_DQT, fp);
    //Lq=64*2+3
    fputc(0x0, fp);
    fputc(0x43, fp);
    //Pq=0 Tq=1
    fputc(0x01, fp);
    //Qk
    for(i=0; i<64; i++)
        fputc(*ptr++, fp);

    /************* SOF ***************/
    fputc(0xFF, fp);
    fputc(M_SOF0, fp);
    //Lf=17
    fputc(0x0, fp);
    fputc(0x11, fp);
    //P=8 --> 8-bit sample
    fputc(0x8, fp);
    //Y=height
    fputc((height & 0xFF00)>>8, fp);
    fputc(height & 0xFF, fp);
    //X=width
    fputc((width & 0xFF00)>>8, fp);
    fputc(width & 0xFF, fp);
    //Nf=3 (number of components)
    fputc(0x3, fp);
    //C1 --> Y
    fputc(0x1, fp);
    //H=2,V=2
    fputc(0x22, fp);
    //Tq --> QT0
    fputc(0x0, fp);
    //C2 --> U
    fputc(0x2, fp);
    //H=1,V=1
    fputc(0x11, fp);
    //Tq --> QT1
    fputc(0x1, fp);
    //C3 --> V
    fputc(0x3, fp);
    //H=1,V=1
    fputc(0x11, fp);
    //Tq --> QT1
    fputc(0x1, fp);
    /**************** DHT **************/
    for(j=0; j<4; j++){
        fputc(0xFF, fp);
        fputc(M_DHT, fp);
        //Lh
        int size = ht_size[j];
        fputc((size & 0xFF00)>>8, fp);
        fputc(size & 0xFF, fp);
        //Tc Th
        fputc(dht_sel[j], fp);
        //Li
        for(i=0; i<16; i++)
            fputc(ht_len[j][i], fp);
        //Vij
        for(i=0; i<16; i++){
            int m;
            for(m=0; m<ht_len[j][i]; m++)
                fputc(ht_val[j][i][m], fp);
        }
    }
    /************** SOS ****************/
    fputc(0xFF, fp);
    fputc(M_SOS, fp);
    //Ls = 12
    fputc(0x0, fp);
    fputc(0xC, fp);
    //Ns
    fputc(0x3, fp);
    //Cs1 - Y
    fputc(0x1, fp);
    //Td, Ta
    fputc(0x00, fp);
    //Cs2 - U
    fputc(0x2, fp);
    //Td, Ta
    fputc(0x11, fp);
    //Cs3 - V
    fputc(0x3, fp);
    //Td, Ta
    fputc(0x11, fp);
    //Ss
    fputc(0x00, fp);
    //Se
    fputc(0x3f, fp);
    //Ah, Al
    fputc(0x0, fp);

    return 0;
}

int gen_image(YUYV_INFO *yuyv_info, FILE *fpo, int bs_len)
{
    unsigned char *bs = yuyv_info->BitStreamBuf;

    genhead(fpo, yuyv_info);
    fwrite(bs, 1, bs_len, fpo);
    fputc(255, fpo);
    fputc(217, fpo);
    fflush(fpo);
    return 0;
}

/*
 *        int gettimeofday(struct timeval *tv, struct timezone *tz);
 */
long long int get_sys_time_ms(void)
{
    struct timeval tv;
    long long int ms;
    gettimeofday(&tv, NULL);
    ms = tv.tv_sec * 1000;
    ms += tv.tv_usec / 1000;
    //printf("****** tv.tv_sec=%d, tv.tv_usec=%d\n", tv.tv_sec, tv.tv_usec);
    return ms;
}

#if 0
static void convert_yuv420p_yuv422(unsigned char *dest, unsigned char *src, int width, int height)
{
    int i, j;
    unsigned char *PY420_0 = src;
    unsigned char *PY420_1 = src + width;
    unsigned char *PU420 = src + width * height;
    unsigned char *PV420 = src + width * height * 5/4;


    unsigned char *PY422_0 = dest;
    unsigned char *PY422_1 = dest + width * 2;

    for (i = 0; i < height / 2; i++)
    {
        for (j = 0;j < width * 2; j += 4)
        {
            *PY422_0++ = *PY420_0++;
            *PY422_1++ = *PY420_1++;
            *PY422_0++ = *PU420;
            *PY422_1++ = *PU420++;

            *PY422_0++ = *PY420_0++;
            *PY422_1++ = *PY420_1++;
            *PY422_0++ = *PV420;
            *PY422_1++ = *PV420++;


        }

        PY420_0 += width;
        PY420_1 += width;
        PY422_0 += width*2;
        PY422_1 += width*2;
    }
}

int jz_put_jpeg_yuv420p_memory(void *handle,unsigned char *dest_image, int image_size,
                               unsigned char *input_image, int width, int height, int quality)
{
    struct jz_jpeg_encode *jz_jpeg = (struct jz_jpeg_encode *)handle;
    if(!handle)
    {
        fprintf(stderr,"jz_put_jpeg_yuv420p_memor:handle isn't init or init failed!\n");
        return -1;
    }
    convert_yuv420p_yuv422((unsigned char *)jz_jpeg->input_yuyv, input_image, width, height);
    //memcpy(jz_jpeg->input_yuyv,input_image,width * height * 2);
    jz_jpeg->yuyv_info.width = width;
    jz_jpeg->yuyv_info.height = height;
    jz_jpeg->yuyv_info.ql_sel = quality;
    jz_jpeg->yuyv_info.buf[0] = (unsigned char *)jz_jpeg->input_yuyv;
    jz_jpeg->yuyv_info.des_pa = (unsigned int)get_phy_addr(jz_jpeg->yuyv_info.des_va);

    /* 2: jpge strcut init */
    jpge_struct_init(jz_jpeg);
    jz_jpeg->bs_size = jz_start_hw_compress(jz_jpeg,jz_jpeg->yuyv_info.des_va,jz_jpeg->yuyv_info.des_pa);
    return copy_image(jz_jpeg,dest_image);
}

int yuv422_to_jpeg_frame(void *handle,unsigned char *input_image, char *dst_jpeg_frame, int width, int height, int quality)
{
    long long int timea, timeb;
    struct jz_jpeg_encode *jz_jpeg = (struct jz_jpeg_encode *)handle;
    if(!handle)
    {
        fprintf(stderr,"yuv422_to_jpeg:handle isn't init or init failed!\n");
        return -1;
    }
    //jz_jpeg->yuyv_info.format = HELIX_NV12_MODE;
    jz_jpeg->yuyv_info.format = HELIX_NV21_MODE;
    jz_jpeg->yuyv_info.width = width;
    jz_jpeg->yuyv_info.height = height;
    jz_jpeg->yuyv_info.ql_sel = quality;
    jz_jpeg->yuyv_info.buf[0] = input_image;
    jz_jpeg->yuyv_info.buf[1] = input_image+(width*height);
    jz_jpeg->yuyv_info.buf[2] = input_image+(width*height)*2;;
    jz_jpeg->yuyv_info.des_pa = (unsigned int)get_phy_addr(jz_jpeg->yuyv_info.des_va);
    /* 2: jpge strcut init */
    jpge_struct_init(jz_jpeg);
    timea = get_sys_time_ms();
    jz_jpeg->bs_size = jz_start_hw_compress(jz_jpeg,jz_jpeg->yuyv_info.des_va,jz_jpeg->yuyv_info.des_pa);
    timeb = get_sys_time_ms();
    /* jpeg encoder (640x480) cost time 3 ms */
    printf("jpeg encoder (%dx%d) cost time %lld ms\n", width, height, timeb-timea);
    //gen_image(&jz_jpeg->yuyv_info, fp, jz_jpeg->bs_size);
    return copy_image(jz_jpeg, dst_jpeg_frame);
}


int jz_put_jpeg_yuv420p_file(void *handle, FILE *fp, unsigned char *input_image, int width,
                             int height, int quality)
{
    struct jz_jpeg_encode *jz_jpeg = (struct jz_jpeg_encode *)handle;
    YUYV_INFO *yuyv_info = &jz_jpeg->yuyv_info;
    if (!handle){
        fprintf(stderr, "jz_take_to_jpeg:handle isn't init or init failed!\n");
        return -1;
    }
    convert_yuv420p_yuv422((unsigned char *)jz_jpeg->input_yuyv, input_image, width, height);
    yuv422_to_jpeg(handle, jz_jpeg->input_yuyv, fp, width, height, quality);
    return 0;
}
#endif

static unsigned char qt[128] = {
    0x08,0x06,0x06,0x07,0x06,0x05,0x08,0x07,
    0x07,0x07,0x09,0x09,0x08,0x0a,0x0c,0x14,
    0x0d,0x0c,0x0b,0x0b,0x0c,0x19,0x12,0x13,
    0x0f,0x14,0x1d,0x1a,0x1f,0x1e,0x1d,0x1a,
    0x1c,0x1c,0x20,0x24,0x2e,0x27,0x20,0x22,
    0x2c,0x23,0x1c,0x1c,0x28,0x37,0x29,0x2c,
    0x30,0x31,0x34,0x34,0x34,0x1f,0x27,0x39,
    0x3d,0x38,0x32,0x3c,0x2e,0x33,0x34,0x32,
    0x09,0x09,0x09,0x0c,0x0b,0x0c,0x18,0x0d,
    0x0d,0x18,0x32,0x21,0x1c,0x21,0x32,0x32,
    0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,
    0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,
    0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,
    0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,
    0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,
    0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,
};



static void jpeg_gen_qt(float qt_factor)
{

    int i;
    static float save_qt_factor = -1;

    if (qt_factor == save_qt_factor)
        return;

    save_qt_factor = qt_factor;

    for (i = 0; i < ARRAY_SIZE(qt); i++){
        qt_new[i] = (int) (qt[i]*qt_factor+0.5);
        qetable[i] = qlook[qt_new[i]];
    }
}

int yuv_image_to_jpeg_frame(void *handle, unsigned char *dst_jpeg_frame, unsigned char *buf0, unsigned char *buf1, unsigned char *buf2, int width, int height, int format, float quality)
{
    // long long int timea, timeb;
    struct jz_jpeg_encode *jz_jpeg = (struct jz_jpeg_encode *)handle;
    if(!handle)
    {
        fprintf(stderr,"yuv422_to_jpeg:handle isn't init or init failed!\n");
        return -1;
    }
    //jz_jpeg->yuyv_info.format = HELIX_NV12_MODE;
    //jz_jpeg->yuyv_info.format = HELIX_NV21_MODE;
    jz_jpeg->yuyv_info.format = format;
    jz_jpeg->yuyv_info.width = width;
    jz_jpeg->yuyv_info.height = height;
    jz_jpeg->yuyv_info.ql_sel = quality;
    jz_jpeg->yuyv_info.buf[0] = buf0;
    jz_jpeg->yuyv_info.buf[1] = buf1;
    jz_jpeg->yuyv_info.buf[2] = buf2;
    jz_jpeg->yuyv_info.des_pa = (unsigned int)get_phy_addr(jz_jpeg->yuyv_info.des_va);

    jpeg_gen_qt(quality);

    /* 2: jpge strcut init */
    jpge_struct_init(jz_jpeg);
    // timea = get_sys_time_ms();
    jz_jpeg->bs_size = jz_start_hw_compress(jz_jpeg,jz_jpeg->yuyv_info.des_va,jz_jpeg->yuyv_info.des_pa);
    // timeb = get_sys_time_ms();
    /* jpeg encoder (640x480) cost time 3 ms */
    // printf("%s() jpeg encoder (%dx%d) cost time %lld ms\n", __func__, width, height, timeb-timea);
    //gen_image(&jz_jpeg->yuyv_info, fp, jz_jpeg->bs_size);
    return copy_image(jz_jpeg, dst_jpeg_frame);

}


int hw_yuv420_planar_nv12_to_jpeg_frame(void *handle, unsigned char *dst_jpeg_frame, unsigned char *ybuf, unsigned char *uvbuf, int width, int height, float quality)
{
    int format;
    format = HELIX_NV12_MODE;
    if (!uvbuf)
        uvbuf = ybuf + width*height;
    return yuv_image_to_jpeg_frame(handle, dst_jpeg_frame, ybuf, uvbuf, NULL, width,  height, format, quality);
}

int hw_yuv420_planar_nv21_to_jpeg_frame(void *handle, unsigned char *dst_jpeg_frame, unsigned char *ybuf, unsigned char *uvbuf, int width, int height, float quality)
{
    int format;
    format = HELIX_NV21_MODE;
    if (!uvbuf)
        uvbuf = ybuf + width*height;
    return yuv_image_to_jpeg_frame(handle, dst_jpeg_frame, ybuf, uvbuf, NULL, width,  height, format, quality);
}
