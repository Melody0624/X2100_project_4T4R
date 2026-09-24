#include <io.h>
#include <config.h>
//#include <serial.h>
#include <common.h>
//#include <soc/gpio.h>
#include <driver/clk.h>
//#include <driver/gpio.h>
#include <driver/cache.h>
#include <driver/irq.h>

#include "felix_drv.h"

static thread_waiter_t waiter;

static struct ingenic_vdec_dev *g_dev;

#define REG(addr) *((volatile unsigned int*)(addr))

static unsigned int vpu_readl(struct ingenic_vdec_dev *dev, unsigned int offset)
{
	unsigned int val;
	val = REG(dev->reg_base + offset);
	return val;
}
static void vpu_writel(struct ingenic_vdec_dev *dev, unsigned int offset, unsigned int value)
{
	REG(dev->reg_base + offset) = value;
}

static unsigned int cpm_readl(struct ingenic_vdec_dev *dev, unsigned int offset)
{
	unsigned int val;
	val = REG(dev->cpm_base + offset);
	return val;
}
static void cpm_writel(struct ingenic_vdec_dev *dev, unsigned int offset, unsigned int value)
{
	REG(dev->cpm_base + offset) = value;
}

static void inline vpu_clear_bits(struct ingenic_vdec_dev *dev, unsigned int offset, unsigned int bits)
{
	unsigned int val = vpu_readl(dev, offset);
	val &= ~bits;
	vpu_writel(dev, offset, val);
}

static void vpu_dump_regs(struct ingenic_vdec_dev *dev)
{
#if 1
	printf("======================================= >>>>>>>>>> %s\n",__func__);
	printf("REG_SDE_STAT:           0x%x\n", vpu_readl(dev, REG_SDE_STAT));   //0x13290000
	printf("REG_SDE_CFG0:           0x%x\n", vpu_readl(dev, REG_SDE_CFG0));   //0x13290014
	printf("REG_SDE_CFG1:           0x%x\n", vpu_readl(dev, REG_SDE_CFG1));   //0x13290018
	printf("REG_SDE_CFG13:          0x%x\n", vpu_readl(dev, REG_SDE_CFG13));  //0x13290048
	printf("REG_SDE_CFG14:          0x%x\n", vpu_readl(dev, REG_SDE_CFG14));  //0x1329004c
	printf("REG_DBLK_GSTA:          0x%x\n", vpu_readl(dev, REG_DBLK_GSTA));  //0x13270070
	printf("REG_VMAU_POS:           0x%x\n", vpu_readl(dev, REG_VMAU_POS));   //0x13280060
	printf("REG_SCH_GLBC:           0x%x\n", vpu_readl(dev, REG_SCH_GLBC));   //0x13200000
	printf("REG_SCH_STAT:           0x%x\n", vpu_readl(dev, REG_SCH_STAT));   //0x13200034
	printf("REG_VDMA_TASKRG:        0x%x\n", vpu_readl(dev, REG_VDMA_TASKRG));//0x13210008
	printf("REG_VDMA_TASKST:        0x%x\n", vpu_readl(dev, REG_VDMA_TASKST));//0x1321000c
#endif
}

void vpu_dump_data(char* pack, unsigned int size)
{
	int i = 0;
	printf("================> dump buf data <================\n");
	printf("pack : %p, size : %d\n", pack, size);
	for(i = 0; i < size; i++){
		if((i != 0) && (i % 8 == 0))
			printf("%02x \n",pack[i]);
		else
			printf("%02x ",pack[i]);
	}
	printf("\n");
	printf("================> dump      end <================\n");
}

