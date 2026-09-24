#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
//#include <sys/mman.h>
#include <sys/time.h>
#include <assert.h>
//#include <sys/ioctl.h>
#include <unistd.h>
#include <os/thread.h>

#include "../IMPP_api/codec.h"

#define BUFFER_NUM 1

IMPP_BufferInfo_t srcbuf[3];
thread_ptr_t input_thread_ptr;

static char *srcfile_path = "/data/input.nv12";
static int srcfd = -1;
static IHAL_INT32 pic_width = 320;
static IHAL_INT32 pic_height = 240;
static char *output_path = "/data/output.jpg";
static int outfd = -1;

void jpge_input_thread(void *arg)
{
	int ret = 0;
	IHal_CodecHandle_t *handle = (IHal_CodecHandle_t *)arg;
	IMPP_BufferInfo_t buf;
	for (;;) {
		// fill src buffer again
		ret = IHal_Codec_WaitSrcAvailable(handle, IMPP_WAIT_FOREVER);
		if (!ret) {
			ret = IHal_Codec_DequeueSrcBuffer(handle, &buf);
			if (!ret) {
				lseek(srcfd, 0, SEEK_SET);
				read(srcfd, (void *)srcbuf[buf.index].mplane.vaddr[0], pic_width * pic_height);
				read(srcfd, (void *)srcbuf[buf.index].mplane.vaddr[1], pic_width * pic_height / 2);
				ret = IHal_Codec_QueueSrcBuffer(handle, &buf);
				if (ret) {
					printf("app : 1 queue src buffer failed\r\n");
				}
			}
		} else {
			printf("wait src available failed\n");
			usleep(5000);
		}
	}
}

int do_jpeg_enc(void)
{
        int ret = 0;
        ret = IHal_CodecInit();
        if (ret) {
                printf("codec init error\n");
                return -1;
        }
        IHal_CodecHandle_t *handle = IHal_CodecCreate(JPEG_ENC);
        if (!handle) {
                printf("codec create failed\n");
                goto deinit_codec;
        }

        IHal_CodecParam param;
        memset(&param, 0, sizeof(IHal_CodecParam));
        param.codec_type = JPEG_ENC;
        param.codecparam.jpegenc_param.initialQp = 35;
        param.codecparam.jpegenc_param.quality = 40;

        /* param.h264e_param.maxPictureSize = pic_width * pic_height; */
        param.codecparam.jpegenc_param.enc_width      = pic_width;
        param.codecparam.jpegenc_param.enc_height     = pic_height;
        param.codecparam.jpegenc_param.src_width      = pic_width;
        param.codecparam.jpegenc_param.src_height     = pic_height;
        param.codecparam.jpegenc_param.src_fmt        = IMPP_PIX_FMT_NV12;
        ret =  IHal_Codec_SetParams(handle, &param);
        if (ret) {
                printf("set codec param failed\n");
                goto destroy_codec;
        }
        int srcbuf_nums = IHal_Codec_CreateSrcBuffers(handle, IMPP_INTERNAL_BUFFER, BUFFER_NUM);
        if (srcbuf_nums < 1) {
                printf("src buffer create failed\n");
                goto destroy_codec;
        }

        int dstbuf_nums = IHal_Codec_CreateDstBuffer(handle, IMPP_INTERNAL_BUFFER, BUFFER_NUM);
        if (dstbuf_nums < 1) {
                printf("dst buffer create failed\n");
                goto destroy_codec;
        }
        for (int i = 0; i < srcbuf_nums; i++) {
                ret = IHal_Codec_GetSrcBuffer(handle, i, &srcbuf[i]);
                if (ret) {
                        printf("get src buffer %d failed", i);
                        goto destroy_codec;
                }
        }

        srcfd = open(srcfile_path, O_RDWR);
        if (srcfd < 0) {
                printf("open src file failed\n");
                goto destroy_codec;
        }
        outfd = open(output_path, O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (outfd < 0) {
                printf("create out file failed\n");
                goto close_srcfile;
        }

        // fill buffer before
        for (int i = 0; i < srcbuf_nums; i++) {
		read(srcfd, (void *)srcbuf[i].mplane.vaddr[0], pic_width * pic_height);
		read(srcfd, (void *)srcbuf[i].mplane.vaddr[1], pic_width * pic_height / 2);
                IHal_Codec_QueueSrcBuffer(handle, &srcbuf[i]);
                lseek(srcfd, 0, SEEK_SET);
        }

        ret = IHal_Codec_Start(handle);
        if (ret) {
                printf("codec start failed\n");
                goto close_dstfile;
	}
	printf("########## enter while ##############\n");
	int times = 10;
	input_thread_ptr = thread_create("input_data_read", 1024, jpge_input_thread, (void *)handle);
	IHAL_CodecStreamInfo_t dstbuf;
	while (times--) {
		IHal_Codec_WaitDstAvailable(handle, IMPP_WAIT_FOREVER);
		ret = IHal_Codec_DequeueDstBuffer(handle, &dstbuf);
		if (!ret) {
			printf("dq dstbuf->index = %d paklen = %d #######\n", dstbuf.index, dstbuf.size);
			lseek(outfd, 0, SEEK_SET);
			write(outfd, (void *)dstbuf.vaddr, dstbuf.size);
			ret = IHal_Codec_QueueDstBuffer(handle, &dstbuf);
			if (ret) {
				printf("app : queue dst buffer failed\r\n");
                        }
                        printf("times = %d ......\n", times);
		}
	}
	//thread_cancel(input_tid);
	//thread_join(input_tid, NULL);
	thread_delete(input_thread_ptr);
	ret = IHal_Codec_Stop(handle);
        if (ret) {
                printf("codec start failed\n");
                return -1;
        }
        close(srcfd);
        close(outfd);
        IHal_CodecDestroy(handle);
        IHal_CodecDeInit();
        return 0;

close_dstfile:
        close(outfd);
close_srcfile:
        close(srcfd);
destroy_codec:
        IHal_CodecDestroy(handle);
deinit_codec:
        IHal_CodecDeInit();

        return -1;
}
