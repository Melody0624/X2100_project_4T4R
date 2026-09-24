/*
* Copyright© 2014 Ingenic Semiconductor Co.,Ltd
*
* Author: qipengzhen <aric.pzqi@ingenic.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
*/

#include <driver/irq.h>
#include "helix_drv.h"
#include "helix_ops.h"
#include "videodev2.h"

//#include "helix_ops.h"

#define REG_CPM_CLKGR 0x20
#define REG_CPM_CLKGR1 0x28
#define CPM_CLKGR_JPEG           (0x1<<12)
#define REG_CPM_LCR 0x4

static thread_waiter_t waiter;
static DEFINE_MUTEX(lock);

static struct ingenic_venc_dev *g_dev;


#define REG(addr) *((volatile unsigned int*)(addr))

static unsigned int vpu_readl(struct ingenic_venc_dev *dev, unsigned int offset)
{
	unsigned int val;
	val = REG(dev->reg_base + offset);
	/* printf("read:base_addr + offset %x = %x\n", base_addr + offset, val); */
	return val;
}
static void vpu_writel(struct ingenic_venc_dev *dev, unsigned int offset, unsigned int value)
{
	REG(dev->reg_base + offset) = value;
}

static unsigned int cpm_readl(struct ingenic_venc_dev *dev, unsigned int offset)
{
	unsigned int val;
	val = REG(dev->cpm_base + offset);
	/* printf("read:base_addr + offset %x = %x\n", base_addr + offset, val); */
	return val;
}
static void cpm_writel(struct ingenic_venc_dev *dev, unsigned int offset, unsigned int value)
{
	REG(dev->cpm_base + offset) = value;
}

static int vpu_on(struct ingenic_venc_dev *dev)
{
	unsigned int val = 0;
	val = cpm_readl(dev ,REG_CPM_CLKGR);
	val &= ~CPM_CLKGR_JPEG;
	cpm_writel(dev,REG_CPM_CLKGR, val);
	val = cpm_readl(dev ,REG_CPM_CLKGR);

	return 0;
}

static int vpu_off(struct ingenic_venc_dev *dev)
{
	unsigned int val = 0;
	val = cpm_readl(dev ,REG_CPM_CLKGR);
	val |= CPM_CLKGR_JPEG;
	cpm_writel(dev,REG_CPM_CLKGR, val);

	return 0;
}

static void inline vpu_clear_bits(struct ingenic_venc_dev *dev, unsigned int offset, unsigned int bits)
{
	unsigned int val = vpu_readl(dev, offset);
	val &= ~bits;
	vpu_writel(dev, offset, val);
}

static void vpu_dump_regs(struct ingenic_venc_dev *dev)
{

	/* EMC */
	printf("REG_EMC_FRM_SIZE :	%d\n", vpu_readl(dev, REG_EMC_FRM_SIZE));
	printf("REG_EMC_BS_ADDR :	0x%x\n", vpu_readl(dev, REG_EMC_BS_ADDR));
	printf("REG_EMC_DBLK_ADDR:	0x%x\n", vpu_readl(dev, REG_EMC_DBLK_ADDR));
	printf("REG_EMC_RECON_ADDR:	0x%x\n", vpu_readl(dev, REG_EMC_RECON_ADDR));
	printf("REG_EMC_MV_ADDR:	0x%x\n", vpu_readl(dev, REG_EMC_MV_ADDR));
	printf("REG_EMC_SE_ADDR:	0x%x\n", vpu_readl(dev, REG_EMC_SE_ADDR));
	printf("REG_EMC_QPT_ADDR:	0x%x\n", vpu_readl(dev, REG_EMC_QPT_ADDR));
	printf("REG_EMC_RC_RADDR:	0x%x\n", vpu_readl(dev, REG_EMC_RC_RADDR));
	printf("REG_EMC_MOS_ADDR:	0x%x\n", vpu_readl(dev, REG_EMC_MOS_ADDR));
	printf("REG_EMC_SLV_INIT:	0x%x\n", vpu_readl(dev, REG_EMC_SLV_INIT));
	printf("REG_EMC_BS_SIZE:	0x%x\n", vpu_readl(dev, REG_EMC_BS_SIZE));
	printf("REG_EMC_BS_STAT:	0x%x\n", vpu_readl(dev, REG_EMC_BS_STAT));
}