static void ingenic_vpu_irq(int irq, void *priv)
{
	struct ingenic_vdec_dev *dev = (struct ingenic_vdec_dev *)priv;
	struct ingenic_vdec_ctx *ctx = dev->curr_ctx;

	H264Context *h = ctx->h;
	vpu_ctx_t *vpu_ctx = &h->vpu_ctx;

	unsigned long flags;

	unsigned int vpu_stat;
	unsigned int sde_stat;
	int err = 0;

#define check_vpu_status(STAT, fmt, args...) do {       \
		if(vpu_stat & STAT)                     \
		printf(fmt, ##args);				\
	}while(0)

	spin_lock_irqsave(&dev->spinlock, flags);

	vpu_stat = vpu_readl(dev, REG_SCH_STAT);
	sde_stat = vpu_readl(dev, REG_SDE_STAT);

	if(vpu_stat) {
		if(vpu_stat & VPU_STAT_ENDF) {
			if(vpu_stat & VPU_STAT_JPGEND) {
				printf("JPG successfully done!\n");
				vpu_stat = vpu_readl(dev, REG_VPU_JPGC_STAT);
				vpu_clear_bits(dev, REG_VPU_JPGC_STAT, JPGC_STAT_ENDF);
			} else {
				printf("SCH successfully done!\n");
				vpu_clear_bits(dev, REG_VPU_SDE_STAT, SDE_STAT_BSEND);
				vpu_clear_bits(dev, REG_VPU_DBLK_STAT, DBLK_STAT_DOEND);
			}
		} else {
			err = 1;
			check_vpu_status(VPU_STAT_SLDERR, "SHLD error!\n");
			check_vpu_status(VPU_STAT_TLBERR, "TLB error! Addr is 0x%08x\n",
					vpu_readl(dev, REG_VPU_STAT));
			check_vpu_status(VPU_STAT_BSERR, "BS error!\n");
			check_vpu_status(VPU_STAT_ACFGERR, "ACFG error!\n");
			check_vpu_status(VPU_STAT_TIMEOUT, "TIMEOUT error!\n");
			vpu_clear_bits(dev, REG_VPU_GLBC, (VPU_INTE_ACFGERR |
						VPU_INTE_TLBERR | VPU_INTE_BSERR |
						VPU_INTE_ENDF));
		}
	} else {
		if(vpu_readl(dev, REG_VPU_AUX_STAT) & AUX_STAT_MIRQP) {
			printf("AUX successfully done!\n");
			vpu_clear_bits(dev, REG_VPU_AUX_STAT, AUX_STAT_MIRQP);
		} else {
			printf("illegal interrupt happened!\n");
			return;
		}
	}

	ctx->int_cond = 1;
	vpu_ctx->error = err;
	vpu_ctx->sch_stat = vpu_stat;
	vpu_ctx->sde_stat = sde_stat;
	spin_unlock_irqrestore(&dev->spinlock, flags);

	thread_waiter_wakeup(&waiter);
	return;
}

static void vpu_irq_init(void *dev)
{
	request_irq_disabled(IRQ_FELIX, 0, ingenic_vpu_irq, "felix-irq", (void *)dev);
	return;
}

static void vpu_irq_deinit(void)
{
	release_irq(IRQ_FELIX);
	return;
}

static int vpu_reset_x2000(struct ingenic_vdec_dev *dev)
{
	int stat;
	int timeout = 0xffff;
	vpu_writel(dev, REG_CFGC_SW_RESET, REG_CFGC_RST);
	while(((*(volatile unsigned int*)0xb330000d) & REG_CFGC_EARB_EMPT) && --timeout);
	if(!timeout) {
		printf("vpu reset timeout!\n");
		return -EINVAL;
	}

	return 0;
}

/*global clk on.*/
int vpu_on(void)
{
	*(volatile unsigned int*)0xb0000020 = 0;
	*(volatile unsigned int*)0xb0000028 = 0;
	__asm__ __volatile__ (
			"mfc0  $2, $16,  7   \n\t"
			"ori   $2, $2, 0x340 \n\t"
			"andi  $2, $2, 0x3ff \n\t"
			"mtc0  $2, $16,  7  \n\t"
			"nop                  \n\t");

	return 0;
}

/* shutdown */
static int vpu_off(void)
{
	unsigned int tmp = *(volatile unsigned int*)0xb0000028;
	tmp |= 1 << 4;
	*(volatile unsigned int*)0xb0000028 = tmp;

	return 0;
}

int ingenic_felix_vpu_start(void *priv)
{
	// 在这里只控制硬件的启动工作，不做等待
	// 中断禁用
	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx *)priv;
	struct ingenic_vdec_dev *dev = ctx->dev;
	H264Context *h = ctx->h;
	vpu_ctx_t *vpu_ctx = &h->vpu_ctx;

	unsigned long flags;
	unsigned int sch_glbc = 0;
	unsigned int des_pa;

	flush_cache_all();
	des_pa = vpu_ctx->desc_pa;
	spin_lock_irqsave(&dev->spinlock, flags);
	dev->curr_ctx = ctx;
	ctx->int_cond = 0;
	enable_irq(IRQ_FELIX);

	vpu_reset_x2000(dev);

	sch_glbc = SCH_GLBC_HIAXI | SCH_INTE_ACFGERR | SCH_INTE_BSERR | SCH_INTE_ENDF;

	vpu_writel(dev, REG_SCH_GLBC, sch_glbc);
	/*trigger start.*/
	vpu_writel(dev, REG_VDMA_TASKRG, VDMA_ACFG_DHA(des_pa) | VDMA_ACFG_RUN);

	/* wait event time out ... */
	spin_unlock_irqrestore(&dev->spinlock, flags);

	//vpu_dump_regs(dev);
	{
		int timeout_ms = 500;
		timeout_ms = thread_waiter_wait_timeout(&waiter, timeout_ms);
		if (timeout_ms)
			printf("thread_waiter_wait_timeout() ret=%d\n", timeout_ms);
		disable_irq(IRQ_FELIX);
	}

	return 0;
}

