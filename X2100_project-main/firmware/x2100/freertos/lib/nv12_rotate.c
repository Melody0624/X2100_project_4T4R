/**
 *  A program to rotate the .nv12 picture 90 degrees counter clockwise.(left rotate)
 *  The width and height of nv12 file should be divisible by 32.
 *  SRC_WIDTH: the width of input
 *  DST_WIDTH: the height of inout
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>
#include <errno.h>

#define ROTATE_LEFT_90_ONELINE_32(n,m)         \
    dst_line_0[m] = src_line_##n[31];            \
    dst_line_1[m] = src_line_##n[30];            \
    dst_line_2[m] = src_line_##n[29];            \
    dst_line_3[m] = src_line_##n[28];            \
    dst_line_4[m] = src_line_##n[27];            \
    dst_line_5[m] = src_line_##n[26];            \
    dst_line_6[m] = src_line_##n[25];            \
    dst_line_7[m] = src_line_##n[24];            \
    dst_line_8[m] = src_line_##n[23];            \
    dst_line_9[m] = src_line_##n[22];            \
    dst_line_10[m] = src_line_##n[21];          \
    dst_line_11[m] = src_line_##n[20];          \
    dst_line_12[m] = src_line_##n[19];          \
    dst_line_13[m] = src_line_##n[18];          \
    dst_line_14[m] = src_line_##n[17];          \
    dst_line_15[m] = src_line_##n[16];          \
    dst_line_16[m] = src_line_##n[15];          \
    dst_line_17[m] = src_line_##n[14];          \
    dst_line_18[m] = src_line_##n[13];          \
    dst_line_19[m] = src_line_##n[12];          \
    dst_line_20[m] = src_line_##n[11];          \
    dst_line_21[m] = src_line_##n[10];          \
    dst_line_22[m] = src_line_##n[9];         \
    dst_line_23[m] = src_line_##n[8];         \
    dst_line_24[m] = src_line_##n[7];         \
    dst_line_25[m] = src_line_##n[6];         \
    dst_line_26[m] = src_line_##n[5];         \
    dst_line_27[m] = src_line_##n[4];         \
    dst_line_28[m] = src_line_##n[3];         \
    dst_line_29[m] = src_line_##n[2];         \
    dst_line_30[m] = src_line_##n[1];         \
    dst_line_31[m] = src_line_##n[0];


#define ROTATE_LEFT_90_ONELINE_32_uv(n,m,m1)                     \
    dst_line_0[m] = src_line_##n[30];                              \
    dst_line_0[m1] = src_line_##n[31];                             \
    dst_line_1[m] = src_line_##n[28];                              \
    dst_line_1[m1] = src_line_##n[29];                             \
    dst_line_2[m] = src_line_##n[26];                              \
    dst_line_2[m1] = src_line_##n[27];                             \
    dst_line_3[m] = src_line_##n[24];                              \
    dst_line_3[m1] = src_line_##n[25];                             \
    dst_line_4[m] = src_line_##n[22];                              \
    dst_line_4[m1] = src_line_##n[23];                             \
    dst_line_5[m] = src_line_##n[20];                             \
    dst_line_5[m1] = src_line_##n[21];                            \
    dst_line_6[m] = src_line_##n[18];                             \
    dst_line_6[m1] = src_line_##n[19];                            \
    dst_line_7[m] = src_line_##n[16];                             \
    dst_line_7[m1] = src_line_##n[17];                            \
    dst_line_8[m] = src_line_##n[14];                             \
    dst_line_8[m1] = src_line_##n[15];                            \
    dst_line_9[m] = src_line_##n[12];                             \
    dst_line_9[m1] = src_line_##n[13];                            \
    dst_line_10[m] = src_line_##n[10];                            \
    dst_line_10[m1] = src_line_##n[11];                           \
    dst_line_11[m] = src_line_##n[8];                            \
    dst_line_11[m1] = src_line_##n[9];                           \
    dst_line_12[m] = src_line_##n[6];                            \
    dst_line_12[m1] = src_line_##n[7];                           \
    dst_line_13[m] = src_line_##n[4];                            \
    dst_line_13[m1] = src_line_##n[5];                           \
    dst_line_14[m] = src_line_##n[2];                            \
    dst_line_14[m1] = src_line_##n[3];                           \
    dst_line_15[m] = src_line_##n[0];                            \
    dst_line_15[m1] = src_line_##n[1];


#define ROTATE_LEFT_90_ONELINE_16_uv(n,m,m1)   \
    dst_line_0[m] = src_line_##n[30];            \
    dst_line_0[m1] = src_line_##n[31];           \
    dst_line_1[m] = src_line_##n[28];            \
    dst_line_1[m1] = src_line_##n[29];           \
    dst_line_2[m] = src_line_##n[26];            \
    dst_line_2[m1] = src_line_##n[27];           \
    dst_line_3[m] = src_line_##n[24];           \
    dst_line_3[m1] = src_line_##n[25];          \
    dst_line_4[m] = src_line_##n[22];           \
    dst_line_4[m1] = src_line_##n[23];          \
    dst_line_5[m] = src_line_##n[20];          \
    dst_line_5[m1] = src_line_##n[21];         \
    dst_line_6[m] = src_line_##n[18];          \
    dst_line_6[m1] = src_line_##n[19];         \
    dst_line_7[m] = src_line_##n[16];          \
    dst_line_7[m1] = src_line_##n[17];         \
    dst_line_8[m] = src_line_##n[14];           \
    dst_line_8[m1] = src_line_##n[15];          \
    dst_line_9[m] = src_line_##n[12];           \
    dst_line_9[m1] = src_line_##n[13];          \
    dst_line_10[m] = src_line_##n[10];          \
    dst_line_10[m1] = src_line_##n[11];         \
    dst_line_11[m] = src_line_##n[8];         \
    dst_line_11[m1] = src_line_##n[9];        \
    dst_line_12[m] = src_line_##n[6];         \
    dst_line_12[m1] = src_line_##n[7];        \
    dst_line_13[m] = src_line_##n[4];         \
    dst_line_13[m1] = src_line_##n[5];        \
    dst_line_14[m] = src_line_##n[2];         \
    dst_line_14[m1] = src_line_##n[3];        \
    dst_line_15[m] = src_line_##n[0];         \
    dst_line_15[m1] = src_line_##n[1];

//---------Macro definition end---------------

/*
 * left rotate a 32x32 block for Y component
 * unsigned char *src : Pointer to the first address of source block
 * unsigned char *dst : Pointer to the first address of distination block
 */
