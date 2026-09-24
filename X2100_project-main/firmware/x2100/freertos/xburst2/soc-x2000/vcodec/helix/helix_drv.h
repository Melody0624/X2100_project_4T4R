#ifndef __INGENIC_HELIX_DRV_H__
#define __INGENIC_HELIX_DRV_H__

#if 0
#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#endif
#include <os.h>
#include <os/thread_waiter.h>
#include <spinlock.h>
#include <semaphore.h>
#include <malloc.h>
#include <common.h>
#include <asm/addrspace.h>
#include <driver/cache.h>

//#include "helix/api/helix_x264_enc.h"
//#include "api/helix_jpeg_enc.h"
#include "helix/api/helix.h"
//#include "helix_buf.h"

#include "h264_encoder/h264e_rc.h"
#include "jpge/jpge.h"
#include "jpgd/jpgd.h"


#define INGENIC_VCODEC_ENC_NAME	"helix-venc"
#define INGENIC_VCODEC_MAX_PLANES	3

void *kzalloc(unsigned long size);

int ingenic_vpu_start(void *priv);
void ingenic_vpu_lock(void);
void ingenic_vpu_unlock(void);

static void *JZMalloc(int align, int size)
{
	return memalign(align, ALIGN(size, align));
}

static unsigned int get_phy_addr(void * vaddr)
{
	return CPHYSADDR(vaddr);
}


enum ingenic_fmt_type {
	INGENIC_FMT_FRAME 	= 0,
	INGENIC_FMT_ENC 	= 1,
	INGENIC_FMT_DEC 	= 2,
};

struct ingenic_video_fmt {
	u32 fourcc;
	enum ingenic_fmt_type type;
	u32 num_planes;
	enum helix_raw_format format;
};

#if 0
struct ingenic_codec_framesizes {
	u32 fourcc;
	struct v4l2_frmsize_stepwise stepwise;
};

struct ingenic_video_buf {
	struct vb2_v4l2_buffer vb;
	struct list_head list;

	struct video_frame_buffer buf;
};


#endif
enum ingenic_q_type {
	INGENIC_Q_DATA_SRC = 0,
	INGENIC_Q_DATA_DST = 1,
};

/* Queue Data. */
struct ingenic_venc_q_data {
	unsigned int    visible_width;
	unsigned int    visible_height;
	unsigned int    coded_width;
	unsigned int    coded_height;
//	enum v4l2_field field;
	unsigned int    bytesperline[INGENIC_VCODEC_MAX_PLANES];
	unsigned int    sizeimage[INGENIC_VCODEC_MAX_PLANES];
	struct ingenic_video_fmt    *fmt;
};

/*
  when ctx is created, IDLE
  when start_streaming, START,
  when abort, ABORT,
*/

enum ingenic_venc_state {
	INGENIC_VENC_STATE_IDLE = 0,
	INGENIC_VENC_STATE_HEADER,
	INGENIC_VENC_STATE_RUNNING,
	INGENIC_VENC_STATE_ABORT,
};

#if 0
/* v4l2 set parm to sw_parm, */
/* sw_parm api to sliceinfo. */
/* Sliceinfo to dma desc. */

/* helix_ctrl_if_start. */

#endif
struct ingenic_venc_ctx {
	struct ingenic_venc_dev *dev;

	struct video_frame_buffer *src_frame;
	int srcframe_num;
	struct video_frame_buffer *dest_frame;
	int destframe_num;

	struct list_head src_queued_list;
	struct list_head src_done_list;
	spinlock_t src_buf_lock;
	sem_t src_buf_queued_sem;
	sem_t src_buf_done_sem;

	struct list_head dest_queued_list;
	struct list_head dest_done_list;
	spinlock_t dest_buf_lock;
	sem_t dest_buf_queued_sem;
	sem_t dest_buf_done_sem;

	thread_ptr_t work_thread;
	thread_waiter_t work_waiter;


	int id;

	int capture_stopped;
	int output_stopped;

	struct ingenic_venc_q_data q_data[2];
	enum ingenic_venc_state state;

//	wait_queue_head_t queue;

	int int_cond;
	int int_status;

//	struct work_struct encode_work;
//	enum v4l2_colorspace colorspace;
	//enum v4l2_ycbcr_encoding ycbcr_enc;
	//enum v4l2_quantization quantization;
	//enum v4l2_xfer_func xfer_func;

	int codec_id;
#define CODEC_ID_H264E	1
#define CODEC_ID_JPGE	2
#define CODEC_ID_JPGD	3
	union {
		struct h264e_ctx h264e_ctx;
		struct jpge_ctx jpge_ctx;
		struct jpgd_ctx jpgd_ctx;
	};

};


struct ingenic_venc_dev {

//	struct v4l2_device v4l2_dev;
//	struct video_device *vfd_enc;
//	struct device *dev;

//	struct v4l2_m2m_dev *m2m_dev_enc;
//	struct platform_device *plat_dev;

//	struct mutex dev_mutex;
	struct workqueue_struct *encode_workqueue;

	spinlock_t spinlock;

	struct ingenic_venc_ctx *curr_ctx;
//	struct vb2_dc_conf *alloc_ctx;

	int id_counter;

	void *reg_base;
	void *cpm_base;
	int irq;
	struct clk *clk_gate;
	struct clk *clk;


};

int ingenic_helix_init(void);
int ingenic_helix_deinit(void);
void *ingenic_helix_ctx_init(unsigned int fmt, int codec_type);
int ingenic_helix_ctx_deinit(void *ctx);

#endif
