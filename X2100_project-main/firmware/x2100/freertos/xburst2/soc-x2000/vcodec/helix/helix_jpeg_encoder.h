#ifndef _HELIX_JPEG_ENCODER_H_
#define _HELIX_JPEG_ENCODER_H_

struct helix_jpeg_encoder_param {
    int compress_quality; /*0 ～ 100 可调百分比*/
    int width;
    int height;
};

struct helix_jpeg_encoder;

struct helix_jpeg_encoder *helix_jpeg_encoder_init(struct helix_jpeg_encoder_param *param);

int helix_jpeg_encoder_encode_nv12_separate(
    struct helix_jpeg_encoder *encoder, void *y, void *uv, void *dst, int dst_size);

int helix_jpeg_encoder_encode(
    struct helix_jpeg_encoder *encoder, void *nv12, void *dst, int dst_size);

void helix_jpeg_encoder_deinit(struct helix_jpeg_encoder *encoder);

#endif /* _HELIX_JPEG_ENCODER_H_ */