static inline int nv12_left_rotate_90_block32(unsigned char *src, unsigned char *dst, int src_stride, int dst_stride)
{
    unsigned char *src_line_0  = src ,  *dst_line_0 =  dst ;
    unsigned char *src_line_1  = src_stride + src_line_0,  *dst_line_1 =  dst_line_0  + dst_stride;
    unsigned char *src_line_2  = src_stride + src_line_1,  *dst_line_2 =  dst_line_1  + dst_stride;
    unsigned char *src_line_3  = src_stride + src_line_2,  *dst_line_3 =  dst_line_2  + dst_stride;
    unsigned char *src_line_4  = src_stride + src_line_3,  *dst_line_4 =  dst_line_3  + dst_stride;
    unsigned char *src_line_5  = src_stride + src_line_4,  *dst_line_5 =  dst_line_4  + dst_stride;
    unsigned char *src_line_6  = src_stride + src_line_5,  *dst_line_6 =  dst_line_5  + dst_stride;
    unsigned char *src_line_7  = src_stride + src_line_6,  *dst_line_7 =  dst_line_6  + dst_stride;
    unsigned char *src_line_8  = src_stride + src_line_7,  *dst_line_8 =  dst_line_7  + dst_stride;
    unsigned char *src_line_9  = src_stride + src_line_8,  *dst_line_9 =  dst_line_8  + dst_stride;
    unsigned char *src_line_10 = src_stride + src_line_9,  *dst_line_10 = dst_line_9  + dst_stride;
    unsigned char *src_line_11 = src_stride + src_line_10, *dst_line_11 = dst_line_10 + dst_stride;
    unsigned char *src_line_12 = src_stride + src_line_11, *dst_line_12 = dst_line_11 + dst_stride;
    unsigned char *src_line_13 = src_stride + src_line_12, *dst_line_13 = dst_line_12 + dst_stride;
    unsigned char *src_line_14 = src_stride + src_line_13, *dst_line_14 = dst_line_13 + dst_stride;
    unsigned char *src_line_15 = src_stride + src_line_14, *dst_line_15 = dst_line_14 + dst_stride;
    unsigned char *src_line_16 = src_stride + src_line_15, *dst_line_16 = dst_line_15 + dst_stride;
    unsigned char *src_line_17 = src_stride + src_line_16, *dst_line_17 = dst_line_16 + dst_stride;
    unsigned char *src_line_18 = src_stride + src_line_17, *dst_line_18 = dst_line_17 + dst_stride;
    unsigned char *src_line_19 = src_stride + src_line_18, *dst_line_19 = dst_line_18 + dst_stride;
    unsigned char *src_line_20 = src_stride + src_line_19, *dst_line_20 = dst_line_19 + dst_stride;
    unsigned char *src_line_21 = src_stride + src_line_20, *dst_line_21 = dst_line_20 + dst_stride;
    unsigned char *src_line_22 = src_stride + src_line_21, *dst_line_22 = dst_line_21 + dst_stride;
    unsigned char *src_line_23 = src_stride + src_line_22, *dst_line_23 = dst_line_22 + dst_stride;
    unsigned char *src_line_24 = src_stride + src_line_23, *dst_line_24 = dst_line_23 + dst_stride;
    unsigned char *src_line_25 = src_stride + src_line_24, *dst_line_25 = dst_line_24 + dst_stride;
    unsigned char *src_line_26 = src_stride + src_line_25, *dst_line_26 = dst_line_25 + dst_stride;
    unsigned char *src_line_27 = src_stride + src_line_26, *dst_line_27 = dst_line_26 + dst_stride;
    unsigned char *src_line_28 = src_stride + src_line_27, *dst_line_28 = dst_line_27 + dst_stride;
    unsigned char *src_line_29 = src_stride + src_line_28, *dst_line_29 = dst_line_28 + dst_stride;
    unsigned char *src_line_30 = src_stride + src_line_29, *dst_line_30 = dst_line_29 + dst_stride;
    unsigned char *src_line_31 = src_stride + src_line_30, *dst_line_31 = dst_line_30 + dst_stride;


    ROTATE_LEFT_90_ONELINE_32(31,31);
    ROTATE_LEFT_90_ONELINE_32(30,30);
    ROTATE_LEFT_90_ONELINE_32(29,29);
    ROTATE_LEFT_90_ONELINE_32(28,28);
    ROTATE_LEFT_90_ONELINE_32(27,27);
    ROTATE_LEFT_90_ONELINE_32(26,26);
    ROTATE_LEFT_90_ONELINE_32(25,25);
    ROTATE_LEFT_90_ONELINE_32(24,24);
    ROTATE_LEFT_90_ONELINE_32(23,23);
    ROTATE_LEFT_90_ONELINE_32(22,22);
    ROTATE_LEFT_90_ONELINE_32(21,21);
    ROTATE_LEFT_90_ONELINE_32(20,20);
    ROTATE_LEFT_90_ONELINE_32(19,19);
    ROTATE_LEFT_90_ONELINE_32(18,18);
    ROTATE_LEFT_90_ONELINE_32(17,17);
    ROTATE_LEFT_90_ONELINE_32(16,16);
    ROTATE_LEFT_90_ONELINE_32(15,15);
    ROTATE_LEFT_90_ONELINE_32(14,14);
    ROTATE_LEFT_90_ONELINE_32(13,13);
    ROTATE_LEFT_90_ONELINE_32(12,12);
    ROTATE_LEFT_90_ONELINE_32(11,11);
    ROTATE_LEFT_90_ONELINE_32(10,10);
    ROTATE_LEFT_90_ONELINE_32(9,9);
    ROTATE_LEFT_90_ONELINE_32(8,8);
    ROTATE_LEFT_90_ONELINE_32(7,7);
    ROTATE_LEFT_90_ONELINE_32(6,6);
    ROTATE_LEFT_90_ONELINE_32(5,5);
    ROTATE_LEFT_90_ONELINE_32(4,4);
    ROTATE_LEFT_90_ONELINE_32(3,3);
    ROTATE_LEFT_90_ONELINE_32(2,2);
    ROTATE_LEFT_90_ONELINE_32(1,1);
    ROTATE_LEFT_90_ONELINE_32(0,0);
    return 0;
}

/*
 * left rotate a 16x16 block for U V component
 * unsigned char *src : Pointer to the first address of source block
 * unsigned char *dst : Pointer to the first address of distination block
 */