static void vpu_intr_handler(int irq, void *priv)
{
	struct ingenic_venc_dev *dev = (struct ingenic_venc_dev *)priv;
	struct ingenic_venc_ctx *ctx = dev->curr_ctx;
	struct h264e_ctx *h264e_ctx = &ctx->h264e_ctx;
	struct jpge_ctx *jpge_ctx = &ctx->jpge_ctx;

	unsigned long flags;

	unsigned int efe_stat;
	unsigned int sch_stat;

	spin_lock_irqsave(&dev->spinlock, flags);

	//ctx->int_cond = 1;

	efe_stat = vpu_readl(dev, REG_EFE_STAT);
	sch_stat = vpu_readl(dev, REG_SCH_STAT);

	/* disable all interrupts. */
	vpu_clear_bits(dev, REG_SCH_GLBC, 0x3f << 16);
	if(sch_stat & SCH_STAT_ENDF){
		if(sch_stat & SCH_STAT_JPGEND) {

			jpge_ctx->bslen = vpu_readl(dev, REG_JPGC_STAT) & 0xffffff;

			vpu_clear_bits(dev, REG_JPGC_STAT, JPGC_STAT_ENDF);
		} else if(sch_stat & SCH_STAT_ENDF) {
			h264e_ctx->r_bs_len = vpu_readl(dev, REG_SDE_CFG9);
			h264e_ctx->r_rbsp_len = vpu_readl(dev, REG_SDE_CFG10);
			h264e_ctx->encoded_bs_len = h264e_ctx->r_bs_len;

			vpu_clear_bits(dev, REG_SDE_STAT, SDE_STAT_BSEND);
			vpu_clear_bits(dev, REG_DBLK_GSTA, DBLK_STAT_DOEND);
			/*wakeup ...*/
		} else if(sch_stat & SCH_STAT_TIMEOUT){
			//	dev_err(ctx->dev->dev, "Sch h264 Timeout !\n");
			printf("Sch h264 Timeout !\n");
		} else {
			/*Error handling ...!*/
			//	dev_err(ctx->dev->dev, "stat error %x\n", sch_stat);
			printf("stat error %x\n", sch_stat);
			/*wakeup ...*/
		}

	}
	ctx->int_status = sch_stat;

	spin_unlock_irqrestore(&dev->spinlock, flags);

	thread_waiter_wakeup(&waiter);

	return;
}

static void vpu_irq_init(void *dev)
{
	request_irq_disabled(IRQ_HELIX, 0, vpu_intr_handler, "vpu-irq", (void *)dev);
	return;
}

static void vpu_irq_deinit(void)
{
	disable_irq(IRQ_HELIX);
	release_irq(IRQ_HELIX);
	return;
}



#define VPU_RUN_TIMEOUT_MS	(3000)

//struct ingenic_venc_dev venc_dev;

int helix_init(struct ingenic_venc_dev *venc_dev)
{
	venc_dev->reg_base = (void *)KSEG1ADDR(VPU_BASE);
	venc_dev->cpm_base = (void *)KSEG1ADDR(CPM_BASE);

	return 0;
}

static int helix_power_on(struct ingenic_venc_dev *dev)
{
	unsigned int val;
	val = cpm_readl(dev,REG_CPM_LCR);
	printf("%s() REG_CPM_LCR=%x\n", __func__, val);
	val &= ~(1<<28);
	cpm_writel(dev,REG_CPM_LCR, val);
	do {
		val = cpm_readl(dev,REG_CPM_LCR);
		printf("%s() REG_CPM_LCR=%x\n", __func__, val);
	}while (val&(1<<24));


	__asm__ __volatile__ (
			"mfc0  $2, $16,  7   \n\t"
			"ori   $2, $2, 0x340 \n\t"
			"andi  $2, $2, 0x3ff \n\t"
			"mtc0  $2, $16,  7  \n\t"
			"nop                  \n\t");


	return 0;
}

void ingenic_vpu_lock(void)
{
	mutex_lock(&lock);
}

void ingenic_vpu_unlock(void)
{
	mutex_unlock(&lock);
}

int ingenic_vpu_start(void *priv)
{
	struct ingenic_venc_ctx *ctx = (struct ingenic_venc_ctx *)priv;
	struct ingenic_venc_dev *dev = ctx->dev;
	struct h264e_ctx *h264e_ctx = &ctx->h264e_ctx;
	struct jpge_ctx *jpge_ctx = &ctx->jpge_ctx;
	struct jpgd_ctx *jpgd_ctx = &ctx->jpgd_ctx;
	unsigned int sch_glbc = 0;
	unsigned int des_pa = 0;
	unsigned long flags;
	int timeout = 0xffff;
	int ret = 0;

	/* TODO: add lock.*/
	dev->curr_ctx = ctx;

	if(ctx->codec_id == CODEC_ID_H264E) {
		des_pa = h264e_ctx->desc_pa;
	} else if(ctx->codec_id == CODEC_ID_JPGE) {
		des_pa = jpge_ctx->desc_pa;
	} else if(ctx->codec_id == CODEC_ID_JPGD) {
		des_pa = jpgd_ctx->desc_pa;
	}

	vpu_on(dev);
	enable_irq(IRQ_HELIX);

	spin_lock_irqsave(&dev->spinlock, flags);
//	ctx->int_cond = 0;

	/*X2000 RESET*/
	/* vpu reset ... */
	vpu_writel(dev, REG_CFGC_SW_RESET, CFGC_SW_RESET_RST);
	while(!(vpu_readl(dev, REG_CFGC_SW_RESET) & CFGC_SW_RESET_EARB_EMPT) && --timeout);
	if(!timeout) {
		printf("%s, vpu_reset timeout!\n", __func__);
		spin_unlock_irqrestore(&dev->spinlock, flags);
		return -EINVAL;
	}

	sch_glbc = SCH_GLBC_HIAXI | SCH_INTE_ACFGERR | SCH_INTE_BSERR |
		SCH_INTE_ENDF | SCH_INTE_TLBERR | SCH_INTE_BSFULL;

	/* type jpege, jpegd, h264e.*/
	vpu_writel(dev, REG_SCH_GLBC, sch_glbc);

	/*trigger start.*/
	vpu_writel(dev, REG_CFGC_ACM_CTRL, VDMA_ACFG_DHA(des_pa) | VDMA_ACFG_RUN);

	/* wait event time out ... */
	spin_unlock_irqrestore(&dev->spinlock, flags);

	{
		int timeout_ms = 500;
		timeout_ms = thread_waiter_wait_timeout(&waiter, timeout_ms);
		if (timeout_ms)
			printf("thread_waiter_wait_timeout() ret=%d\n", timeout_ms);
		disable_irq(IRQ_HELIX);
	}

	//vpu_dump_regs(dev);
	vpu_off(dev);

	return ret;
}