int ingenic_felix_vpu_wait(void *priv)
{
	return 0;
}

int ingenic_felix_vpu_stop(void *priv)
{
	// 无操作
	return 0;
}

struct vpu_ops ingenic_vpu_ops = {
	.start = ingenic_felix_vpu_start,
	.wait = ingenic_felix_vpu_wait,
	.end = ingenic_felix_vpu_stop,
};


int ingenic_felix_init(void)
{
	unsigned int val = 0;
	struct ingenic_vdec_dev *dev;
	dev = (struct ingenic_vdec_dev *)malloc(sizeof(struct ingenic_vdec_dev));
	dev->reg_base = (void *)KSEG1ADDR(VPU_BASE);
	dev->cpm_base = (void *)KSEG1ADDR(CPM_BASE);

	val = cpm_readl(dev,REG_CPM_LCR);
	val &= ~(1 << 29);
	cpm_writel(dev,REG_CPM_LCR, val);
	g_dev = dev;

	spin_lock_init(&dev->spinlock);

	return 0;
}

int ingenic_felix_deinit(void)
{
	free(g_dev);
	g_dev = NULL;

	return 0;
}

extern void ingenic_vcodec_felix_work_thread(void *data);
void* ingenic_felix_ctx_init(void)
{
	int ret = 0;

	// felix low power disable
	// s1 some struct resource prepare

	struct ingenic_vdec_ctx *ctx = (struct ingenic_vdec_ctx*)malloc(sizeof(struct ingenic_vdec_ctx));
	memset(ctx, 0, sizeof(struct ingenic_vdec_ctx));

	ctx->avctx = malloc(sizeof(AVCodecContext));
	memset(ctx->avctx, 0, sizeof(AVCodecContext));

	ctx->h = malloc(sizeof(H264Context));
	memset(ctx->h, 0, sizeof(H264Context));

	ctx->avctx->priv_data = ctx->h;

	ctx->dev = g_dev;

	thread_waiter_init(&waiter);
	vpu_irq_init(g_dev);

	spin_lock_init(&ctx->src_buf_lock);
	spin_lock_init(&ctx->dst_buf_lock);

	sem_init(&ctx->src_buf_queued_sem, 0, 0);
	sem_init(&ctx->dst_buf_queued_sem, 0, 0);
	sem_init(&ctx->src_buf_done_sem, 0, 0);
	sem_init(&ctx->dst_buf_done_sem, 0, 0);

	ctx->work_thread = thread_create("h264_decode_worker",  2048, ingenic_vcodec_felix_work_thread, ctx);

	ret = h264_decode_init(ctx->avctx);
	if(ret < 0){
		free(ctx->h);
		free(ctx->avctx);
		free(ctx);
		return NULL;
	}

	return (void *)ctx;
}

int ingenic_felix_ctx_deinit(void *ctx)
{
	struct ingenic_vdec_ctx *vdec_ctx = (struct ingenic_vdec_ctx *)ctx;
	int i;
	felix_buffer_t *buf = NULL;
	// TODO Deinit
	// S1 close clk
	// S2 deinit decoder
	// S3 vpu stop
	// S4 release struct resource
	vpu_irq_deinit();
	h264_decode_end(vdec_ctx->avctx);

	for(i = 0; i < 3; i++){
		buf = &vdec_ctx->src_buf[i];
		if(buf->vaddr)
			av_free((void *)buf->vaddr);

		buf = &vdec_ctx->dst_buf[i];
		if(buf->vaddr)
			av_free((void *)buf->vaddr);

		av_buffer_del(vdec_ctx->src_buf[i].frame.buf[0]);
		av_buffer_del(vdec_ctx->dst_buf[i].frame.buf[0]);
		av_buffer_del(vdec_ctx->dst_buf[i].frame.buf[1]);
	}


	free(vdec_ctx->h);
	free(vdec_ctx->avctx);
	free(vdec_ctx);
	return 0;
}