static inline int nv12_left_rotate_90_block16_uv(unsigned char *src, unsigned char *dst, int src_stride, int dst_remainder, int dst_align)
{
    unsigned char *src_line_0  = src ,  *dst_line_0 =  dst ;
    unsigned char *src_line_1  = src_stride +src_line_0,  *dst_line_1  = dst_line_0  + dst_remainder+dst_align;
    unsigned char *src_line_2  = src_stride +src_line_1,  *dst_line_2  = dst_line_1  + dst_remainder+dst_align;
    unsigned char *src_line_3  = src_stride +src_line_2,  *dst_line_3  = dst_line_2  + dst_remainder+dst_align;
    unsigned char *src_line_4  = src_stride +src_line_3,  *dst_line_4  = dst_line_3  + dst_remainder+dst_align;
    unsigned char *src_line_5  = src_stride +src_line_4,  *dst_line_5  = dst_line_4  + dst_remainder+dst_align;
    unsigned char *src_line_6  = src_stride +src_line_5,  *dst_line_6  = dst_line_5  + dst_remainder+dst_align;
    unsigned char *src_line_7  = src_stride +src_line_6,  *dst_line_7  = dst_line_6  + dst_remainder+dst_align;
    unsigned char *src_line_8  = src_stride +src_line_7,  *dst_line_8  = dst_line_7  + dst_remainder+dst_align;
    unsigned char *src_line_9  = src_stride +src_line_8,  *dst_line_9  = dst_line_8  + dst_align+dst_remainder;
    unsigned char *src_line_10 = src_stride +src_line_9,  *dst_line_10 = dst_line_9  + dst_align+dst_remainder;
    unsigned char *src_line_11 = src_stride +src_line_10, *dst_line_11 = dst_line_10 + dst_align+dst_remainder;
    unsigned char *src_line_12 = src_stride +src_line_11, *dst_line_12 = dst_line_11 + dst_align+dst_remainder;
    unsigned char *src_line_13 = src_stride +src_line_12, *dst_line_13 = dst_line_12 + dst_align+dst_remainder;
    unsigned char *src_line_14 = src_stride +src_line_13, *dst_line_14 = dst_line_13 + dst_align+dst_remainder;
    unsigned char *src_line_15 = src_stride +src_line_14, *dst_line_15 = dst_line_14 + dst_align+dst_remainder;

    ROTATE_LEFT_90_ONELINE_16_uv(15,30,31);
    ROTATE_LEFT_90_ONELINE_16_uv(14,28,29);
    ROTATE_LEFT_90_ONELINE_16_uv(13,26,27);
    ROTATE_LEFT_90_ONELINE_16_uv(12,24,25);
    ROTATE_LEFT_90_ONELINE_16_uv(11,22,23);
    ROTATE_LEFT_90_ONELINE_16_uv(10,20,21);
    ROTATE_LEFT_90_ONELINE_16_uv(9,18,19);
    ROTATE_LEFT_90_ONELINE_16_uv(8,16,17);
    ROTATE_LEFT_90_ONELINE_16_uv(7,14,15);
    ROTATE_LEFT_90_ONELINE_16_uv(6,12,13);
    ROTATE_LEFT_90_ONELINE_16_uv(5,10,11);
    ROTATE_LEFT_90_ONELINE_16_uv(4,8,9);
    ROTATE_LEFT_90_ONELINE_16_uv(3,6,7);
    ROTATE_LEFT_90_ONELINE_16_uv(2,4,5);
    ROTATE_LEFT_90_ONELINE_16_uv(1,2,3);
    ROTATE_LEFT_90_ONELINE_16_uv(0,0,1);
    return 0;
}

/*
 * left rotate a 32x32 block for U V component
 * unsigned char *src : Pointer to the first address of source block
 * unsigned char *dst : Pointer to the first address of distination block
 */
