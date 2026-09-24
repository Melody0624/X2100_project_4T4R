#ifndef _FELIX_H264_DECODER_H_
#define _FELIX_H264_DECODER_H_

struct felix_h264_decoder_param {
    int width;
    int height;
};

struct felix_h264_decoder;

struct felix_h264_decoder *felix_h264_decoder_init(struct felix_h264_decoder_param *param);

int felix_h264_decoder_decode_nv12_separate(
    struct felix_h264_decoder *decoder, void *src_, int src_size, void *y_, void *uv_);

int felix_h264_decoder_decode(
    struct felix_h264_decoder *decoder, void *src, int src_size, void *nv12);

void felix_h264_decoder_deinit(struct felix_h264_decoder *decoder);

#endif /* _FELIX_H264_DECODER_H_ */
