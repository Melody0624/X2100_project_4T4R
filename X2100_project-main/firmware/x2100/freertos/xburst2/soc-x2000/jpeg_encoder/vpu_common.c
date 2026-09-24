#include <stdio.h>
#include <stdlib.h>
#include <common.h>
#include <soc/base.h>
#include <asm/addrspace.h>
#include <driver/cache.h>
#include <os/thread_waiter.h>
#include <driver/irq.h>

#include <helix.h>
#include <jpeg-hw.h>
#include "vpu_common.h"

#if defined(CONFIG_SOC_X2000)
#define IRQ_JPEG    IRQ_HELIX
#endif

#define IRQ_THREAD_WAITER

thread_waiter_t waiter;


static void dump_intc_regs(int irq, void *dev);

#define REG(addr) *((volatile unsigned int*)(addr))
static unsigned int read_reg(unsigned int vpu_base,unsigned int offset)
{
    unsigned int val;
    val = REG(vpu_base + offset);
    /* printf("read:base_addr + offset %x = %x\n", base_addr + offset, val); */
    return val;
}
static void write_reg(unsigned int vpu_base,unsigned int offset, unsigned int value)
{
    REG(vpu_base + offset) = value;
}

#if 0
static inline void *reg_mmap(int vpu_fd, unsigned int size, unsigned int offset)
{
    void * vaddr = mmap((void *)0, size, PROT_READ | PROT_WRITE, MAP_SHARED, vpu_fd, offset);
    if (vaddr == MAP_FAILED) {
        printf("Map 0x%08x addr with 0x%x size failed.\n", offset, size);
        return NULL;
    }
    return vaddr;
}
#else
static inline void *reg_mmap(int vpu_fd, unsigned int size, unsigned int offset)
{
    return (void*) KSEG1ADDR(offset);
}
#endif

static int vpu_reg_mmap(struct vpu_struct *vpu)
{
    vpu->vpu_base = (unsigned int)reg_mmap(vpu->vpu_fd, VPU_SIZE, VPU_BASE);
    vpu->cpm_base = (unsigned int)reg_mmap(vpu->vpu_fd, CPM_SIZE, CPM_BASE);

    printf("VAE mmap successfully done! vpu_base = 0x%x\n", (unsigned int)vpu->vpu_base);
    printf("VAE mmap successfully done! cpm_base = 0x%x\n", (unsigned int)vpu->cpm_base);
    return 0;
}
static void vpu_reg_unmap(struct vpu_struct *vpu)
{
/*
    munmap((void*)vpu->vpu_base, VPU_SIZE);
    munmap((void*)vpu->cpm_base, CPM_SIZE);
    printf("VAE unmap successfully done!\n");
*/
}

static unsigned int cpm_read_reg(unsigned int base_addr,unsigned int offset)
{
    unsigned int val;
    val = REG(base_addr + offset);
    /* printf("read:base_addr + offset %x = %x\n", base_addr + offset, val); */
    return val;
}
static void cpm_write_reg(unsigned int base_addr,unsigned int offset, unsigned int value)
{
    REG(base_addr + offset) = value;
}

#if defined(CONFIG_SOC_X2000)
/*
  copy from kernel-4.4.94/module_drivers/drivers/media/platform/ingenic-vcodec/helix/helix_drv.c
 */