int ingenic_vpu_stop(struct ingenic_venc_dev *dev)
{
	return 0;
}

static int init_cnt = 0;

int ingenic_helix_init(void)
{
	ingenic_vpu_lock();

	init_cnt++;
	if (init_cnt != 1) {
		ingenic_vpu_unlock();
		return 0;
	}

	struct ingenic_venc_dev *venc_dev;
	venc_dev = malloc(sizeof(struct ingenic_venc_dev));
	helix_init(venc_dev);
	helix_power_on(venc_dev);

	g_dev = venc_dev;

	spin_lock_init(&venc_dev->spinlock);

	thread_waiter_init(&waiter);
	vpu_irq_init(g_dev);

	ingenic_vpu_unlock();

	return 0;
}

int ingenic_helix_deinit(void)
{
	ingenic_vpu_lock();
	init_cnt--;
	if (init_cnt != 0) {
		ingenic_vpu_unlock();
		return 0;
	}

//	helix_deinit(venc_dev);
//	helix_power_off(venc_dev);

	vpu_irq_deinit();

	free(g_dev);
	g_dev = NULL;

	ingenic_vpu_unlock();

	return 0;
}


extern void ingenic_vcodec_helix_work_thread(void *data);
void *ingenic_helix_ctx_init(unsigned int fmt, int codec_type)
{
	struct ingenic_venc_ctx *ctx;
	ctx = malloc(sizeof(struct ingenic_venc_ctx));
	memset(ctx, 0x00, sizeof(struct ingenic_venc_ctx));

	switch(fmt) {
		case V4L2_PIX_FMT_H264:
			//h264_enc_ctx_init(&ctx->h264e_ctx);
			h264e_set_priv(&ctx->h264e_ctx, ctx);
			ctx->codec_id = CODEC_ID_H264E;
			break;
		case V4L2_PIX_FMT_JPEG:
			if(!codec_type) {
				jpeg_encoder_set_priv(&ctx->jpge_ctx, ctx);
				ctx->codec_id = CODEC_ID_JPGE;
			} else {
				jpeg_decoder_set_priv(&ctx->jpgd_ctx, ctx);
				ctx->codec_id = CODEC_ID_JPGD;
			}
			break;
		default:
			break;
	}

	ctx->dev = g_dev;

	thread_waiter_init(&ctx->work_waiter);
	spin_lock_init(&ctx->src_buf_lock);
	spin_lock_init(&ctx->dest_buf_lock);

	sem_init(&ctx->src_buf_queued_sem, 0, 0);
	sem_init(&ctx->dest_buf_queued_sem, 0, 0);
	sem_init(&ctx->src_buf_done_sem, 0, 0);
	sem_init(&ctx->dest_buf_done_sem, 0, 0);

	ctx->work_thread = thread_create("h264_encode_worker",  2048, ingenic_vcodec_helix_work_thread, ctx);

	return ctx;
}

int ingenic_helix_ctx_deinit(void *data)
{
	struct ingenic_venc_ctx *ctx = data;
	struct h264e_ctx *h264e_ctx = &ctx->h264e_ctx;
	struct jpge_ctx *jpge_ctx = &ctx->jpge_ctx;
	struct jpgd_ctx *jpgd_ctx = &ctx->jpgd_ctx;

	switch(ctx->codec_id) {
		case CODEC_ID_H264E:
			h264e_encoder_deinit(h264e_ctx);
			break;
		case CODEC_ID_JPGE:
			jpeg_encoder_deinit(jpge_ctx);
			break;
		case CODEC_ID_JPGD:
			jpeg_decoder_deinit(jpgd_ctx);
			break;
		default:
			break;
	}

	thread_delete(ctx->work_thread);
	sem_destroy(&ctx->src_buf_queued_sem);
	sem_destroy(&ctx->dest_buf_queued_sem);
	sem_destroy(&ctx->src_buf_done_sem);
	sem_destroy(&ctx->dest_buf_done_sem);

	ingenic_vcodec_helix_destroy_srcbuf(ctx);
	ingenic_vcodec_helix_destroy_destbuf(ctx);

	free(ctx);
	ctx = NULL;

	return 0;
}


