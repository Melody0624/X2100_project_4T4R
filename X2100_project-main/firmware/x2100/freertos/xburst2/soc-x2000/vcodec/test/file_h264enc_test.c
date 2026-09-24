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

//#include "icamera.h"
#include "../IMPP_api/codec.h"

static int filefd;
static int outfd;

IMPP_BufferInfo_t srcbuf[3];

#define PIC_W   320
#define PIC_H   240
#define SOURCE_PATH "/data/input.nv12"
void h264e_input_thread(void *arg)
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
				lseek(filefd, 0, SEEK_SET);
				read(filefd, (void *)srcbuf[buf.index].mplane.vaddr[0], PIC_W * PIC_H);
				read(filefd, (void *)srcbuf[buf.index].mplane.vaddr[1], PIC_W * PIC_H / 2);
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

int do_h264e_enc(void)
{
        int ret = 0;
        ret = IHal_CodecInit();
        if (ret) {
                printf("codec init error\n");
                return -1;
        }
        IHal_CodecHandle_t *handle = IHal_CodecCreate(H264_ENC);
        if (!handle) {
                printf("codec create failed\n");
                return -1;
        }

        IHal_CodecParam param;
        param.codec_type = H264_ENC;
        param.codecparam.h264e_param.rc_mode = IMPP_ENC_RC_MODE_VBR;
        param.codecparam.h264e_param.max_bitrate    = 400000;
        param.codecparam.h264e_param.gop_len        = 50;
        param.codecparam.h264e_param.mini_Qp        = 10;
        param.codecparam.h264e_param.max_Qp         = 40;
        param.codecparam.h264e_param.IFrameQp       = 30;
        param.codecparam.h264e_param.PFrameQp       = 30;
        param.codecparam.h264e_param.level          = 20;
        param.codecparam.h264e_param.freqIDR        = 25;
        /* param.h264e_param.maxPictureSize = PIC_W * PIC_H; */
        param.codecparam.h264e_param.enc_width      = PIC_W;
        param.codecparam.h264e_param.enc_height     = PIC_H;
        param.codecparam.h264e_param.src_width      = PIC_W;
        param.codecparam.h264e_param.src_height     = PIC_H;
        param.codecparam.h264e_param.src_fmt        = IMPP_PIX_FMT_NV12;
        ret =  IHal_Codec_SetParams(handle, &param);
        if (ret) {
                printf("set codec param failed\n");
                return -1;
        }
        int srcbuf_nums = IHal_Codec_CreateSrcBuffers(handle, IMPP_INTERNAL_BUFFER, 2);
        if (srcbuf_nums < 1) {
                printf("src buffer create failed\n");
                return -1;
        }

        int dstbuf_nums = IHal_Codec_CreateDstBuffer(handle, IMPP_INTERNAL_BUFFER, 2);
        if (dstbuf_nums < 1) {
                printf("dst buffer create failed\n");
                return -1;
        }
        for (int i = 0; i < srcbuf_nums; i++) {
                ret = IHal_Codec_GetSrcBuffer(handle, i, &srcbuf[i]);
                if (ret) {
                        printf("get src buffer %d failed", i);
                        return -1;
                }
        }

//         filefd = open(SOURCE_PATH, O_RDWR);
//         if (filefd < 0) {
//                 printf("open src file failed\n");
//                 return -1;
//         }
//         outfd = open("/data/x2000_out.h264", O_RDWR | O_CREAT | O_TRUNC, 0666);
//        // outfd = open("/data/x2000_out.h264", O_RDWR | O_CREAT);
//         if (outfd < 0) {
//                 printf("create out file failed\n");
//                 return -1;
//         }

        // fill buffer before
        // read(filefd, (void *)srcbuf[0].mplane.vaddr[0], PIC_W * PIC_H);
        // read(filefd, (void *)srcbuf[0].mplane.vaddr[1], PIC_W * PIC_H / 2);
        IHal_Codec_QueueSrcBuffer(handle, &srcbuf[0]);
        sleep(1);
#if 1
        // lseek(filefd, 0, SEEK_SET);
        // read(filefd, (void *)srcbuf[1].mplane.vaddr[0], PIC_W * PIC_H);
        // read(filefd, (void *)srcbuf[1].mplane.vaddr[1], PIC_W * PIC_H / 2);
        IHal_Codec_QueueSrcBuffer(handle, &srcbuf[1]);
        sleep(1);
#endif

        ret = IHal_Codec_Start(handle);
        if (ret) {
                printf("codec start failed\n");
                return -1;
        }
        int times = 10;
	thread_ptr_t p = thread_create("input_data_read", 1024, h264e_input_thread, (void *)handle);
        IHAL_CodecStreamInfo_t dstbuf;
	while (times--) {
		IHal_Codec_WaitDstAvailable(handle, IMPP_WAIT_FOREVER);
		ret = IHal_Codec_DequeueDstBuffer(handle, &dstbuf);
		if (!ret) {
			printf("dq dstbuf->index = %d paklen = %d #######\n", dstbuf.index, dstbuf.size);
			write(outfd, (void *)dstbuf.vaddr, dstbuf.size);
			ret = IHal_Codec_QueueDstBuffer(handle, &dstbuf);
			if (ret) {
				printf("app : queue dst buffer failed\r\n");
			}
			printf("times = %d ......\n", times);
		}
	}

	ret = IHal_Codec_Stop(handle);
	if (ret) {
		printf("codec start failed\n");
		return -1;
	}

	/*TODO: input_thread stop*/
	thread_delete(p);
	IHal_CodecDestroy(handle);
	IHal_CodecDeInit();
        close(filefd);
        close(outfd);

	return 0;
}