static inline int nv12_left_rotate_90_block32_uv(unsigned char *src, unsigned char *dst, int src_stride, int dst_align, int dst_remainder)
{
    unsigned char *src_line_0  = src ,  *dst_line_0 = dst   ;
    unsigned char *src_line_1  = src_line_0 + src_stride,  *dst_line_1  = dst_line_0  + (dst_align+dst_remainder);
    unsigned char *src_line_2  = src_line_1 + src_stride,  *dst_line_2  = dst_line_1  + (dst_align+dst_remainder);
    unsigned char *src_line_3  = src_line_2 + src_stride,  *dst_line_3  = dst_line_2  + (dst_align+dst_remainder);
    unsigned char *src_line_4  = src_line_3 + src_stride,  *dst_line_4  = dst_line_3  + (dst_align+dst_remainder);
    unsigned char *src_line_5  = src_line_4 + src_stride,  *dst_line_5  = dst_line_4  + (dst_align+dst_remainder);
    unsigned char *src_line_6  = src_line_5 + src_stride,  *dst_line_6  = dst_line_5  + (dst_align+dst_remainder);
    unsigned char *src_line_7  = src_line_6 + src_stride,  *dst_line_7  = dst_line_6  + (dst_align+dst_remainder);
    unsigned char *src_line_8  = src_line_7 + src_stride,  *dst_line_8  = dst_line_7  + (dst_align+dst_remainder);
    unsigned char *src_line_9  = src_line_8 + src_stride,  *dst_line_9  = dst_line_8  + (dst_align+dst_remainder);
    unsigned char *src_line_10 = src_line_9 + src_stride,  *dst_line_10 = dst_line_9  + (dst_align+dst_remainder);
    unsigned char *src_line_11 = src_line_10+ src_stride,  *dst_line_11 = dst_line_10 + (dst_align+dst_remainder);
    unsigned char *src_line_12 = src_line_11+ src_stride,  *dst_line_12 = dst_line_11 + (dst_align+dst_remainder);
    unsigned char *src_line_13 = src_line_12+ src_stride,  *dst_line_13 = dst_line_12 + (dst_align+dst_remainder);
    unsigned char *src_line_14 = src_line_13+ src_stride,  *dst_line_14 = dst_line_13 + (dst_align+dst_remainder);
    unsigned char *src_line_15 = src_line_14+ src_stride,  *dst_line_15 = dst_line_14 + (dst_align+dst_remainder);
    unsigned char *src_line_16 = src_line_15+ src_stride;
    unsigned char *src_line_17 = src_line_16+ src_stride;
    unsigned char *src_line_18 = src_line_17+ src_stride;
    unsigned char *src_line_19 = src_line_18+ src_stride;
    unsigned char *src_line_20 = src_line_19+ src_stride;
    unsigned char *src_line_21 = src_line_20+ src_stride;
    unsigned char *src_line_22 = src_line_21+ src_stride;
    unsigned char *src_line_23 = src_line_22+ src_stride;
    unsigned char *src_line_24 = src_line_23+ src_stride;
    unsigned char *src_line_25 = src_line_24+ src_stride;
    unsigned char *src_line_26 = src_line_25+ src_stride;
    unsigned char *src_line_27 = src_line_26+ src_stride;
    unsigned char *src_line_28 = src_line_27+ src_stride;
    unsigned char *src_line_29 = src_line_28+ src_stride;
    unsigned char *src_line_30 = src_line_29+ src_stride;
    unsigned char *src_line_31 = src_line_30+ src_stride;


    ROTATE_LEFT_90_ONELINE_32_uv(31,62,63);
    ROTATE_LEFT_90_ONELINE_32_uv(30,60,61);
    ROTATE_LEFT_90_ONELINE_32_uv(29,58,59);
    ROTATE_LEFT_90_ONELINE_32_uv(28,56,57);
    ROTATE_LEFT_90_ONELINE_32_uv(27,54,55);
    ROTATE_LEFT_90_ONELINE_32_uv(26,52,53);
    ROTATE_LEFT_90_ONELINE_32_uv(25,50,51);
    ROTATE_LEFT_90_ONELINE_32_uv(24,48,49);
    ROTATE_LEFT_90_ONELINE_32_uv(23,46,47);
    ROTATE_LEFT_90_ONELINE_32_uv(22,44,45);
    ROTATE_LEFT_90_ONELINE_32_uv(21,42,43);
    ROTATE_LEFT_90_ONELINE_32_uv(20,40,41);
    ROTATE_LEFT_90_ONELINE_32_uv(19,38,39);
    ROTATE_LEFT_90_ONELINE_32_uv(18,36,37);
    ROTATE_LEFT_90_ONELINE_32_uv(17,34,35);
    ROTATE_LEFT_90_ONELINE_32_uv(16,32,33);
    ROTATE_LEFT_90_ONELINE_32_uv(15,30,31);
    ROTATE_LEFT_90_ONELINE_32_uv(14,28,29);
    ROTATE_LEFT_90_ONELINE_32_uv(13,26,27);
    ROTATE_LEFT_90_ONELINE_32_uv(12,24,25);
    ROTATE_LEFT_90_ONELINE_32_uv(11,22,23);
    ROTATE_LEFT_90_ONELINE_32_uv(10,20,21);
    ROTATE_LEFT_90_ONELINE_32_uv(9,18,19);
    ROTATE_LEFT_90_ONELINE_32_uv(8,16,17);
    ROTATE_LEFT_90_ONELINE_32_uv(7,14,15);
    ROTATE_LEFT_90_ONELINE_32_uv(6,12,13);
    ROTATE_LEFT_90_ONELINE_32_uv(5,10,11);
    ROTATE_LEFT_90_ONELINE_32_uv(4,8,9);
    ROTATE_LEFT_90_ONELINE_32_uv(3,6,7);
    ROTATE_LEFT_90_ONELINE_32_uv(2,4,5);
    ROTATE_LEFT_90_ONELINE_32_uv(1,2,3);
    ROTATE_LEFT_90_ONELINE_32_uv(0,0,1);

    return 0;
}

/*
 * the core function.
 * left rotate the nv12 file
 * unsigned char *nv12_data : Pointer to the first address of source file
 * unsigned char *rot90_nv12_data : Pointer to the first address of distination file
 */
// static inline int nv12_left_rotate_90(unsigned char* nv12_data,unsigned char* rot90_nv12_data)
void nv12_left_rotate_90(unsigned char* nv12_data, unsigned char* rot90_nv12_data, int src_width, int src_height)
{
    int i = 0;
    int j = 0;

    int src_stride = src_width;

    int dst_width = src_height;
    int dst_height = src_width;
    int dst_stride = dst_width;
    int dst_remainder = dst_width % 64;
    int dst_align = dst_width - dst_remainder;

    uint8_t* src=(uint8_t*)nv12_data;
    uint8_t* dst=(uint8_t*)rot90_nv12_data;
    uint8_t* src_uv=(uint8_t*)(nv12_data + src_width*src_height);
    uint8_t* dst_uv =(uint8_t*)(rot90_nv12_data + dst_width*dst_height);

    //optmize for common-use size
    //begin-----------------------------

    int width_block_num = src_width / 32; //width_block_num = src_width/32
    int height_block_num = src_height / 32; //height_block_num = dst_width/32

    unsigned char *src_block32 = NULL,*dst_block32 = NULL;
    unsigned char *src_block16 = NULL,*dst_block16 = NULL;

    //rotate Y component
     for (i = 0; i < height_block_num; i++) {
        for(j = 0; j < width_block_num; j++){
            src_block32 = src + i * 32 * src_stride + (j * 32);
            dst_block32 = dst + ((width_block_num-1-j)  * 32 ) * dst_stride + (i) * 32;

            nv12_left_rotate_90_block32(src_block32, dst_block32, src_stride, dst_stride);
        }
    }

    /////////////uv
    //rotate U,V component

   //case when dst_width is divisible by 64
      if(height_block_num % 2 == 0) {
        int height_block_num_uv = (height_block_num) / 2;

        for (i = 0; i < height_block_num_uv; i++) {
            for(j = 0; j < width_block_num; j++){
                src_block32 = src_uv + i * 32 * src_stride + j * 32;
                dst_block32 = dst_uv + ((width_block_num-1-j) * 16) * (dst_remainder+dst_align)  + (i) * 64;

                nv12_left_rotate_90_block32_uv(src_block32, dst_block32, src_stride, dst_align, dst_remainder);
            }
        }
    }

    //case when dst_width is not divisible by 64
    else{
        int height_block_num_uv = (height_block_num - 1) / 2;
         for (i = 0; i < 1; i++) {
            for(j = 0; j< width_block_num; j++){
                src_block16 = src_uv + height_block_num_uv * 32 * src_stride + j * 32;
                dst_block16 = dst_uv + ((width_block_num-1-j) * 16 ) * (dst_remainder+dst_align) + (height_block_num-1) * 32;

                nv12_left_rotate_90_block16_uv(src_block16, dst_block16, src_stride, dst_remainder, dst_align);
            }
        }

         for (i = 0; i < height_block_num_uv; i++) {
            for(j = 0; j < width_block_num; j++){
                src_block32 = src_uv + i * 32 * src_stride + j * 32;
                dst_block32 = dst_block16+32 + ((width_block_num-1-j) * 16) * (dst_remainder+dst_align) + (i) * 64;

                nv12_left_rotate_90_block32_uv(src_block32, dst_block32, src_stride, dst_align, dst_remainder);
            }
        }
    }
    //end-----------------------------
}

