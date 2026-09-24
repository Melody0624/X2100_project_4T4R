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
#include <driver/fb.h>

#include <stdio.h>

#include "lcdc_regs.h"
#include "soc/fb_layer_mixer.h"
#include "soc/dpu_hal.h"

#define State_writeback_start 0
#define State_writeback_end   1

struct lcdc_frame {
    struct framedesc *framedesc;
    struct layerdesc *layers[4];
};

struct fb_layer_mixer_info {
    int xres;
    int yres;
    unsigned int bytes_per_line;
    unsigned int bytes_per_frame;
    enum fb_fmt format;
    void *dst_mem;
};

struct fb_layer_mixer_dev {
    struct lcdc_frame frame;
    struct fb_layer_mixer_output_cfg mxier_cfg;
    struct fb_layer_mixer_info data;
};

struct {
    struct clk *clk;
    int frame_state;
    int is_enable;
} layer_mixer;


static DEFINE_MUTEX_RECURSIVE(lock);
static DEFINE_CRITICAL_THREADCOND(cond);

static void init_lcdc(void)
{
    unsigned long intc = dpu_read(INTC);
    set_bit_field(&intc, EOS_MSK, 1);
    set_bit_field(&intc, EOW_MSK, 1);
    set_bit_field(&intc, UOT_MSK, 1);
    dpu_write(INTC, intc);

    dpu_write(CLR_ST, dpu_read(INT_FLAG));

    unsigned long com_cfg = dpu_read(COM_CFG);
    set_bit_field(&com_cfg, BURST_LEN_BDMA, 3);
    set_bit_field(&com_cfg, BURST_LEN_RDMA, 3);
    set_bit_field(&com_cfg, CH_SEL, 0);
    dpu_write(COM_CFG, com_cfg);
}

static int wb_format(enum fb_fmt format)
{
    switch (format)
    {
        case fb_fmt_RGB565: return 1;
        case fb_fmt_RGB555: return 2;
        case fb_fmt_RGB888: return 6;
        case fb_fmt_ARGB8888: return 0;
        default:
            panic("writeback not support this format:%d\n", format);
    }
}

static void init_writeback_desc(struct fb_layer_mixer_dev *dev)
{

    struct lcdc_frame *frame = &dev->frame;
    struct framedesc *desc = frame->framedesc;

    desc->FrameCfgAddr = virt_to_phys(desc);

    desc->FrameCtrl = 0;
    set_bit_field(&desc->FrameCtrl, f_DirectEn, 0);
    set_bit_field(&desc->FrameCtrl, f_WriteBack, 1);
    set_bit_field(&desc->FrameCtrl, f_Change2RDMA, 0);
    set_bit_field(&desc->FrameCtrl, f_stop, 1);
    set_bit_field(&desc->FrameCtrl, f_WB_DitherAuto, 1);
    set_bit_field(&desc->FrameCtrl, f_WB_DitherEn, 0);

    desc->Layer0CfgAddr = virt_to_phys(frame->layers[0]);
    desc->Layer1CfgAddr = virt_to_phys(frame->layers[1]);
    desc->Layer2CfgAddr = virt_to_phys(frame->layers[2]);
    desc->Layer3CfgAddr = virt_to_phys(frame->layers[3]);

    set_bit_field(&desc->LayerCfgScaleEn, f_layer0order, 0);
    set_bit_field(&desc->LayerCfgScaleEn, f_layer1order, 1);
    set_bit_field(&desc->LayerCfgScaleEn, f_layer2order, 2);

    set_bit_field(&desc->LayerCfgScaleEn, f_layer3order, 3);

    desc->InterruptControl = 0;
    set_bit_field(&desc->InterruptControl, f_EOW_MSK, 1);

    flush_dcache_force((unsigned long)frame->framedesc, sizeof(*frame->framedesc));
}

static void config_writeback_desc(struct fb_layer_mixer_dev *dev)
{
    struct lcdc_frame *frame = &dev->frame;

    struct framedesc *desc = frame->framedesc;

    set_bit_field(&desc->FrameSize, f_Width, dev->data.xres);
    set_bit_field(&desc->FrameSize, f_Height, dev->data.yres);

    set_bit_field(&desc->FrameCtrl, f_WB_Format, wb_format(dev->data.format));

    desc->WritebackBufferAddr = virt_to_phys(dev->data.dst_mem);
    desc->WritebackStride = dev->data.xres;

    flush_dcache_force((unsigned long)frame->framedesc, sizeof(*frame->framedesc));
}

static unsigned int bytes_per_pixel(enum fb_fmt format)
{
    switch (format) {
    case fb_fmt_RGB888:
    case fb_fmt_ARGB8888:
        return 4;
    case fb_fmt_RGB555:
    case fb_fmt_RGB565:
        return 2;
    default:
        return 2;
    }
}

static void fb_layer_mixer_enable(void)
{
    if (layer_mixer.is_enable++ != 0)
        return;

    clk_enable(layer_mixer.clk);

    init_lcdc();

    dpu_enable_irq();
}

