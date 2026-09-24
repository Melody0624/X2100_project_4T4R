
CFLAGS += -Wno-unused-variable
CFLAGS += -Wno-unused-label
CFLAGS += -Wno-unused-but-set-variable
CFLAGS += -Wno-unused-function
CFLAGS += -Wno-pointer-sign
CFLAGS += -Wno-sequence-point

src-$(CONFIG_X2000_VCODEC) += helix/helix_jpeg_encoder.c
src-$(CONFIG_X2000_VCODEC) += helix/helix_jpeg_decoder.c
src-$(CONFIG_X2000_VCODEC) += helix/helix_h264_encoder.c
src-$(CONFIG_X2000_VCODEC) += felix/felix_h264_decoder.c

src-$(CONFIG_X2000_VCODEC)+= helix/helix_drv.c
src-$(CONFIG_X2000_VCODEC)+= helix/helix_ops.c
src-$(CONFIG_X2000_VCODEC)+= helix/api/helix_x264_enc.c
src-$(CONFIG_X2000_VCODEC)+= helix/api/helix_jpeg_enc.c
src-$(CONFIG_X2000_VCODEC)+= helix/api/helix_jpeg_dec.c
src-$(CONFIG_X2000_VCODEC)+= helix/api/helix_x264_enc.c
src-$(CONFIG_X2000_VCODEC)+= helix/h264_encoder/h264e_rc.c
src-$(CONFIG_X2000_VCODEC)+= helix/h264_encoder/h264enc/set.c
src-$(CONFIG_X2000_VCODEC)+= helix/h264_encoder/h264enc/slice.c
src-$(CONFIG_X2000_VCODEC)+= helix/h264_encoder/h264enc/cabac.c
src-$(CONFIG_X2000_VCODEC)+= helix/h264_encoder/h264enc/common.c
src-$(CONFIG_X2000_VCODEC)+= helix/h264_encoder/h264e-rc/common_private.c
src-$(CONFIG_X2000_VCODEC)+= helix/h264_encoder/h264e-rc/h264e-rc-api.c
src-$(CONFIG_X2000_VCODEC)+= helix/jpge/jpge.c
src-$(CONFIG_X2000_VCODEC)+= helix/jpgd/jpgd.c
src-$(CONFIG_X2000_VCODEC)+= IMPP_api/codec.c
src-$(CONFIG_X2000_VCODEC)+= test/file_h264enc_test.c
src-$(CONFIG_X2000_VCODEC)+= test/file_jpegenc_test.c
src-$(CONFIG_X2000_VCODEC)+= test/file_jpegdec_test.c
obj-$(CONFIG_X2000_VCODEC) += xburst2/soc-x2000/vcodec/helix/h264_encoder/h264e-rc/librc/librc.a
### h264 dec
CFLAGS += -Ixburst2/soc-x2000/vcodec/felix -Ixburst2/soc-x2000/vcodec/felix/libh264 -Ixburst2/soc-x2000/vcodec/felix/libh264/include -Ixburst2/soc-x2000/vcodec/IMPP_api
src-$(CONFIG_X2000_VCODEC) += felix/felix_drv.c 		\
			felix/felix_ops.c			\
			felix/libh264/api/jzm_h264_dec.c 	\
			felix/libh264/src/buffer.c	 	\
			felix/libh264/src/golomb.c 		\
			felix/libh264/src/h2645_parse.c 	\
			felix/libh264/src/h264_direct.c 	\
			felix/libh264/src/h264_parse.c 		\
			felix/libh264/src/h264_picture.c 	\
			felix/libh264/src/h264_ps.c 		\
			felix/libh264/src/h264_refs.c 		\
			felix/libh264/src/h264_sei.c 		\
			felix/libh264/src/h264_slice.c 		\
			felix/libh264/src/h264data.c 		\
			felix/libh264/src/h264dec.c 		\
			felix/libh264/src/log.c 		\
			felix/libh264/src/mathtables.c 		\
			felix/libh264/src/mem.c 		\
			felix/libh264/src/vpu_ops.c
src-$(CONFIG_X2000_VCODEC) += test/h264dec_test.c

