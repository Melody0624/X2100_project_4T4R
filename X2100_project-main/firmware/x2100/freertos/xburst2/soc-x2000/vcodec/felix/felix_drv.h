#ifndef __JZ_FELIX_DRV_H__
#define __JZ_FELIX_DRV_H__

#include "./libh264/include/avcodec.h"
#include "./libh264/include/frame.h"
#include "./libh264/include/h264dec.h"
#include <spinlock.h>
#include <semaphore.h>
#include <os/thread.h>
#include <os/thread_waiter.h>


#define INGENIC_VCODEC_DEC_NAME "felix-vdec"
#define INGENIC_VCODEC_MAX_PLANES	3

#define REG_VPU_STATUS ( *(volatile unsigned int*)0xb3200034 )
#define REG_VPU_LOCK ( *(volatile unsigned int*)0xb329004c )
#define REG_VPUCDR ( *(volatile unsigned int*)0xb0000030 )
#define REG_CPM_VPU_SWRST ( *(volatile unsigned int*)0xb00000c4 )
#define REG_VPU_TLBBASE (*(volatile unsigned int *)(0x30 + 0xb3200000))
#define CPM_VPU_SR           (0x1<<31)
#define CPM_VPU_STP          (0x1<<30)
#define CPM_VPU_ACK          (0x1<<29)

#define REG_VPU_GLBC      0x00000
#define VPU_INTE_ACFGERR     (0x1<<20)
#define VPU_INTE_TLBERR      (0x1<<18)
#define VPU_INTE_BSERR       (0x1<<17)
#define VPU_INTE_ENDF        (0x1<<16)

#define REG_VPU_STAT      0x00034
#define VPU_STAT_ENDF    (0x1<<0)
#define VPU_STAT_BPF     (0x1<<1)
#define VPU_STAT_ACFGERR (0x1<<2)
#define VPU_STAT_TIMEOUT (0x1<<3)
#define VPU_STAT_JPGEND  (0x1<<4)
#define VPU_STAT_BSERR   (0x1<<7)

#define VPU_STAT_TLBERR  (0x1F<<10)
#define VPU_STAT_SLDERR  (0x1<<16)

#define REG_VPU_JPGC_STAT 0xE0008
#define JPGC_STAT_ENDF   (0x1<<31)

#define REG_VPU_SDE_STAT  0x90000
#define SDE_STAT_BSEND   (0x1<<1)

#define REG_VPU_DBLK_STAT 0x70070
#define DBLK_STAT_DOEND  (0x1<<0)

#define REG_VPU_AUX_STAT  0xA0010
#define AUX_STAT_MIRQP   (0x1<<0)


/********************************************
  SW_RESET (VPU software reset)
*********************************************/
#define REG_CFGC_SW_RESET    0x00000
#define REG_CFGC_RST         (0x1<<30)
#define REG_CFGC_RST_CLR     (0x0<<30)
#define REG_CFGC_EARB_STAT   0x0000d
#define REG_CFGC_EARB_EMPT   (0x20000)

#define REG_CPM_CLKGR 0x20
#define REG_CPM_CLKGR1 0x28
#define CPM_CLKGR_JPEG           (0x1<<12)
#define REG_CPM_LCR 0x4

enum ingenic_fmt_type {
	INGENIC_FMT_FRAME = 0,
	INGENIC_FMT_DEC = 1,
};

enum felix_raw_format {
	FELIX_TILE_MODE = 0,
	FELIX_420P_MODE = 4,	/* not support */
	FELIX_NV12_MODE = 8,
	FELIX_NV21_MODE = 12,	/* not support */
	FELIX_FORMAT_NONE,
};

typedef struct {
	unsigned int vaddr;
	unsigned int paddr;
	unsigned int size;
	AVFrame  frame;		// 在申请完内存后，需要将此字段Frame地址进行填充
	int index;
	struct list_head entry;
}felix_buffer_t;

struct ingenic_video_fmt {
	u32 fourcc;
	enum ingenic_fmt_type type;
	u32 num_planes;
	enum felix_raw_format format;
};

enum ingenic_vcodec_state {
	INGENIC_STATE_IDLE = 0,
	INGENIC_STATE_HEADER,
	INGENIC_STATE_RUNNING,
	INGENIC_STATE_ABORT,
};


enum ingenic_q_type {
	INGENIC_Q_DATA_SRC = 0,
	INGENIC_Q_DATA_DST = 1,
};

struct ingenic_vdec_dev {
	struct workqueue_struct *dec_workqueue;

	spinlock_t spinlock;

	struct ingenic_vdec_ctx *curr_ctx;

	int id_counter;

	void *reg_base;
	void *cpm_base;
	int irq;

};

struct ingenic_vdec_ctx {

	struct ingenic_vdec_dev *dev;
	int id;	/* used for debug ?*/
	enum ingenic_vcodec_state state;

	int output_stopped;
	int capture_stopped;

	int int_cond;
	struct list_head src_queue_entry;		// for full buffer
	AVCodecContext *avctx;
	H264Context *h;

	int sWidth;
	int sHeight;

	// src & dst buffer mannager
	struct list_head src_queue_list;
	struct list_head src_done_list;
	sem_t src_buf_queued_sem;
	sem_t src_buf_done_sem;
	spinlock_t src_buf_lock;

	struct list_head dst_queue_list;
	struct list_head dst_done_list;
	sem_t dst_buf_queued_sem;
	sem_t dst_buf_done_sem;
	spinlock_t dst_buf_lock;

	thread_ptr_t work_thread;
	thread_waiter_t work_waiter;

	felix_buffer_t src_buf[3];
	felix_buffer_t dst_buf[3];

	AVFrame dec_frame;
};

int ingenic_felix_init(void);

int ingenic_felix_deinit(void);

void * ingenic_felix_ctx_init(void);

int ingenic_felix_ctx_deinit(void *ctx);

int ingenic_felix_vpu_start(void *priv);

int ingenic_felix_vpu_wait(void *priv);

int ingenic_felix_vpu_stop(void *priv);

#endif