/*************************************************************rotate right*************************************************************************/


#define ROTATE_RIGHT_90_ONELINE_32(n,m)         \
    dst_line_0[m] = src_line_##n[0];            \
    dst_line_1[m] = src_line_##n[1];            \
    dst_line_2[m] = src_line_##n[2];            \
    dst_line_3[m] = src_line_##n[3];            \
    dst_line_4[m] = src_line_##n[4];            \
    dst_line_5[m] = src_line_##n[5];            \
    dst_line_6[m] = src_line_##n[6];            \
    dst_line_7[m] = src_line_##n[7];            \
    dst_line_8[m] = src_line_##n[8];            \
    dst_line_9[m] = src_line_##n[9];            \
    dst_line_10[m] = src_line_##n[10];          \
    dst_line_11[m] = src_line_##n[11];          \
    dst_line_12[m] = src_line_##n[12];          \
    dst_line_13[m] = src_line_##n[13];          \
    dst_line_14[m] = src_line_##n[14];          \
    dst_line_15[m] = src_line_##n[15];          \
    dst_line_16[m] = src_line_##n[16];          \
    dst_line_17[m] = src_line_##n[17];          \
    dst_line_18[m] = src_line_##n[18];          \
    dst_line_19[m] = src_line_##n[19];          \
    dst_line_20[m] = src_line_##n[20];          \
    dst_line_21[m] = src_line_##n[21];          \
    dst_line_22[m] = src_line_##n[22];         \
    dst_line_23[m] = src_line_##n[23];         \
    dst_line_24[m] = src_line_##n[24];         \
    dst_line_25[m] = src_line_##n[25];         \
    dst_line_26[m] = src_line_##n[26];         \
    dst_line_27[m] = src_line_##n[27];         \
    dst_line_28[m] = src_line_##n[28];         \
    dst_line_29[m] = src_line_##n[29];         \
    dst_line_30[m] = src_line_##n[30];         \
    dst_line_31[m] = src_line_##n[31];



#define ROTATE_RIGHT_90_ONELINE_32_uv(n,m,m1)                     \
    dst_line_0[m] = src_line_##n[0];                              \
    dst_line_0[m1] = src_line_##n[1];                             \
    dst_line_1[m] = src_line_##n[2];                              \
    dst_line_1[m1] = src_line_##n[3];                             \
    dst_line_2[m] = src_line_##n[4];                              \
    dst_line_2[m1] = src_line_##n[5];                             \
    dst_line_3[m] = src_line_##n[6];                              \
    dst_line_3[m1] = src_line_##n[7];                             \
    dst_line_4[m] = src_line_##n[8];                              \
    dst_line_4[m1] = src_line_##n[9];                             \
    dst_line_5[m] = src_line_##n[10];                             \
    dst_line_5[m1] = src_line_##n[11];                            \
    dst_line_6[m] = src_line_##n[12];                             \
    dst_line_6[m1] = src_line_##n[13];                            \
    dst_line_7[m] = src_line_##n[14];                             \
    dst_line_7[m1] = src_line_##n[15];                            \
    dst_line_8[m] = src_line_##n[16];                             \
    dst_line_8[m1] = src_line_##n[17];                            \
    dst_line_9[m] = src_line_##n[18];                             \
    dst_line_9[m1] = src_line_##n[19];                            \
    dst_line_10[m] = src_line_##n[20];                            \
    dst_line_10[m1] = src_line_##n[21];                           \
    dst_line_11[m] = src_line_##n[22];                            \
    dst_line_11[m1] = src_line_##n[23];                           \
    dst_line_12[m] = src_line_##n[24];                            \
    dst_line_12[m1] = src_line_##n[25];                           \
    dst_line_13[m] = src_line_##n[26];                            \
    dst_line_13[m1] = src_line_##n[27];                           \
    dst_line_14[m] = src_line_##n[28];                            \
    dst_line_14[m1] = src_line_##n[29];                           \
    dst_line_15[m] = src_line_##n[30];                            \
    dst_line_15[m1] = src_line_##n[31];


#define ROTATE_RIGHT_90_ONELINE_16_uv(n,m,m1)   \
    dst_line_0[m] = src_line_##n[0];            \
    dst_line_0[m1] = src_line_##n[1];           \
    dst_line_1[m] = src_line_##n[2];            \
    dst_line_1[m1] = src_line_##n[3];           \
    dst_line_2[m] = src_line_##n[4];            \
    dst_line_2[m1] = src_line_##n[5];           \
    dst_line_3[m] = src_line_##n[6];           \
    dst_line_3[m1] = src_line_##n[7];          \
    dst_line_4[m] = src_line_##n[8];           \
    dst_line_4[m1] = src_line_##n[9];          \
    dst_line_5[m] = src_line_##n[10];          \
    dst_line_5[m1] = src_line_##n[11];         \
    dst_line_6[m] = src_line_##n[12];          \
    dst_line_6[m1] = src_line_##n[13];         \
    dst_line_7[m] = src_line_##n[14];          \
    dst_line_7[m1] = src_line_##n[15];         \
    dst_line_8[m] = src_line_##n[16];           \
    dst_line_8[m1] = src_line_##n[17];          \
    dst_line_9[m] = src_line_##n[18];           \
    dst_line_9[m1] = src_line_##n[19];          \
    dst_line_10[m] = src_line_##n[20];          \
    dst_line_10[m1] = src_line_##n[21];         \
    dst_line_11[m] = src_line_##n[22];         \
    dst_line_11[m1] = src_line_##n[23];        \
    dst_line_12[m] = src_line_##n[24];         \
    dst_line_12[m1] = src_line_##n[25];        \
    dst_line_13[m] = src_line_##n[26];         \
    dst_line_13[m1] = src_line_##n[27];        \
    dst_line_14[m] = src_line_##n[28];         \
    dst_line_14[m1] = src_line_##n[29];        \
    dst_line_15[m] = src_line_##n[30];         \
    dst_line_15[m1] = src_line_##n[31];

//---------Macro definition end---------------

/*
 * right rotate a 32x32 block for Y component
 * unsigned char *src : Pointer to the first address of source block
 * unsigned char *dst : Pointer to the first address of distination block
 */