static void fb_layer_mixer_disable(void)
{
    if (--layer_mixer.is_enable != 0)
        return;

    layer_mixer.is_enable = 0;

    dpu_disable_irq();

    clk_disable(layer_mixer.clk);
}

void fb_layer_mixer_init(void)
{
    layer_mixer.clk = clk_get("gate_lcd");
    assert(layer_mixer.clk);

    dpu_request_irq();
}


struct fb_layer_mixer_dev *fb_layer_mixer_create(void)
{
    struct fb_layer_mixer_dev *mixer = malloc(sizeof(struct fb_layer_mixer_dev));
    mixer->frame.framedesc = cache_align_malloc(sizeof(struct framedesc));
    memset(mixer->frame.framedesc, 0, sizeof(*mixer->frame.framedesc));

    int i;
    for(i = 0; i < 4; i++)
        mixer->frame.layers[i] = cache_align_malloc(sizeof(struct layerdesc));

    fb_layer_mixer_enable();
    init_writeback_desc(mixer);

    return mixer;
}

void fb_layer_mixer_set_output_frame(struct fb_layer_mixer_dev *mixer, struct fb_layer_mixer_output_cfg *mixer_cfg)
{
    mutex_lock(&lock);

    unsigned int bytes;
    mixer->data.xres = mixer_cfg->xres;
    mixer->data.yres = mixer_cfg->yres;
    mixer->data.format = mixer_cfg->format;
    mixer->data.dst_mem = mixer_cfg->dst_mem;

    bytes = bytes_per_pixel(mixer_cfg->format) * mixer_cfg->xres;
    mixer->data.bytes_per_line = ALIGN(bytes, 8);
    bytes = mixer->data.bytes_per_line * mixer_cfg->yres;
    mixer->data.bytes_per_frame = ALIGN(bytes, cache_line_size());

    // init_writeback_layer(mixer);
    config_writeback_desc(mixer);

    mutex_unlock(&lock);
}

void fb_layer_mixer_config_layer(struct fb_layer_mixer_dev *mixer, int layer_id, struct lcdc_layer *cfg)
{
    assert(layer_id < 4 && layer_id >= 0);
    assert(mixer);


    struct lcdc_frame *frame = &mixer->frame;
    struct layerdesc *layer = frame->layers[layer_id];

    os_enter_critical();

    dpu_init_layer_desc(layer, cfg);

    dpu_enable_layer(frame->framedesc, layer_id, !!cfg->layer_enable);
    dpu_enable_layer_scaling(frame->framedesc, layer_id, !!cfg->scaling.enable);
    dpu_set_layer_order(frame->framedesc, layer_id, cfg->layer_order);
    dpu_set_csc(layer_id, cfg->convert_type);

    os_exit_critical();

    flush_dcache_force((unsigned long)layer, sizeof(*layer));
    flush_dcache_force((unsigned long)frame->framedesc, sizeof(*frame->framedesc));

}

void fb_layer_mixer_enable_layer(struct fb_layer_mixer_dev *mixer, unsigned int layer_id, int enable)
{
    assert(layer_id < 4 && layer_id >= 0);
    assert(mixer);
    struct lcdc_frame *frame = &mixer->frame;

    os_enter_critical();

    dpu_enable_layer(frame->framedesc, layer_id, !!enable);

    os_exit_critical();

    flush_dcache_force((unsigned long)frame->framedesc, sizeof(*frame->framedesc));
}

int fb_layer_mixer_irq_handler(int irq, void *data)
{
    unsigned long flags = dpu_read(INT_FLAG);

    if (get_bit_field(&flags, WDMA_END)) {
        layer_mixer.frame_state = State_writeback_end;
        dpu_write(CLR_ST, bit_field_val(CLR_WDMA_END, 1));
        critical_thread_cond_signal(&cond);
        return 0;
    }

    return -1;
}

void fb_layer_mixer_work_out_one_frame(struct fb_layer_mixer_dev *mixer)
{
    assert(mixer);

    mutex_lock(&lock);

    flush_cache_all();

    os_enter_critical();

    struct lcdc_frame *frame = &mixer->frame;

    dpu_write(FRM_CFG_ADDR, virt_to_phys(frame->framedesc));

    layer_mixer.frame_state = State_writeback_start;
    dpu_start_composer();

    critical_thread_cond_wait_timeout(&cond, 300);
    if (layer_mixer.frame_state != State_writeback_end)
        panic("frame write back timeout %d\n", layer_mixer.frame_state);

    os_exit_critical();

    invalidate_dcache((unsigned long)mixer->data.dst_mem, mixer->data.bytes_per_frame);
    mutex_unlock(&lock);
}

void fb_layer_mixer_delete(struct fb_layer_mixer_dev *mixer)
{
    if (!mixer)
        return;

    mutex_lock(&lock);

    free(mixer);

    fb_layer_mixer_disable();

    mutex_unlock(&lock);
}