#ifndef _HELIX_JPEG_DECODER_H_
#define _HELIX_JPEG_DECODER_H_

struct helix_jpeg_decoder;

struct helix_jpeg_decoder_param {
    int width;
    int height;
};

struct helix_jpeg_decoder *helix_jpeg_decoder_init(struct helix_jpeg_decoder_param *param);

int helix_jpeg_decoder_decode_nv12_separate(
    struct helix_jpeg_decoder *decoder, void *src, int src_size, void *y, void *uv);

int helix_jpeg_decoder_decode(
    struct helix_jpeg_decoder *decoder, void *src, int src_size, void *nv12);

void helix_jpeg_decoder_deinit(struct helix_jpeg_decoder *decoder);

#endif /* _HELIX_JPEG_DECODER_H_ */