static inline int nv12_right_rotate_90_block32(unsigned char *src, unsigned char *dst, int src_stride, int dst_stride)
{
    unsigned char *src_line_0  = src ,  *dst_line_0 =  dst ;
    unsigned char *src_line_1  = src_stride +src_line_0,  *dst_line_1  = dst_line_0  + dst_stride;
    unsigned char *src_line_2  = src_stride +src_line_1,  *dst_line_2  = dst_line_1  + dst_stride;
    unsigned char *src_line_3  = src_stride +src_line_2,  *dst_line_3  = dst_line_2  + dst_stride;
    unsigned char *src_line_4  = src_stride +src_line_3,  *dst_line_4  = dst_line_3  + dst_stride;
    unsigned char *src_line_5  = src_stride +src_line_4,  *dst_line_5  = dst_line_4  + dst_stride;
    unsigned char *src_line_6  = src_stride +src_line_5,  *dst_line_6  = dst_line_5  + dst_stride;
    unsigned char *src_line_7  = src_stride +src_line_6,  *dst_line_7  = dst_line_6  + dst_stride;
    unsigned char *src_line_8  = src_stride +src_line_7,  *dst_line_8  = dst_line_7  + dst_stride;
    unsigned char *src_line_9  = src_stride +src_line_8,  *dst_line_9  = dst_line_8  + dst_stride;
    unsigned char *src_line_10 = src_stride +src_line_9,  *dst_line_10 = dst_line_9  + dst_stride;
    unsigned char *src_line_11 = src_stride +src_line_10, *dst_line_11 = dst_line_10 + dst_stride;
    unsigned char *src_line_12 = src_stride +src_line_11, *dst_line_12 = dst_line_11 + dst_stride;
    unsigned char *src_line_13 = src_stride +src_line_12, *dst_line_13 = dst_line_12 + dst_stride;
    unsigned char *src_line_14 = src_stride +src_line_13, *dst_line_14 = dst_line_13 + dst_stride;
    unsigned char *src_line_15 = src_stride +src_line_14, *dst_line_15 = dst_line_14 + dst_stride;
    unsigned char *src_line_16 = src_stride +src_line_15, *dst_line_16 = dst_line_15 + dst_stride;
    unsigned char *src_line_17 = src_stride +src_line_16, *dst_line_17 = dst_line_16 + dst_stride;
    unsigned char *src_line_18 = src_stride +src_line_17, *dst_line_18 = dst_line_17 + dst_stride;
    unsigned char *src_line_19 = src_stride +src_line_18, *dst_line_19 = dst_line_18 + dst_stride;
    unsigned char *src_line_20 = src_stride +src_line_19, *dst_line_20 = dst_line_19 + dst_stride;
    unsigned char *src_line_21 = src_stride +src_line_20, *dst_line_21 = dst_line_20 + dst_stride;
    unsigned char *src_line_22 = src_stride +src_line_21, *dst_line_22 = dst_line_21 + dst_stride;
    unsigned char *src_line_23 = src_stride +src_line_22, *dst_line_23 = dst_line_22 + dst_stride;
    unsigned char *src_line_24 = src_stride +src_line_23, *dst_line_24 = dst_line_23 + dst_stride;
    unsigned char *src_line_25 = src_stride +src_line_24, *dst_line_25 = dst_line_24 + dst_stride;
    unsigned char *src_line_26 = src_stride +src_line_25, *dst_line_26 = dst_line_25 + dst_stride;
    unsigned char *src_line_27 = src_stride +src_line_26, *dst_line_27 = dst_line_26 + dst_stride;
    unsigned char *src_line_28 = src_stride +src_line_27, *dst_line_28 = dst_line_27 + dst_stride;
    unsigned char *src_line_29 = src_stride +src_line_28, *dst_line_29 = dst_line_28 + dst_stride;
    unsigned char *src_line_30 = src_stride +src_line_29, *dst_line_30 = dst_line_29 + dst_stride;
    unsigned char *src_line_31 = src_stride +src_line_30, *dst_line_31 = dst_line_30 + dst_stride;


    ROTATE_RIGHT_90_ONELINE_32(31,0);
    ROTATE_RIGHT_90_ONELINE_32(30,1);
    ROTATE_RIGHT_90_ONELINE_32(29,2);
    ROTATE_RIGHT_90_ONELINE_32(28,3);
    ROTATE_RIGHT_90_ONELINE_32(27,4);
    ROTATE_RIGHT_90_ONELINE_32(26,5);
    ROTATE_RIGHT_90_ONELINE_32(25,6);
    ROTATE_RIGHT_90_ONELINE_32(24,7);
    ROTATE_RIGHT_90_ONELINE_32(23,8);
    ROTATE_RIGHT_90_ONELINE_32(22,9);
    ROTATE_RIGHT_90_ONELINE_32(21,10);
    ROTATE_RIGHT_90_ONELINE_32(20,11);
    ROTATE_RIGHT_90_ONELINE_32(19,12);
    ROTATE_RIGHT_90_ONELINE_32(18,13);
    ROTATE_RIGHT_90_ONELINE_32(17,14);
    ROTATE_RIGHT_90_ONELINE_32(16,15);
    ROTATE_RIGHT_90_ONELINE_32(15,16);
    ROTATE_RIGHT_90_ONELINE_32(14,17);
    ROTATE_RIGHT_90_ONELINE_32(13,18);
    ROTATE_RIGHT_90_ONELINE_32(12,19);
    ROTATE_RIGHT_90_ONELINE_32(11,20);
    ROTATE_RIGHT_90_ONELINE_32(10,21);
    ROTATE_RIGHT_90_ONELINE_32(9,22);
    ROTATE_RIGHT_90_ONELINE_32(8,23);
    ROTATE_RIGHT_90_ONELINE_32(7,24);
    ROTATE_RIGHT_90_ONELINE_32(6,25);
    ROTATE_RIGHT_90_ONELINE_32(5,26);
    ROTATE_RIGHT_90_ONELINE_32(4,27);
    ROTATE_RIGHT_90_ONELINE_32(3,28);
    ROTATE_RIGHT_90_ONELINE_32(2,29);
    ROTATE_RIGHT_90_ONELINE_32(1,30);
    ROTATE_RIGHT_90_ONELINE_32(0,31);


    return 0;
}

/*
 * right rotate a 16x16 block for U V component
 * unsigned char *src : Pointer to the first address of source block
 * unsigned char *dst : Pointer to the first address of distination block
 */
