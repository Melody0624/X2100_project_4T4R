#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>

#include "codec.h"
#include "bs.h"


#define PACKET_LEN  512 * 1024
char packet[PACKET_LEN];


struct configInfo {
        int width;
        int height;
        char *srcfile;
};

static struct configInfo input_params;

static struct option long_options[] = {
        { "pixel_size", required_argument, NULL, 's'},
        { "src_file  ", required_argument, NULL, 'i'},
        { "help      ", no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0},
};

#define print_opt_help(opt_index, help_str)             \
        do {                                \
                        printf("   -%c or --%s          %s", (char)long_options[opt_index].val, long_options[opt_index].name, help_str); \
        } while (0)

static void usage(void)
{
        printf("\nh264dec-example usage:\n");
        print_opt_help(0, "Resolution size of H264 encoded code stream file. The format is WxH, for example: 1280x720.\n");
        print_opt_help(1, "File name and path of H264 encoded code stream file.\n");
        print_opt_help(2, "show help.\n");
}

static int get_paramt(int argc, char *argv[])
{
        int ret = 0;
        memset(&input_params, 0, sizeof(struct configInfo));

        char optstring[] = "s:i:h";
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



typedef struct {
        unsigned char *stream;
        unsigned char type;
        unsigned int size;
} nal_unit_t;

#define MAX_NALS_PER_TIME       9000    //25fps@60min

typedef struct {
        nal_unit_t nal[MAX_NALS_PER_TIME];
        int nal_count;
} nal_units_t;

nal_units_t nal_units;


static int m_find_start_code(unsigned char *buf, unsigned int len, unsigned int *prefix_bytes)
{
        unsigned char *start = buf;
        unsigned char *end = buf + len;
        int index = 0;

        if (start >= end) {
                return -1;
        }

        while (start < end) {
#if 1
                /* next24bits */
                index = start - buf;

                if (start[0] == 0 && start[1] == 0 && start[2] == 1) {
                        *prefix_bytes = 3;
                        return index + 3;
                }

                start++;


                /* end of data buffer, no need to find. */
                if ((start + 3) == end) {
                        return -1;
                }
#endif
        }

        return -1;
}

static unsigned int get_nal_size(char *buf, unsigned int len)
{
        char *start = buf;
        char *end = buf + len;
        unsigned int size = 0;

        if (start >= end) {
                return -1;
        }

        while (start < end) {

                if (start[0] == 0 && start[1] == 0 && start[2] == 1) {
                        break;
                }

                start++;
        }

        size = (unsigned int)(start - buf);

        return size;
}

int extract_nal_units(nal_units_t *nals, unsigned char *buf, unsigned int len)
{
        int index = 0;
        int next_nal = 0;
        unsigned char *start = buf;
        unsigned char *end = buf + len;
        int size = 0;
        unsigned int prefix_bytes; /*000001/00000001*/
        nal_unit_t *nal;

        unsigned int left = len;
        int i = 0;

        int eos = 0;
        nals->nal_count = 0;

        for (i = 0; i < MAX_NALS_PER_TIME; i++) {

                nal = &nals->nal[i];

                index = m_find_start_code(start, left, &prefix_bytes);
                if (index < 0) {
                        printf("no start code found!\n");
                        return -1;
                }

                nal->stream = start;
                nal->type = nal->stream[index];

                nal->size = get_nal_size(start + index, left - index) + index;
                nals->nal_count++;

                start += nal->size;
                left -= nal->size;

                if (left <= 0) {
                        /*EOS*/
                        break;
                }

        }

        if (left) {
                printf("too many nals extraced!, %d bytes left\n", left);
        }

        return len - left;
}

/* 从NALS 中合并一个完整的Frame，将Frame中所有的slice 拼接到一起。*/
int  get_slice_frame(nal_unit_t *nals, int start, int nal_counts, unsigned char **slice_frame_buffer, unsigned int *slice_frame_size, int *got_frame)
{
        nal_unit_t *nal = NULL;
        int i = 0;

        int first_mb_in_slice = 0;
        int slice_type = 0;
        int new_frame = 0;
        bs_t current_nal_bs;

        *got_frame = 0;
        *slice_frame_size = 0;

        for (i = start; i < nal_counts; i++) {

                nal = &nal_units.nal[i];

                bs_init(&current_nal_bs, nal->stream + 4, nal->size - 4);


                if ((nal->type & 0x1f) == 0x5 || (nal->type & 0x1f) == 0x1) {
                        first_mb_in_slice = bs_read_ue(&current_nal_bs);
                        slice_type = bs_read_ue(&current_nal_bs);


                        if (first_mb_in_slice == 0) {

                                if (new_frame) {
                                        *got_frame = 1;
                                        break;
                                }
                                new_frame = 1;
                        }

                }

                if (*slice_frame_buffer == NULL) {
                        *slice_frame_buffer = nal->stream;
                }

                *slice_frame_size += nal->size;
        }

        return i;
}

void hexdump(unsigned char *buf, int len);

void dump_dec_data(unsigned char *buf, unsigned int size, int index)
{
	if(index == 0){
		printf("y data: vddr = %p, size = %x\n", buf, size);
		hexdump(buf, 512/*size*/);
	}else{
		printf("uv data: vddr = %p, size = %x\n", buf, size);
		hexdump(buf, 512/*size*/);
	}
}

int do_h264_dec(int argc, char **argv)
{
        int ret = 0;
        IHal_CodecHandle_t *codec_handle = NULL;
        IHal_CodecParam h264dec_param;
        int srcbuf_nums = 0;
        int dstbuf_nums = 0;
        int srcfd = -1;
        unsigned long filesize = 0;

        get_paramt(argc, argv);

        ret = IHal_CodecInit();
        assert(0 == ret);

        codec_handle = IHal_CodecCreate(H264_DEC);
        assert(0 != codec_handle);

        memset(&h264dec_param, 0, sizeof(IHal_CodecParam));
        h264dec_param.codec_type = H264_DEC;
        h264dec_param.codecparam.h264d_param.src_width = input_params.width;          /* 源图像宽度 */
        h264dec_param.codecparam.h264d_param.src_height = input_params.height;        /* 源图像高度 */
        h264dec_param.codecparam.h264d_param.dst_fmt = IMPP_PIX_FMT_NV12;             /* 目标像素格式 */
        ret = IHal_Codec_SetParams(codec_handle, &h264dec_param);
        assert(0 == ret);

        srcbuf_nums = IHal_Codec_CreateSrcBuffers(codec_handle, IMPP_INTERNAL_BUFFER, 3);
        assert(1 <= srcbuf_nums);

        dstbuf_nums = IHal_Codec_CreateDstBuffer(codec_handle, IMPP_INTERNAL_BUFFER, 3);
        assert(1 <= dstbuf_nums);

	ret = IHal_Codec_Start(codec_handle);
        assert(0 == ret);

	srcfd = open(input_params.srcfile, O_RDONLY);
        assert(-1 != srcfd);

        int dstfd = -1;
        dstfd = open("/udisk/out_test.nv12", O_RDWR | O_CREAT | O_TRUNC, 0666);
        assert(-1 != dstfd);

        filesize = lseek(srcfd, 0, SEEK_END);
        lseek(srcfd, 0, SEEK_SET);

	int index = 0;
        int packet_len = 0;
        nal_unit_t *nal = NULL;
        nal_unit_t *next_nal = NULL;
        nal_unit_t *last_nal = NULL;
        unsigned long current_offset = 0;
        unsigned char *slice_frame_buffer = NULL;
        unsigned int slice_frame_size = 0;
        int eof = 0;
        IMPP_BufferInfo_t codec_srcbuf;
        IHAL_CodecStreamInfo_t codec_dststream;
        int nal_count = 0;
        int got_frame = 0;
        int start = 0;

	while (1) {
                memset(&codec_srcbuf, 0, sizeof(IMPP_BufferInfo_t));

                packet_len = read(srcfd, packet, PACKET_LEN);
                if (packet_len <= 0) {
                        // lseek(srcfd,0,SEEK_SET);
                        break;
                }

                nal_units.nal_count = 0;
                ret = extract_nal_units(&nal_units, packet, packet_len);

                if (ret < packet_len) {
                        printf("packet not extracted completely!\n");
                }

                nal_count = 0;

                last_nal = &nal_units.nal[nal_units.nal_count - 1];
                current_offset = lseek(srcfd, 0, SEEK_CUR);

                if (current_offset < filesize) {
                        nal_count = nal_units.nal_count - 1;
                        lseek(srcfd, -last_nal->size, SEEK_CUR);
                } else {
                        /*EOF.*/
                        nal_count = nal_units.nal_count;
                        eof = 1;
                        printf("----EOF----!\n");
                }

                got_frame = 0;
                start = 0;

                do {
                        slice_frame_buffer = NULL;
                        ret = get_slice_frame(nal_units.nal, start, nal_count, &slice_frame_buffer, &slice_frame_size, &got_frame);
                        start = ret;
                        if (ret == nal_count) {
                                if (!got_frame && eof == 0) {
                                        /* reseek to last nal decoded.*/
                                        lseek(srcfd, -slice_frame_size, SEEK_CUR);
                                }
                                break;
                        }

                        /* decode nal units */
                        ret = IHal_Codec_GetSrcBuffer(codec_handle, index, &codec_srcbuf);
                        assert(0 == ret);

                        for (int i = 0; i < codec_srcbuf.mplane.numPlanes; i++) {
                                memcpy((void *)codec_srcbuf.mplane.vaddr[i], slice_frame_buffer, slice_frame_size);
                        }
                        codec_srcbuf.byteused = slice_frame_size;

                        ret = IHal_Codec_QueueSrcBuffer(codec_handle, &codec_srcbuf);
                        assert(0 == ret);

                        ret = IHal_Codec_WaitDstAvailable(codec_handle, IMPP_WAIT_FOREVER);
                        assert(0 == ret);

                        ret = IHal_Codec_DequeueDstBuffer(codec_handle, &codec_dststream);
                        assert(0 == ret);

			//dump_dec_data(codec_dststream.mp.vaddr[0], codec_dststream.mp.len[0], 0);
			//dump_dec_data(codec_dststream.mp.vaddr[1], codec_dststream.mp.len[1], 1);
                        write(dstfd, (void *)codec_dststream.mp.vaddr[0], codec_dststream.mp.len[0]);
                        write(dstfd, (void *)codec_dststream.mp.vaddr[1], codec_dststream.mp.len[1]);


                        ret = IHal_Codec_QueueDstBuffer(codec_handle, &codec_dststream);
                        assert(0 == ret);

                        ret = IHal_Codec_WaitSrcAvailable(codec_handle, IMPP_WAIT_FOREVER);
                        assert(0 == ret);

                        ret = IHal_Codec_DequeueSrcBuffer(codec_handle, &codec_srcbuf);
                        assert(0 == ret);
                } while (1);
        }

exit_loop:

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
