#include <stdio.h>
#include <printf.h>
#include <common.h>
#include <os.h>
#include <stdlib.h>
#include <malloc.h>
#include <include_bin.h>
#include <assert.h>
#include <driver/cache.h>

#include <felix/felix_h264_decoder.h>
#include <lib/nalu_buf.h>
#include <common.h>

INCBIN(h264, "example/resource/test.h264");
#define VIDEO_WIDTH    1280
#define VIDEO_HEIGHT   720

void felix_h264_decoder_test(void)
{
    /* H264 解码需要宽128对齐 高度16对齐*/
    struct felix_h264_decoder_param param = {
        .width = ALIGN(VIDEO_WIDTH, 128),
        .height = ALIGN(VIDEO_HEIGHT, 16),
    };

    void *nv12 = memalign(256, ALIGN(param.width*param.height*3/2, cache_line_size()));
    assert(nv12);

    int decode_image_sz = ALIGN(param.width*param.height, cache_line_size());
    unsigned char *decode_image = (unsigned char *)memalign(256, decode_image_sz);
    assert(decode_image);

    int nalu_buf_sz = decode_image_sz;
    if (nalu_buf_sz < 128*1024)
        nalu_buf_sz = 128*1024;

    struct nalu_buf *nalu_buf = nalu_buf_init(nalu_buf_sz);
    struct nalu_unit nalu_unit = {0};
    assert(nalu_buf);

    struct felix_h264_decoder *decoder = felix_h264_decoder_init(&param);
    if (!decoder)
        goto delete_nalu_buf;

    void *data = (void *)h264Data;
    int size = 0;
    int decode_len = 0;
    int write_len = 0;
    int ret = 0;
    int grop_len = 0;

    while (write_len < h264Size) {
        /* 本次写入的长度大小 */
        size = h264Size - write_len;
        ret = nalu_buf_write(nalu_buf, data+write_len, size);
        if (ret > 0)
            write_len += ret;

        /* 解析 nalu unit */
        while(1) {
            decode_len = nalu_buf_read_unit(nalu_buf, &nalu_unit, decode_image+grop_len, decode_image_sz-grop_len);
            if (decode_len <= 0)
                break;

decode_nalu_unit:
            /* 仅I帧/P帧为解码帧，其他均不可直接解码 */
            if (nalu_unit.nal_unit_type != NALU_TYPE_IDR && nalu_unit.nal_unit_type != NALU_TYPE_SLICE) {
                /* sps/pps需要累加, 其他类型则忽略不处理*/
                if (nalu_unit.nal_unit_type == NALU_TYPE_SPS || nalu_unit.nal_unit_type == NALU_TYPE_PPS)
                    grop_len += decode_len;

                continue;
            }

            felix_h264_decoder_decode(decoder, decode_image, grop_len+decode_len, nv12);
            grop_len = 0;
        }
    }

    grop_len = 0;
    /* 获取缓冲区中剩余的最后一个 nalu_unit, 因为是根据两个开始码(001\0001)之间来计算划分nalu_unit, 因此最后一个需要特殊处理 */
    decode_len = nalu_buf_read_tail(nalu_buf, &nalu_unit, decode_image, decode_image_sz);
    if (decode_len > 0)
        goto decode_nalu_unit;

    felix_h264_decoder_deinit(decoder);
delete_nalu_buf:
    nalu_buf_deinit(nalu_buf);
    free(nv12);
    free(decode_image);
}