static inline int nv12_right_rotate_90_block16_uv(unsigned char *src, unsigned char *dst, int src_stride, int dst_remainder, int dst_align)
{
    unsigned char *src_line_0  = src ,  *dst_line_0 =  dst ;
    unsigned char *src_line_1  = src_stride +src_line_0,  *dst_line_1  = dst_line_0  + dst_remainder+dst_align;
    unsigned char *src_line_2  = src_stride +src_line_1,  *dst_line_2  = dst_line_1  + dst_remainder+dst_align;
    unsigned char *src_line_3  = src_stride +src_line_2,  *dst_line_3  = dst_line_2  + dst_remainder+dst_align;
    unsigned char *src_line_4  = src_stride +src_line_3,  *dst_line_4  = dst_line_3  + dst_remainder+dst_align;
    unsigned char *src_line_5  = src_stride +src_line_4,  *dst_line_5  = dst_line_4  + dst_remainder+dst_align;
    unsigned char *src_line_6  = src_stride +src_line_5,  *dst_line_6  = dst_line_5  + dst_remainder+dst_align;
    unsigned char *src_line_7  = src_stride +src_line_6,  *dst_line_7  = dst_line_6  + dst_remainder+dst_align;
    unsigned char *src_line_8  = src_stride +src_line_7,  *dst_line_8  = dst_line_7  + dst_remainder+dst_align;
    unsigned char *src_line_9  = src_stride +src_line_8,  *dst_line_9  = dst_line_8  + dst_align+dst_remainder;
    unsigned char *src_line_10 = src_stride +src_line_9,  *dst_line_10 = dst_line_9  + dst_align+dst_remainder;
    unsigned char *src_line_11 = src_stride +src_line_10, *dst_line_11 = dst_line_10 + dst_align+dst_remainder;
    unsigned char *src_line_12 = src_stride +src_line_11, *dst_line_12 = dst_line_11 + dst_align+dst_remainder;
    unsigned char *src_line_13 = src_stride +src_line_12, *dst_line_13 = dst_line_12 + dst_align+dst_remainder;
    unsigned char *src_line_14 = src_stride +src_line_13, *dst_line_14 = dst_line_13 + dst_align+dst_remainder;
    unsigned char *src_line_15 = src_stride +src_line_14, *dst_line_15 = dst_line_14 + dst_align+dst_remainder;

    ROTATE_RIGHT_90_ONELINE_16_uv(15,0,1);
    ROTATE_RIGHT_90_ONELINE_16_uv(14,2,3);
    ROTATE_RIGHT_90_ONELINE_16_uv(13,4,5);
    ROTATE_RIGHT_90_ONELINE_16_uv(12,6,7);
    ROTATE_RIGHT_90_ONELINE_16_uv(11,8,9);
    ROTATE_RIGHT_90_ONELINE_16_uv(10,10,11);
    ROTATE_RIGHT_90_ONELINE_16_uv(9,12,13);
    ROTATE_RIGHT_90_ONELINE_16_uv(8,14,15);
    ROTATE_RIGHT_90_ONELINE_16_uv(7,16,17);
    ROTATE_RIGHT_90_ONELINE_16_uv(6,18,19);
    ROTATE_RIGHT_90_ONELINE_16_uv(5,20,21);
    ROTATE_RIGHT_90_ONELINE_16_uv(4,22,23);
    ROTATE_RIGHT_90_ONELINE_16_uv(3,24,25);
    ROTATE_RIGHT_90_ONELINE_16_uv(2,26,27);
    ROTATE_RIGHT_90_ONELINE_16_uv(1,28,29);
    ROTATE_RIGHT_90_ONELINE_16_uv(0,30,31);
    return 0;
}

/*
 * right rotate a 32x32 block for U V component
 * unsigned char *src : Pointer to the first address of source block
 * unsigned char *dst : Pointer to the first address of distination block
 */
