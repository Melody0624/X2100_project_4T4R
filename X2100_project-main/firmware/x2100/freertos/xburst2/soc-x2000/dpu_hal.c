#include <common.h>
#include <os.h>
#include <soc/base.h>
#include <driver/cache.h>
#include <driver/fb.h>
#include <driver/clk.h>
#include <driver/gpio.h>
#include <driver/irq.h>
#include <wake_lock.h>
#include <soc/clk.h>

#include <stdio.h>

#include "lcdc_regs.h"
#include "soc/dpu_hal.h"

struct {
    int irq_is_request;
    int irq_is_enable;
}dpu;

static DEFINE_CRITICAL_THREADCOND(cond);
static DEFINE_MUTEX_RECURSIVE(lock);

#ifdef CONFIG_X2000_LCD
extern int lcdc_irq_handler(int irq, void *data);
#endif

#ifdef CONFIG_X2000_FB_LAYER_MIXER
extern int fb_layer_mixer_irq_handler(int irq, void *data);
#endif

#define DPU_ADDR(reg)  (io_addr(DPU_IOBASE + reg))

void dpu_write(unsigned int reg, unsigned int val)
{
    *DPU_ADDR(reg) = val;
}

unsigned int dpu_read(unsigned int reg)
{
    return *DPU_ADDR(reg);
}

void dpu_set_bit(unsigned int reg, int start, int end, unsigned int val)
{
    set_bit_field_v(DPU_ADDR(reg), start, end, val);
}

unsigned int dpu_get_bit(unsigned int reg, int start, int end)
{
    return get_bit_field_v(DPU_ADDR(reg), start, end);
}

void dpu_start_composer(void)
{
    dpu_write(FRM_CFG_CTRL, bit_field_val(FRM_CFG_START, 1));
}

void dpu_genernal_stop_display(void)
{
    dpu_write(CTRL, bit_field_val(GEN_STP_CMP, 1));
}

void dpu_set_srdma_ch(void)
{
    dpu_set_bit(COM_CFG, CH_SEL, 1);
}

void dpu_set_composer_ch(void)
{
    dpu_set_bit(COM_CFG, CH_SEL, 0);
}

void dpu_quick_stop_display(void)
{
    dpu_write(CTRL, bit_field_val(QCK_STP_CMP, 1));
}

void dpu_start_simple_read(void)
{
    dpu_write(SRD_CHAIN_CTRL, bit_field_val(SRD_CHAIN_START, 1));
}

int bit_field_start(int start, int end)
{
    return start;
}

static inline int bytes_per_pixel(enum fb_fmt fmt)
{
    if (fmt == fb_fmt_RGB888 || fmt == fb_fmt_ARGB8888)
        return 4;
    return 2;
}

void dpu_enable_layer(struct framedesc *desc, int id, int enable)
{
    int off = bit_field_start(f_layer0En) + id;
    set_bit_field(&desc->LayerCfgScaleEn, off, off, enable);
}

void dpu_enable_layer_scaling(struct framedesc *desc, int id, int enable)
{
    int off = bit_field_start(f_lay0ScaleEn) + id;
    set_bit_field(&desc->LayerCfgScaleEn, off, off, enable);
}

void dpu_set_layer_order(struct framedesc *desc, int id, int order)
{
    int off = bit_field_start(f_layer0order) + id * 2;
    if (order == lcdc_layer_top)
        order = lcdc_layer_3;
    if (order == lcdc_layer_bottom)
        order = lcdc_layer_0;
    order = order - lcdc_layer_0;
    set_bit_field(&desc->LayerCfgScaleEn, off, off + 1, order);
}

void dpu_init_layer_desc(struct layerdesc *layer, struct lcdc_layer *cfg)
{
    int format;
    switch (cfg->fb_fmt) {
    case fb_fmt_RGB555:
        format = 0; break;
    case fb_fmt_RGB565:
        format = 2; break;
    case fb_fmt_RGB888:
        format = 4; break;
    case fb_fmt_ARGB8888:
        format = 5; break;
    case fb_fmt_NV12:
        format = 8; break;
    case fb_fmt_NV21:
        format = 9; break;
    case fb_fmt_yuv422:
        format = 10;break;
    default:
        panic("format err: %d\n", cfg->fb_fmt); break;
    }

    unsigned int addr_y, addr_uv;
    unsigned int stride_y, stride_uv;
    if (cfg->fb_fmt == fb_fmt_NV12 || cfg->fb_fmt == fb_fmt_NV21) {
        addr_y = virt_to_phys(cfg->y.mem);
        addr_uv = virt_to_phys(cfg->uv.mem);
        stride_y = cfg->y.stride;
        stride_uv = cfg->uv.stride;
    } else {
        addr_y = virt_to_phys(cfg->rgb.mem);
        addr_uv = 0;
        stride_y = cfg->rgb.stride / bytes_per_pixel(cfg->fb_fmt);
        stride_uv = 0;
    }

    layer->LayerSize = 0;
    set_bit_field(&layer->LayerSize, l_Height, cfg->yres);
    set_bit_field(&layer->LayerSize, l_Width, cfg->xres);

    layer->LayerCfg = 0;
    set_bit_field(&layer->LayerCfg, l_SHARPL, 1);
    set_bit_field(&layer->LayerCfg, l_Format, format);
    set_bit_field(&layer->LayerCfg, l_PREMULT, 0);
    set_bit_field(&layer->LayerCfg, l_GAlpha_en, cfg->alpha.enable);
    set_bit_field(&layer->LayerCfg, l_Color, 0);
    set_bit_field(&layer->LayerCfg, l_GAlpha, cfg->alpha.value);

    layer->LayerPos = 0;
    set_bit_field(&layer->LayerPos, l_YPos, cfg->ypos);
    set_bit_field(&layer->LayerPos, l_XPos, cfg->xpos);

    unsigned int target_xres = cfg->scaling.xres ? cfg->scaling.xres : cfg->xres;
    unsigned int target_yres = cfg->scaling.yres ? cfg->scaling.yres : cfg->yres;
    layer->Layer_Resize_Coef_X = cfg->xres * 512 / target_xres;
    layer->Layer_Resize_Coef_Y = cfg->yres * 512 / target_yres;

    if (cfg->scaling.enable) {
        layer->LayerTargetSize = 0;
        set_bit_field(&layer->LayerTargetSize, l_TargetHeight, target_yres);
        set_bit_field(&layer->LayerTargetSize, l_TargetWidth, target_xres);
    } else {
        layer->LayerTargetSize = 0;
    }

    layer->LayerBufferAddr = addr_y;
    layer->BufferAddr_UV = addr_uv;
    layer->LayerStride = stride_y;
    layer->stride_UV = stride_uv;
    layer->Reserved1 = 0;
    layer->Reserved2 = 0;
}

