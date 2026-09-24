#ifndef _HELIX_H264_ENCODER_H_
#define _HELIX_H264_ENCODER_H_

struct helix_h264_encoder;

enum helix_h264_bitrate_mode {
    HELIX_BITRATE_FIXQP,
    HELIX_BITRATE_CBR,
    HELIX_BITRATE_VBR,
};

struct helix_h264_param {
    enum helix_h264_bitrate_mode bitrate_mode;
    int bitrate;
    int width;
    int height;
    int gop_size;
};

struct helix_h264_encoder *helix_h264_encoder_init(struct helix_h264_param *params);

int helix_h264_encoder_encode(
    struct helix_h264_encoder *encoder, void *nv12, void *dst, int dst_size);

int helix_h264_encoder_encode_nv12_separate(
    struct helix_h264_encoder *encoder, void *y, void *uv, void *dst, int dst_size);

int helix_h264_encoder_is_keyframe(struct helix_h264_encoder *encoder);

void helix_h264_encoder_deinit(struct helix_h264_encoder *encoder);

#endif /* _HELIX_H264_ENCODER_H_ */