static inline int nv12_right_rotate_90_block32_uv(unsigned char *src, unsigned char *dst, int src_stride, int dst_align, int dst_remainder)
{
    unsigned char *src_line_0  = src ,  *dst_line_0 = dst   ;
    unsigned char *src_line_1  = src_line_0 + src_stride,  *dst_line_1  = dst_line_0  + (dst_align+dst_remainder);
    unsigned char *src_line_2  = src_line_1 + src_stride,  *dst_line_2  = dst_line_1  + (dst_align+dst_remainder);
    unsigned char *src_line_3  = src_line_2 + src_stride,  *dst_line_3  = dst_line_2  + (dst_align+dst_remainder);
    unsigned char *src_line_4  = src_line_3 + src_stride,  *dst_line_4  = dst_line_3  + (dst_align+dst_remainder);
    unsigned char *src_line_5  = src_line_4 + src_stride,  *dst_line_5  = dst_line_4  + (dst_align+dst_remainder);
    unsigned char *src_line_6  = src_line_5 + src_stride,  *dst_line_6  = dst_line_5  + (dst_align+dst_remainder);
    unsigned char *src_line_7  = src_line_6 + src_stride,  *dst_line_7  = dst_line_6  + (dst_align+dst_remainder);
    unsigned char *src_line_8  = src_line_7 + src_stride,  *dst_line_8  = dst_line_7  + (dst_align+dst_remainder);
    unsigned char *src_line_9  = src_line_8 + src_stride,  *dst_line_9  = dst_line_8  + (dst_align+dst_remainder);
    unsigned char *src_line_10 = src_line_9 + src_stride,  *dst_line_10 = dst_line_9  + (dst_align+dst_remainder);
    unsigned char *src_line_11 = src_line_10+ src_stride,  *dst_line_11 = dst_line_10 + (dst_align+dst_remainder);
    unsigned char *src_line_12 = src_line_11+ src_stride,  *dst_line_12 = dst_line_11 + (dst_align+dst_remainder);
    unsigned char *src_line_13 = src_line_12+ src_stride,  *dst_line_13 = dst_line_12 + (dst_align+dst_remainder);
    unsigned char *src_line_14 = src_line_13+ src_stride,  *dst_line_14 = dst_line_13 + (dst_align+dst_remainder);
    unsigned char *src_line_15 = src_line_14+ src_stride,  *dst_line_15 = dst_line_14 + (dst_align+dst_remainder);
    unsigned char *src_line_16 = src_line_15+ src_stride;
    unsigned char *src_line_17 = src_line_16+ src_stride;
    unsigned char *src_line_18 = src_line_17+ src_stride;
    unsigned char *src_line_19 = src_line_18+ src_stride;
    unsigned char *src_line_20 = src_line_19+ src_stride;
    unsigned char *src_line_21 = src_line_20+ src_stride;
    unsigned char *src_line_22 = src_line_21+ src_stride;
    unsigned char *src_line_23 = src_line_22+ src_stride;
    unsigned char *src_line_24 = src_line_23+ src_stride;
    unsigned char *src_line_25 = src_line_24+ src_stride;
    unsigned char *src_line_26 = src_line_25+ src_stride;
    unsigned char *src_line_27 = src_line_26+ src_stride;
    unsigned char *src_line_28 = src_line_27+ src_stride;
    unsigned char *src_line_29 = src_line_28+ src_stride;
    unsigned char *src_line_30 = src_line_29+ src_stride;
    unsigned char *src_line_31 = src_line_30+ src_stride;



    ROTATE_RIGHT_90_ONELINE_32_uv(31,0,1);
    ROTATE_RIGHT_90_ONELINE_32_uv(30,2,3);
    ROTATE_RIGHT_90_ONELINE_32_uv(29,4,5);
    ROTATE_RIGHT_90_ONELINE_32_uv(28,6,7);
    ROTATE_RIGHT_90_ONELINE_32_uv(27,8,9);
    ROTATE_RIGHT_90_ONELINE_32_uv(26,10,11);
    ROTATE_RIGHT_90_ONELINE_32_uv(25,12,13);
    ROTATE_RIGHT_90_ONELINE_32_uv(24,14,15);
    ROTATE_RIGHT_90_ONELINE_32_uv(23,16,17);
    ROTATE_RIGHT_90_ONELINE_32_uv(22,18,19);
    ROTATE_RIGHT_90_ONELINE_32_uv(21,20,21);
    ROTATE_RIGHT_90_ONELINE_32_uv(20,22,23);
    ROTATE_RIGHT_90_ONELINE_32_uv(19,24,25);
    ROTATE_RIGHT_90_ONELINE_32_uv(18,26,27);
    ROTATE_RIGHT_90_ONELINE_32_uv(17,28,29);
    ROTATE_RIGHT_90_ONELINE_32_uv(16,30,31);
    ROTATE_RIGHT_90_ONELINE_32_uv(15,32,33);
    ROTATE_RIGHT_90_ONELINE_32_uv(14,34,35);
    ROTATE_RIGHT_90_ONELINE_32_uv(13,36,37);
    ROTATE_RIGHT_90_ONELINE_32_uv(12,38,39);
    ROTATE_RIGHT_90_ONELINE_32_uv(11,40,41);
    ROTATE_RIGHT_90_ONELINE_32_uv(10,42,43);
    ROTATE_RIGHT_90_ONELINE_32_uv(9,44,45);
    ROTATE_RIGHT_90_ONELINE_32_uv(8,46,47);
    ROTATE_RIGHT_90_ONELINE_32_uv(7,48,49);
    ROTATE_RIGHT_90_ONELINE_32_uv(6,50,51);
    ROTATE_RIGHT_90_ONELINE_32_uv(5,52,53);
    ROTATE_RIGHT_90_ONELINE_32_uv(4,54,55);
    ROTATE_RIGHT_90_ONELINE_32_uv(3,56,57);
    ROTATE_RIGHT_90_ONELINE_32_uv(2,58,59);
    ROTATE_RIGHT_90_ONELINE_32_uv(1,60,61);
    ROTATE_RIGHT_90_ONELINE_32_uv(0,62,63);


    return 0;
}


/*
 * the core function.
 * right rotate the nv12 file
 * unsigned char *nv12_data : Pointer to the first address of source file
 * unsigned char *rot90_nv12_data : Pointer to the first address of distination file
 */
void nv12_right_rotate_90(unsigned char* nv12_data, unsigned char* rot90_nv12_data, int src_width, int src_height)
{
    int i = 0;
    int j = 0;

    int src_stride = src_width;

    int dst_width = src_height;
    int dst_height = src_width;
    int dst_stride = dst_width;
    int dst_remainder = dst_width % 64;
    int dst_align = dst_width - dst_remainder;

    uint8_t* src=(uint8_t*)nv12_data;
    uint8_t* dst=(uint8_t*)rot90_nv12_data;
    uint8_t* src_uv=(uint8_t*)(nv12_data + src_width*src_height);
    uint8_t* dst_uv =(uint8_t*)(rot90_nv12_data + dst_width*dst_height);

    //optmize for common-use size
    //begin-----------------------------

    int width_block_num = src_width / 32;
    int height_block_num = src_height / 32;

    unsigned char *src_block32, *dst_block32;
    unsigned char *src_block16, *dst_block16;

    //rotate Y component
    for (i = 0; i < height_block_num; i++) {
        for(j = 0; j < width_block_num; j++){
            src_block32 = src + i * 32 * src_stride + (j * 32);
            dst_block32 = dst + (j * 32 ) * dst_stride + (height_block_num-1-i) * 32;
            nv12_right_rotate_90_block32(src_block32, dst_block32, src_stride, dst_stride);
        }
    }

    /////////////uv
    //rotate U,V component

    //case when dst_width is divisible by 64
    if(height_block_num % 2 == 0) {
        int height_block_num_uv = (height_block_num) / 2;

        for (i = 0; i < height_block_num_uv; i++) {
            for(j = 0; j < width_block_num; j++){
                src_block32 = src_uv + i * 32 * src_stride + j * 32;
                dst_block32 = dst_uv + ((j) * 16 ) * (dst_remainder+dst_align) + (height_block_num_uv-1-i) * 64;
                nv12_right_rotate_90_block32_uv(src_block32, dst_block32, src_stride, dst_align, dst_remainder);
            }
        }
    }
    //case when dst_width is not divisible by 64
    else{
        int height_block_num_uv=(height_block_num - 1) / 2;
        for (i = 0; i < 1; i++) {
            for(j = 0; j < width_block_num; j++){
                src_block16 = src_uv + height_block_num_uv * 32 * src_stride + j * 32;
                dst_block16 = dst_uv + ((j) * 16 ) * (dst_remainder+dst_align) + (1-1-i) * 32;

                nv12_right_rotate_90_block16_uv(src_block16, dst_block16, src_stride, dst_remainder, dst_align);
            }
        }

        for (i = 0; i < height_block_num_uv; i++) {
            for(j = 0; j < width_block_num; j++){
                src_block32 = src_uv + i * 32 * src_stride + j * 32;
                dst_block32 = dst_uv + ((j) * 16 ) * (dst_remainder+dst_align) + 32 + (height_block_num_uv-1-i) * 64;

                nv12_right_rotate_90_block32_uv(src_block32, dst_block32, src_stride, dst_align, dst_remainder);
            }
        }
    }
    //end-----------------------------
}
