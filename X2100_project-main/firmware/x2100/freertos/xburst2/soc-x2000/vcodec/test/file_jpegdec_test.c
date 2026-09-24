#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <IMPP_api/codec.h>
#include <getopt.h>
#include <stdlib.h>


struct configInfo {
        int width;
        int height;
        char *srcfile;
        char *dstfile;
};

static struct configInfo input_params;


static struct option long_options[] = {
        { "pixel_size", required_argument, NULL, 's'},
        { "src_file  ", required_argument, NULL, 'i'},
        { "dst_file  ", required_argument, NULL, 'o'},
        { "help      ", no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0},
};

#define print_opt_help(opt_index, help_str)             \
        do {                                \
                        printf("   -%c or --%s          %s", (char)long_options[opt_index].val, long_options[opt_index].name, help_str); \
        } while (0)

static void usage(void)
{
        printf("\njpegdec-example usage:\n");
        print_opt_help(0, "The resolution size of the JPEG file. The format is WxH, for example: 1280x720.\n");
        print_opt_help(1, "File name and path of the JPEG file.\n");
        print_opt_help(2, "File name and path of the NV12 file. The default is out_test.nv12.\n");
        print_opt_help(3, "show help.\n");
}

static int get_paramt(int argc, char *argv[])
{
        int ret = 0;
        memset(&input_params, 0, sizeof(struct configInfo));

        /* Default Parameters. */
        input_params.dstfile = "/data/output.nv12";
        input_params.srcfile = "/data/input.jpg";
        input_params.width = 320;
        input_params.height = 240;

        char optstring[] = "s:i:o:h";
        while ((ret = getopt_long(argc, argv, optstring, long_options, NULL)) != -1) {
                switch (ret) {
                case 's':
                        if (2 != sscanf(optarg, "%dx%d", &input_params.width, &input_params.height)) {
                                printf("The resolution format of the resolution size is incorrect!\r\n");
                                exit(-1);
                        }
                        break;
                case 'i':
                        input_params.srcfile = optarg;
                        break;
                case 'o':
                        input_params.dstfile = optarg;
                        break;
                case 'h':
                        usage();
                        exit(0);
                case '?':
                default:
                        usage();
                        exit(-1);
                }
        }

        if (0 == input_params.width || 0 == input_params.height || NULL == input_params.srcfile) {
                printf("Please input paramt!!!\r\n");
                usage();
                exit(-1);
        }

        return 0;
}


int do_jpeg_dec(int argc, char **argv)
{
        int ret = 0;
        IHal_CodecHandle_t *codec_handle = NULL;
        IHal_CodecParam dec_param;
        unsigned long filesize = 0;

     //   get_paramt(argc, argv);
	input_params.dstfile = "/data/output.nv12";
	input_params.srcfile = "/data/input.jpg";
	input_params.width = 320;
	input_params.height = 240;


        ret = IHal_CodecInit();

        codec_handle = IHal_CodecCreate(JPEG_DEC);
        assert(0 != codec_handle);

        memset(&dec_param, 0, sizeof(IHal_CodecParam));
        dec_param.codec_type = JPEG_DEC;
        dec_param.codecparam.jpegdec_param.src_width = input_params.width;            /* 源图像宽度 */
        dec_param.codecparam.jpegdec_param.src_height = input_params.height;          /* 源图像高度 */
        dec_param.codecparam.jpegdec_param.dst_fmt = IMPP_PIX_FMT_NV12;               /* 目标像素格式 */
        ret = IHal_Codec_SetParams(codec_handle, &dec_param);
        assert(0 == ret);

        int srcbuf_nums = IHal_Codec_CreateSrcBuffers(codec_handle, IMPP_INTERNAL_BUFFER, 1);
        assert(1 <= srcbuf_nums);

        int dstbuf_nums = IHal_Codec_CreateDstBuffer(codec_handle, IMPP_INTERNAL_BUFFER, 1);
        assert(1 <= dstbuf_nums);

        int srcfd = open(input_params.srcfile, O_RDONLY);
        assert(srcfd > 0);

        int dstfd = open(input_params.dstfile, O_RDWR | O_CREAT | O_TRUNC, 0666);
        assert(dstfd > 0);

        filesize = lseek(srcfd, 0, SEEK_END);
        lseek(srcfd, 0, SEEK_SET);


        IMPP_BufferInfo_t codec_srcbuf;
        IHAL_CodecStreamInfo_t codec_dststream;
        memset(&codec_srcbuf, 0, sizeof(IMPP_BufferInfo_t));
        memset(&codec_dststream, 0, sizeof(IHAL_CodecStreamInfo_t));

        ret = IHal_Codec_Start(codec_handle);
        assert(0 == ret);

        ret = IHal_Codec_GetSrcBuffer(codec_handle, 0, &codec_srcbuf);
        assert(0 == ret);

        codec_srcbuf.byteused = filesize;
        int times = 100 ;
        while (times--) {
                lseek(srcfd, 0, SEEK_SET);
                read(srcfd, (void *)codec_srcbuf.mplane.vaddr[0], filesize);
                codec_srcbuf.byteused = filesize;

                ret = IHal_Codec_QueueSrcBuffer(codec_handle, &codec_srcbuf);
                assert(0 == ret);

                ret = IHal_Codec_WaitDstAvailable(codec_handle, IMPP_WAIT_FOREVER);
                assert(0 == ret);

                ret = IHal_Codec_DequeueDstBuffer(codec_handle, &codec_dststream);
                assert(0 == ret);

                ret = IHal_Codec_WaitSrcAvailable(codec_handle, IMPP_WAIT_FOREVER);
                assert(0 == ret);

                ret = IHal_Codec_DequeueSrcBuffer(codec_handle, &codec_srcbuf);
                assert(0 == ret);

                if (times == 0) {
                        write(dstfd, (void *)codec_dststream.mp.vaddr[0], codec_dststream.mp.len[0]);
                        write(dstfd, (void *)codec_dststream.mp.vaddr[1], codec_dststream.mp.len[1]);
                }

                printf("[%d] get dec out data ok !! \r\n", __LINE__);

                ret = IHal_Codec_QueueDstBuffer(codec_handle, &codec_dststream);
                assert(0 == ret);
        }

        close(dstfd);
        ret = close(srcfd);
        assert(0 == ret);

        ret = IHal_Codec_Stop(codec_handle);
        assert(0 == ret);

        ret = IHal_CodecDestroy(codec_handle);
        assert(0 == ret);

        IHal_CodecDeInit();

        return 0;
}
