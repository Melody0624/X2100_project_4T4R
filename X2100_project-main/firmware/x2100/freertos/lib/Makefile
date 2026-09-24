src-$(CONFIG_LIB_DUMP_MEM) += dump_mem.c
src-$(CONFIG_LIB_ASSERT) += assert.c
ifneq ($(CONFIG_NEWLIB),y)
src-$(CONFIG_LIB_STRING) += string.c
src-$(CONFIG_LIB_STRING) += ctype.c
endif
src-$(CONFIG_LIB_XFORMAT_PRINTF) += printf.c
src-$(CONFIG_LIB_XFORMAT) += xformatc.c
ifeq ($(CONFIG_XBURST2),y)
src-$(CONFIG_LIB_XFORMAT) += cpux_safe_libc.c
endif
src-$(CONFIG_LIB_DEALY) += delay.c
src-$(CONFIG_LIB_CLEAR_BSS) += clear_bss.c
src-$(CONFIG_LIB_RTC) += rtc_lib.c
src-$(CONFIG_LIB_RING_BUFFER) += ring_buffer.c
src-$(CONFIG_LIB_SORT) += sort.c
src-$(CONFIG_BAYER16_TO_RGB) += bayer16_to_rgb.c
src-$(CONFIG_YUV422_TO_RGB) += yuv422_to_rgb.c
src-$(CONFIG_CMYK_TO_RGB) += cmyk_to_rgb.c
src-$(CONFIG_RING_MEM) += ring_mem.c
src-$(CONFIG_LIB_NALU) += nalu_buf.c
src-$(CONFIG_UBOOT_LIB) += uboot_lib.c
src-$(CONFIG_LIB_CRC32) += crc32.c
src-$(CONFIG_LIB_NV12_ROTATE) += nv12_rotate.c
src-y += spl_cmdargs_analysis.c
src-y += ctors.c

src-$(CONFIG_LIB_IMAGE_SCALER) += image_scaler.c

# 镜像处理库（C 版本）
src-$(CONFIG_LIB_IMAGE_MIRROR_C) += c_mirror.c

# 镜像处理库（MSA 加速版本）
ifeq ($(CONFIG_LIB_IMAGE_MIRROR_MSA),y)
src-y += msa_mirror.c
# MSA 文件需要 -mmsa 编译标志
CFLAGS += -mmsa
endif

ifeq ($(CONFIG_LIB_IMAGE_ROTATE_MSA),y)
src-y += msa_rotate.c
CFLAGS += -mmsa
endif

ifeq ($(CONFIG_LIB_IMAGE_ROTATE_MIRROR_MSA),y)
src-y += msa_images_rotate_mirror.c
CFLAGS += -mmsa
endif