static void dpu_irq_handler(int irq, void *data)
{
    unsigned long flags = dpu_read(INT_FLAG);
    int ret;

#ifdef CONFIG_X2000_FB_LAYER_MIXER
    ret = fb_layer_mixer_irq_handler(irq, data);
    if (!ret)
        return;
#endif

#ifdef CONFIG_X2000_LCD
    ret = lcdc_irq_handler(irq, data);
    if (!ret)
        return;
#endif

    printf("lcd: why this irq: %lx\n", flags);

    dpu_write(CLR_ST, flags);
    return;
}

void dpu_enable_irq(void)
{
    if (dpu.irq_is_enable++ != 0)
        return;

    enable_irq(IRQ_LCD);
}

void dpu_disable_irq(void)
{
    if (--dpu.irq_is_enable != 0)
        return;

    disable_irq(IRQ_LCD);
}

void dpu_request_irq(void)
{
    if (dpu.irq_is_request++ != 0)
        return;

    request_irq_disabled(IRQ_LCD, 0, dpu_irq_handler, "lcdc", NULL);
}

struct dpu_csc_config {
    int mult_y;
    int mult_rv;
    int mult_gu;
    int mult_gv;
    int mult_bu;
    int sub_uv;
    int sub_y;
};


/*
 * tv range 的颜色空间，配置控制器sub_y，会出现反色问题。
 * 应该是边界溢出没处理好，这里先默认为0。实际需要配置成16(0x10)
 */
static struct dpu_csc_config csc_config[] = {
    [FB_CSC_BT601_TV_RANGE] = {0x4a8, 0x662, 0x191, 0x341, 0x811, 0x80, 0x0},
    [FB_CSC_BT601_FULL_RANGE] = {0x400, 0x59c, 0x160, 0x2db, 0x717, 0x80, 0x0},
    [FB_CSC_BT709_TV_RANGE] = {0x4a8, 0x72c, 0x223, 0x453, 0x876, 0x80, 0x0},
    [FB_CSC_BT709_FULL_RANGE] = {0x400, 0x64c, 0xc0, 0x1df, 0x76c, 0x80, 0x0},
};

void dpu_set_csc(int layer_id, enum fb_csc_type type)
{
    if (layer_id > 1) {
        printf("dpu_hal: failed to set layer %d csc, not support\n", layer_id);
        return;
    }

    if (type < FB_CSC_BT601_TV_RANGE || type > FB_CSC_BT709_FULL_RANGE) {
        printf("dpu_hal: failed to set csc type = %d, set default: bt601_tv_range\n", type);
        type = FB_CSC_BT601_TV_RANGE;
    }

    int offset = layer_id * 0x10;

    dpu_set_bit(LAY0_CSC_MULT_YRV + offset, CSC_MULT_Y, csc_config[type].mult_y);

    dpu_set_bit(LAY0_CSC_MULT_YRV + offset, CSC_MULT_RV,csc_config[type].mult_rv);

    dpu_set_bit(LAY0_CSC_MULT_GUGV + offset, CSC_MULT_GU, csc_config[type].mult_gu);
    dpu_set_bit(LAY0_CSC_MULT_GUGV + offset, CSC_MULT_GV, csc_config[type].mult_gv);

    dpu_set_bit(LAY0_CSC_MULT_BU + offset, CSC_MULT_BU, csc_config[type].mult_bu);

    dpu_set_bit(LAY0_CSC_SUB_YUV + offset, CSC_SUB_UV, csc_config[type].sub_uv);
    dpu_set_bit(LAY0_CSC_SUB_YUV + offset, CSC_SUB_Y, csc_config[type].sub_y);
}