static int vpu_power_on(struct vpu_struct *vpu)
{
    unsigned int val;
    val = cpm_read_reg(vpu->cpm_base,REG_CPM_LCR);
    printf("%s() REG_CPM_LCR=%x\n", __func__, val);
    val &= ~(28);
    cpm_write_reg(vpu->cpm_base,REG_CPM_LCR, val);
    do {
        val = cpm_read_reg(vpu->cpm_base,REG_CPM_LCR);
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
static int vpu_clock_start(struct vpu_struct *vpu)
{
    unsigned int val;

    val = cpm_read_reg(vpu->cpm_base,REG_CPM_CLKGR);
    val &= ~CPM_CLKGR_JPEG;
    cpm_write_reg(vpu->cpm_base,REG_CPM_CLKGR, val);

/*
    val = cpm_read_reg(vpu->cpm_base,REG_CPM_CLKGR);
    printf("%s() REG_CPM_CLKGR=%x\n", __func__, val);
    val = cpm_read_reg(vpu->cpm_base,REG_CPM_CLKGR1);
    printf("%s() REG_CPM_CLKGR1=%x\n", __func__, val);
*/
/*  //debug: mask all clock gate
    cpm_write_reg(vpu->cpm_base,REG_CPM_CLKGR, 0);
    cpm_write_reg(vpu->cpm_base,REG_CPM_CLKGR1, 0);
    val = cpm_read_reg(vpu->cpm_base,REG_CPM_CLKGR);
    printf("%s() REG_CPM_CLKGR=%x\n", __func__, val);
    val = cpm_read_reg(vpu->cpm_base,REG_CPM_CLKGR1);
    printf("%s() REG_CPM_CLKGR1=%x\n", __func__, val);
*/
    return 0;
}

static int vpu_clock_stop(struct vpu_struct *vpu)
{
    unsigned int val;
    val = cpm_read_reg(vpu->cpm_base,REG_CPM_CLKGR);
    val |= CPM_CLKGR_JPEG;
    cpm_write_reg(vpu->cpm_base,REG_CPM_CLKGR, val);
    return 0;
}

/* felix reset */
__attribute__((__unused__)) static void vpu_reset1(struct vpu_struct *vpu)
{

    unsigned int val;
    /* return ioctl(vpu_fd, CMD_VPU_RESET, 0); */
    val = cpm_read_reg(vpu->cpm_base,REG_CPM_SRBC);
    val |= CPM_VPU1_STP;
    cpm_write_reg(vpu->cpm_base,REG_CPM_SRBC, val);

    while(!(cpm_read_reg(vpu->cpm_base,REG_CPM_SRBC) & CPM_VPU1_ACK))
        ;
    val = cpm_read_reg(vpu->cpm_base,REG_CPM_SRBC);
    val |= CPM_VPU1_SR;
    val &= ~CPM_VPU1_STP;
    cpm_write_reg(vpu->cpm_base,REG_CPM_SRBC, val);

    val = cpm_read_reg(vpu->cpm_base,REG_CPM_SRBC);
    val &= ~CPM_VPU1_SR;
    val &= ~CPM_VPU1_STP;
    cpm_write_reg(vpu->cpm_base,REG_CPM_SRBC, val);
}

static int vpu_reset_x2000(struct vpu_struct *vpu)
{
    int timeout = 0xffff;
    /*X2000 VPU RESET*/
    /* vpu reset ... */
    write_reg(vpu->vpu_base, REG_CFGC_SW_RESET, CFGC_SW_RESET_RST);
    while(!(read_reg(vpu->vpu_base, REG_CFGC_SW_RESET) & CFGC_SW_RESET_EARB_EMPT) && --timeout);
    if(!timeout) {
        printf("%s, vpu_reset timeout!\n", __func__);
        return -EINVAL;
    }
    return 0;
}

/* helix reset */
static void vpu_reset(struct vpu_struct *vpu)
{

    unsigned int val;
    //vpu_reset1(vpu);
    /* return ioctl(vpu_fd, CMD_VPU_RESET, 0); */
    val = cpm_read_reg(vpu->cpm_base,REG_CPM_SRBC);
    val |= CPM_VPU_STP;
    cpm_write_reg(vpu->cpm_base,REG_CPM_SRBC, val);

    while(!(cpm_read_reg(vpu->cpm_base,REG_CPM_SRBC) & CPM_VPU_ACK))
        ;
    val = cpm_read_reg(vpu->cpm_base,REG_CPM_SRBC);
    val |= CPM_VPU_SR;
    val &= ~CPM_VPU_STP;
    cpm_write_reg(vpu->cpm_base,REG_CPM_SRBC, val);

    val = cpm_read_reg(vpu->cpm_base,REG_CPM_SRBC);
    val &= ~CPM_VPU_SR;
    val &= ~CPM_VPU_STP;
    cpm_write_reg(vpu->cpm_base,REG_CPM_SRBC, val);
}
#else
static int vpu_clock_start(struct vpu_struct *vpu)
{
    unsigned int val;
    val = cpm_read_reg(vpu->cpm_base,REG_CPM_CLKGR);
    val &= ~CPM_CLKGR_JPEG;
    cpm_write_reg(vpu->cpm_base,REG_CPM_CLKGR, val);
    return 0;
}

static int vpu_clock_stop(struct vpu_struct *vpu)
{
    unsigned int val;
    val = cpm_read_reg(vpu->cpm_base,REG_CPM_CLKGR);
    val |= CPM_CLKGR_JPEG;
    cpm_write_reg(vpu->cpm_base,REG_CPM_CLKGR, val);
    return 0;
}

static void vpu_reset(struct vpu_struct *vpu)
{

    unsigned int val;
    /* return ioctl(vpu_fd, CMD_VPU_RESET, 0); */
    val = cpm_read_reg(vpu->cpm_base,REG_CPM_SRBC);
    val |= CPM_VPU_STP;
    cpm_write_reg(vpu->cpm_base,REG_CPM_SRBC, val);

    while(!(cpm_read_reg(vpu->cpm_base,REG_CPM_SRBC) & CPM_VPU_ACK))
        ;
    val = cpm_read_reg(vpu->cpm_base,REG_CPM_SRBC);
    val |= CPM_VPU_SR;
    val &= ~CPM_VPU_STP;
    cpm_write_reg(vpu->cpm_base,REG_CPM_SRBC, val);

    val = cpm_read_reg(vpu->cpm_base,REG_CPM_SRBC);
    val &= ~CPM_VPU_SR;
    val &= ~CPM_VPU_STP;
    cpm_write_reg(vpu->cpm_base,REG_CPM_SRBC, val);
}
#endif

static int jz_flush_cache(struct vpu_struct *vpu,unsigned int vaddr, unsigned int size)
{
    flush_dcache((unsigned long)vaddr, size);
    return 0;
}
static int jz_invalidate_cache(struct vpu_struct *vpu,unsigned int vaddr, unsigned int size)
{
    invalidate_dcache((unsigned long)vaddr, size);
    return 0;
}

static int jz_start_hw_compress(void *handle,unsigned int des_va,unsigned int des_pa)
{
    int bslen;
#ifndef IRQ_THREAD_WAITER
    int wait_time = 0;
#endif
    unsigned int val;
    struct jz_jpeg_encode *jz_jpeg = (struct jz_jpeg_encode *)handle;
    struct vpu_struct *vpu = (struct vpu_struct *)jz_jpeg->vpu;

    /* flush_cache */
    jz_flush_cache(vpu, (unsigned int)jz_jpeg->yuyv_info.buf[0], jz_jpeg_encode_get_y_buffer_size(jz_jpeg));
    jz_flush_cache(vpu, (unsigned int)jz_jpeg->yuyv_info.buf[1], jz_jpeg_encode_get_uv_buffer_size(jz_jpeg));
    jz_invalidate_cache(vpu, (unsigned int)jz_jpeg->yuyv_info.BitStreamBuf, jz_jpeg_encode_get_BitStreamBuf_size(jz_jpeg));
    jz_flush_cache(vpu, (unsigned int)des_va, JPEG_DMA_DESC_SIZE);

    /* VPU reset */
    vpu_reset(vpu);
    vpu_reset_x2000(vpu);

    /* clear status */
    write_reg(vpu->vpu_base,REG_JPGC_STAT, 0);

#ifdef IRQ_THREAD_WAITER
    /* enable_irq */
    enable_irq(IRQ_JPEG);
#endif

    val = SCH_GLBC_HIAXI | SCH_INTE_ACFGERR | SCH_INTE_BSERR |
            SCH_INTE_ENDF | SCH_INTE_TLBERR | SCH_INTE_BSFULL;
    write_reg(vpu->vpu_base,REG_SCH_GLBC, val);

    /*trigger start.*/
    write_reg(vpu->vpu_base, REG_CFGC_ACM_CTRL, VDMA_ACFG_DHA(des_pa) | VDMA_ACFG_RUN);

/*    fprintf(stderr,"JPGC SCH_GLBC: 0x%08x\n", read_reg(vpu->vpu_base,REG_SCH_GLBC));
    fprintf(stderr,"JPGC CFGC_ACM_CTRL: 0x%08x\n", read_reg(vpu->vpu_base,REG_CFGC_ACM_CTRL));
    fprintf(stderr,"JPGC STATUS: 0x%08x\n", read_reg(vpu->vpu_base,REG_JPGC_STAT));
    fprintf(stderr,"SCH_STAT: 0x%08x\n", read_reg(vpu->vpu_base,REG_SCH_STAT));
*/

#ifdef IRQ_THREAD_WAITER
    {
        int timeout_ms = 500;
        timeout_ms = thread_waiter_wait_timeout(&waiter, timeout_ms);
        if (timeout_ms)
            printf("thread_waiter_wait_timeout() ret=%d\n", timeout_ms);
        disable_irq(IRQ_JPEG);
    }
#else
    while(!(read_reg(vpu->vpu_base,REG_JPGC_STAT) & 0x80000000)) {
        wait_time++;
        if (wait_time > 100000000) {
            fprintf(stderr,"timemout\n");
            fprintf(stderr,"SCH_STAT: 0x%08x\n", read_reg(vpu->vpu_base,REG_SCH_STAT));
            fprintf(stderr,"NMCU: 0x%08x\n", read_reg(vpu->vpu_base,REG_JPGC_STAT) & 0xFFFFFF);
            fprintf(stderr,"JPGC STATUS: 0x%08x\n", read_reg(vpu->vpu_base,REG_JPGC_STAT));
            fprintf(stderr, "efe_stat %x, sch_stat %x\n", read_reg(vpu->vpu_base, REG_EFE_STAT), read_reg(vpu->vpu_base, REG_SCH_STAT));
            //vpu_dump_regs(dev);
            break;
        }
    }
#endif

    bslen = read_reg(vpu->vpu_base,REG_JPGC_STAT) & 0xFFFFFF;
    // printf("%s() bslen=%d\n", __func__, bslen);
    return bslen;
}


#define INTC_ICSR0  0x00
#define INTC_ICMR0  0x04
#define INTC_ICMSR0 0x08
#define INTC_ICMCR0 0x0C
#define INTC_ICPR0  0x10
#define INTC_ICSR1  0x20
#define INTC_ICMR1  0x24
#define INTC_ICMSR1 0x28
#define INTC_ICMCR1 0x2C
#define INTC_ICPR1  0x30
#define INTC_DSR0   0x34
#define INTC_DMR0   0x38
#define INTC_DPR0   0x3C
#define INTC_DSR1   0x40
#define INTC_DMR1   0x44
#define INTC_DPR1   0x48

#define INTC_ADDR(reg) io_addr(INTC_IOBASE + reg)

static inline void dump_intc_regs(int irq, void *dev)
{

#define PRINT_INTC_REG(reg)                     \
    printf(#reg "=0x%08lx\n", *INTC_ADDR(reg))

    PRINT_INTC_REG(INTC_ICSR1);
    PRINT_INTC_REG(INTC_ICMR1);
    PRINT_INTC_REG(INTC_ICPR1);
}

/*
[0.013128] VAE mmap successfully done! cpm_base = 0xb0000000
[0.018502] INTC_ICSR1=0x0
[0.020840] INTC_ICMR1=0xbffdffff
[0.023787] INTC_ICMSR1=0x0
[0.026214] INTC_ICMCR1=0x0
[0.031224] convert to jpeg frame:
[0.034635] jz_start_hw_compress() REG_JPGC_STAT=3d6b
[0.040065] jz_start_hw_compress() REG_JPGC_STAT=800032e1
 */
static void vpu_intr_handler(int irq, void *dev)
{
    unsigned int status;
    struct vpu_struct *vpu ;
    vpu = (struct vpu_struct *)dev;
    status = read_reg(vpu->vpu_base,REG_JPGC_STAT);
    // printf("%s() REG_JPGC_STAT=0x%08x\n", __func__, status);

    if (status & JPGC_STAT_ENDF) {
        status &= ~JPGC_STAT_ENDF;
        thread_waiter_wakeup(&waiter);
    }
    if (status & (0x7f000000)) {
        printf("%s() unknown error REG_JPGC_STAT=0x%08x\n", __func__, status);
        status &= ~0x7f000000;
    }

    status |= 0x00ffffff;       /* bslen mask */
    write_reg(vpu->vpu_base,REG_JPGC_STAT, status);

    return;
}

static void vpu_irq_init(void * vpu)
{
    request_irq_disabled(IRQ_JPEG, 0, vpu_intr_handler, "vpu-irq", (void *)vpu);
    //enable_irq(IRQ_JPEG);
    //dump_intc_regs(0, vpu);
    return;
}

/*************************************************************************************
 Test Environment initialize/uninitialize
*************************************************************************************/
static void* vpu_init(void)
{
    struct vpu_struct *vpu = malloc(sizeof(struct vpu_struct));

    vpu->vpu_fd = -1;
    vpu_reg_mmap(vpu);    // @FPGA, VPU and other used register address mapping.
    vpu_clock_start(vpu);
    vpu_power_on(vpu);

#ifdef IRQ_THREAD_WAITER
    thread_waiter_init(&waiter);
    vpu_irq_init(vpu);
#endif
    return (void*)vpu;
}

static void vpu_exit(void *vpu)
{
#ifdef IRQ_THREAD_WAITER
    //disable_irq(IRQ_JPEG);
    release_irq(IRQ_JPEG);
#endif
    vpu_clock_stop(vpu);
    vpu_reg_unmap(vpu);
    if (vpu)
        free(vpu);
    return;
}

