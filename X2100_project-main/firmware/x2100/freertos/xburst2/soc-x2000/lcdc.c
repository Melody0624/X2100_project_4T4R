
#include <common.h>
#include <soc/base.h>
#include <driver/cache.h>
#include <driver/fb.h>
#include <driver/clk.h>
#include <driver/gpio.h>
#include <driver/irq.h>
#include <os.h>
#include <wake_lock.h>
#include <soc/clk.h>

#include <stdio.h>

#include "soc/lcdc_data.h"
#include "lcdc_regs.h"

#include "lcd_gpio.c"
#include "mipi_dsi.c"
#include "soc/dpu_hal.h"
#include "soc/fb_layer_mixer.h"

static void *rmem_start = (void *)CONFIG_X2000_LCD_DESC_ADDR;

static void *m_mem_alloc(int size)
{
    if (!rmem_start || rmem_start == (void*)-1)
        return cache_align_malloc(size);

    int align = cache_line_size();

    void *ret = (void *)ALIGN((unsigned long)rmem_start, align);

    rmem_start = (void *)ALIGN((unsigned long)ret+size, align);

    return ret;
}

static void m_flush_dcache(unsigned long addr, int size)
{
    if (addr <= 0xb0000000)
        flush_dcache_force(addr, size);
}

#define CPM_ADDR(reg)  ((volatile unsigned long *)CKSEG1ADDR(CPM_IOBASE + reg))

#define LPCDR       0x64
#define LCD_IO_INV  26, 26

void cpm_write(unsigned int reg, unsigned int val)
{
    *CPM_ADDR(reg) = val;
}

unsigned int cpm_read(unsigned int reg)
{
    return *CPM_ADDR(reg);
}

void lcdc_dump_regs(void);
static void soc_fb_pan_display(struct fb_dev *fbdev, unsigned int layer_index);

enum display_mode {
    COMPOSER_DISPLAY,
    SRDMA_DISPLAY,
};

enum frame_state {
    state_clear,
    state_display_start,
    state_display_end,
    state_stop,
};

struct srdmadesc {
    unsigned long RdmaNextCfgAddr;
    unsigned long FrameBufferAddr;
    unsigned long stride;
    unsigned long FrameCtrl;
    unsigned long InterruptControl;
};

struct lcdc_frame {
    struct framedesc *framedesc;
    struct layerdesc *layers[4];
    struct srdmadesc *srdmadesc;
};

struct lcdc_srdma_cfg {
    enum fb_fmt fb_fmt;
    void *fb_mem;
    int is_video;
    int stride;
};

struct fbdev_user_config {
    int width;
    int height;
    int xpos;
    int ypos;
    int scaling_enable;
    int scaling_width;
    int scaling_height;
};

struct fb_dev {
    int is_user_setting;
    struct lcdc_layer user_cfg;
    struct lcdc_layer layer_cfg;
    struct lcdc_srdma_cfg srdma_cfg;
    struct fb_info info;
    struct fb_ops ops;
    const char *name;

    int enable;

    int enable_fbdev_user_cfg;
    struct fbdev_user_config fbdev_user_cfg;
};

struct {
    struct lcdc_frame *frames;
    struct lcdc_data *pdata;
    struct clk *clk;
    struct clk *pclk;

    struct fb_layer_mixer_output_cfg mixer_cfg;
    struct fb_layer_mixer_dev *mixer;

    int is_inited;
    int is_enabled;
    int is_wait_stop;
    int frame_index;
    enum frame_state frame_state;

    int pan_display_sync;
    int use_default_order;

    enum display_mode display_mode;
    int display_count;
    int wb_index;
    struct fb_dev fbdev[5];
} lcd;

static DEFINE_MUTEX_RECURSIVE(lock);
static DEFINE_MUTEX_RECURSIVE(frames_lock);
static DEFINE_CRITICAL_THREADCOND(cond);

static unsigned int spi_send_delay = 0;

static int slcd_wait_busy_us(unsigned int count_udelay)
{
    int busy;
    uint64_t old = systick_get_time_us();

    busy = dpu_get_bit(SLCD_ST, BUSY);
    while (busy && systick_get_time_us() - old < count_udelay) {
        usleep(500);
        busy = dpu_get_bit(SLCD_ST, BUSY);
    }

    return busy;
}

static int slcd_wait_busy(unsigned int count)
{
    int busy;

    busy = dpu_get_bit(SLCD_ST, BUSY);
    while (count-- && busy) {
        busy = dpu_get_bit(SLCD_ST, BUSY);
    }

    return busy;
}

static void slcd_send_cmd(unsigned int cmd)
{
    unsigned long val = 0;

    if (slcd_wait_busy(10 * 1000))
        panic("lcdc busy\n");

    set_bit_field(&val, FLAG, 2);
    set_bit_field(&val, CONTENT, cmd);
    dpu_write(SLCD_REG_IF, val);

    if (spi_send_delay != 0)
        udelay(spi_send_delay);
}

static void slcd_send_data(unsigned int data)
{
    unsigned long val = 0;

    if (slcd_wait_busy(10 * 1000))
        panic("lcdc busy\n");

    set_bit_field(&val, FLAG, 1);
    set_bit_field(&val, CONTENT, data);
    dpu_write(SLCD_REG_IF, val);

    if (spi_send_delay != 0)
        udelay(spi_send_delay);
}


static void init_tft_mipi(struct lcdc_data *pdata)
{
    int hps = pdata->hsync_len;
    int hpe = hps + pdata->left_margin + pdata->xres + pdata->right_margin;
    int vps = pdata->vsync_len;
    int vpe = vps + pdata->upper_margin + pdata->yres + pdata->lower_margin;
    int hds = pdata->hsync_len + pdata->left_margin;
    int hde = hds + pdata->xres;
    int vds = pdata->vsync_len + pdata->upper_margin;
    int vde = vds + pdata->yres;

    dpu_write(TFT_HSYNC, bit_field_val(HPS,hps) | bit_field_val(HPE,hpe));
    dpu_write(TFT_VSYNC, bit_field_val(VPS,vps) | bit_field_val(VPE,vpe));
    dpu_write(TFT_HDE, bit_field_val(HDS,hds) | bit_field_val(HDE,hde));
    dpu_write(TFT_VDE, bit_field_val(VDS,vds) | bit_field_val(VDE,vde));

    unsigned long tft_tran_cfg = 0;
    set_bit_field(&tft_tran_cfg, PIX_CLK_INV, 0);
    set_bit_field(&tft_tran_cfg, DE_DL, 0);
    set_bit_field(&tft_tran_cfg, SYNC_DL, 0);
    set_bit_field(&tft_tran_cfg, COLOR_EVEN, 0);
    set_bit_field(&tft_tran_cfg, COLOR_ODD, 0);
    set_bit_field(&tft_tran_cfg, MODE, pdata->lcd_mode);

    switch (pdata->out_format) {
        case OUT_FORMAT_RGB888:
            set_bit_field(&tft_tran_cfg, MODE, 0);
            break;
        case OUT_FORMAT_RGB666:
            set_bit_field(&tft_tran_cfg, MODE, 1);
            break;
        case OUT_FORMAT_RGB565:
            set_bit_field(&tft_tran_cfg, MODE, 2);
            break;
        default:
            break;
    }

    dpu_write(TFT_CFG, tft_tran_cfg);
}

static void init_tft(struct lcdc_data *pdata)
{
    int hps = pdata->hsync_len;
    int hpe = hps + pdata->left_margin + pdata->xres + pdata->right_margin;
    int vps = pdata->vsync_len;
    int vpe = vps + pdata->upper_margin + pdata->yres + pdata->lower_margin;
    int hds = pdata->hsync_len + pdata->left_margin;
    int hde = hds + pdata->xres;
    int vds = pdata->vsync_len + pdata->upper_margin;
    int vde = vds + pdata->yres;

    /* 若为TFT_8BITS_SERIAL模式，hde与hpe的行有效时长会自动乘以3，此处无需修改 */
    dpu_write(TFT_HSYNC, bit_field_val(HPS,hps) | bit_field_val(HPE,hpe));
    dpu_write(TFT_VSYNC, bit_field_val(VPS,vps) | bit_field_val(VPE,vpe));
    dpu_write(TFT_HDE, bit_field_val(HDS,hds) | bit_field_val(HDE,hde));
    dpu_write(TFT_VDE, bit_field_val(VDS,vds) | bit_field_val(VDE,vde));

    unsigned long tft_tran_cfg = 0;
    set_bit_field(&tft_tran_cfg, PIX_CLK_INV, pdata->tft.pix_clk_polarity == AT_FALLING_EDGE);
    set_bit_field(&tft_tran_cfg, DE_DL, pdata->tft.de_active_level == AT_LOW_LEVEL);
    set_bit_field(&tft_tran_cfg, SYNC_DL, pdata->tft.hsync_vsync_active_level == AT_LOW_LEVEL);
    set_bit_field(&tft_tran_cfg, COLOR_EVEN, pdata->tft.even_line_order);
    set_bit_field(&tft_tran_cfg, COLOR_ODD, pdata->tft.odd_line_order);
    set_bit_field(&tft_tran_cfg, MODE, pdata->lcd_mode);

    dpu_write(TFT_CFG, tft_tran_cfg);

    unsigned long lpcdr = cpm_read(LPCDR);
    set_bit_field(&lpcdr, LCD_IO_INV, pdata->tft.pix_clk_inv == INVERT_ENABLE);
    cpm_write(LPCDR, lpcdr);
}

static void inline init_slcd_mipi(struct lcdc_data *pdata)
{
    unsigned long slcd_cfg = dpu_read(SLCD_CFG);
    set_bit_field(&slcd_cfg, TE_SWITCH, pdata->mipi.slcd_te_pin_mode == TE_LCDC_TRIGGER);
    set_bit_field(&slcd_cfg, TE_DP, pdata->mipi.slcd_te_data_transfered_edge == AT_RISING_EDGE);
    set_bit_field(&slcd_cfg, DWIDTH, 4);
    set_bit_field(&slcd_cfg, CWIDTH, 0);
    dpu_write(SLCD_CFG, slcd_cfg);

    dpu_write(SLCD_WR_DUTY, 0);
    dpu_write(SLCD_TIMING, 0);

    unsigned long slcd_frm_size = 0;
    set_bit_field(&slcd_frm_size, V_SIZE, pdata->yres);
    set_bit_field(&slcd_frm_size, H_SIZE, pdata->xres);
    dpu_write(SLCD_FRM_SIZE, slcd_frm_size);

    dpu_write(SLCD_SLOW_TIME, 0);
}

static void init_slcd(struct lcdc_data *pdata)
{
    int dbi_type = 2;
    if (pdata->lcd_mode == SLCD_6800)
        dbi_type = 1;
    if (pdata->lcd_mode == SLCD_8080)
        dbi_type = 2;
    if (pdata->lcd_mode == SLCD_SPI_3LINE)
        dbi_type = 4;
    if (pdata->lcd_mode == SLCD_SPI_4LINE)
        dbi_type = 5;

    int pix_fmt = pdata->out_format;
    if (pdata->out_format == OUT_FORMAT_RGB444)
        pix_fmt = 1;
    if (pdata->out_format == OUT_FORMAT_RGB555)
        panic("slcd outformat can't be 555\n");

    unsigned long slcd_cfg = 0;
    set_bit_field(&slcd_cfg, RDY_ANTI_JIT, 0);
    set_bit_field(&slcd_cfg, FMT_EN, 0);
    set_bit_field(&slcd_cfg, DBI_TYPE, dbi_type);
    set_bit_field(&slcd_cfg, PIX_FMT, pix_fmt);
    set_bit_field(&slcd_cfg, TE_ANTI_JIT, 1);
    set_bit_field(&slcd_cfg, TE_MD, 0);
    set_bit_field(&slcd_cfg, TE_SWITCH, pdata->slcd.te_pin_mode == TE_LCDC_TRIGGER);
    set_bit_field(&slcd_cfg, RDY_SWITCH, pdata->slcd.enable_rdy_pin);
    set_bit_field(&slcd_cfg, CS_EN, 0);
    set_bit_field(&slcd_cfg, CS_DP, 0);
    set_bit_field(&slcd_cfg, RDY_DP, pdata->slcd.rdy_cmd_send_level == AT_HIGH_LEVEL);
    set_bit_field(&slcd_cfg, DC_MD, pdata->slcd.dc_pin == CMD_HIGH_DATA_LOW);
    set_bit_field(&slcd_cfg, WR_MD, pdata->slcd.wr_data_sample_edge == AT_RISING_EDGE);
    set_bit_field(&slcd_cfg, TE_DP, pdata->slcd.te_data_transfered_edge == AT_RISING_EDGE);
    set_bit_field(&slcd_cfg, DWIDTH, pdata->slcd.mcu_data_width);
    set_bit_field(&slcd_cfg, CWIDTH, pdata->slcd.mcu_cmd_width);
    dpu_write(SLCD_CFG, slcd_cfg);

    dpu_write(SLCD_WR_DUTY, 0);
    dpu_write(SLCD_TIMING, 0);

    unsigned long slcd_frm_size = 0;
    set_bit_field(&slcd_frm_size, V_SIZE, pdata->yres);
    set_bit_field(&slcd_frm_size, H_SIZE, pdata->xres);
    dpu_write(SLCD_FRM_SIZE, slcd_frm_size);

    dpu_write(SLCD_SLOW_TIME, 0);
}

static inline int is_tft(struct lcdc_data *pdata)
{
    return pdata->lcd_mode <= TFT_8BITS_DUMMY_SERIAL;
}

static inline int is_slcd(struct lcdc_data *pdata)
{
    return pdata->lcd_mode >= SLCD_6800 && pdata->lcd_mode != SLCD_MIPI;
}

static inline int is_slcd_mipi(struct lcdc_data *pdata)
{
    return pdata->lcd_mode == SLCD_MIPI;
}

static inline int is_tft_mipi(struct lcdc_data *pdata)
{
    return pdata->lcd_mode == TFT_MIPI;
}

static inline int is_video_mode(struct lcdc_data *pdata)
{
    return pdata->lcd_mode <= TFT_MIPI;
}

static void init_lcdc(void)
{
    struct lcdc_data *pdata = lcd.pdata;

    unsigned long intc = 0;
    set_bit_field(&intc, EOD_MSK, 1);
    // set_bit_field(&intc, EOC_MSK, 1);
    set_bit_field(&intc, SCA_MSK, 1);
    set_bit_field(&intc, UOT_MSK, 1);

    set_bit_field(&intc, EOS_MSK, 1);
    set_bit_field(&intc, EOW_MSK, 1);
    dpu_write(INTC, intc);

    dpu_write(CLR_ST, dpu_read(INT_FLAG));

    unsigned long com_cfg = dpu_read(COM_CFG);
    set_bit_field(&com_cfg, BURST_LEN_BDMA, 3);
    set_bit_field(&com_cfg, BURST_LEN_RDMA, 3);
    dpu_write(COM_CFG, com_cfg);

    int dither_en = 0;
    int dither_dw = 0;
    if (pdata->fb_fmt == fb_fmt_RGB888 || pdata->fb_fmt == fb_fmt_ARGB8888) {
        if (pdata->out_format != OUT_FORMAT_RGB888)
            dither_en = 1;
        if (pdata->out_format == OUT_FORMAT_RGB444)
            dither_dw = 0b111111;
        if (pdata->out_format == OUT_FORMAT_RGB555)
            dither_dw = 0b101010;
        if (pdata->out_format == OUT_FORMAT_RGB565)
            dither_dw = 0b100110;
        if (pdata->out_format == OUT_FORMAT_RGB666)
            dither_dw = 0b010101;
    }

    unsigned long disp_com = dpu_read(DISP_COM);
    set_bit_field(&disp_com, DP_DITHER_EN, dither_en);
    set_bit_field(&disp_com, DP_DITHER_DW, dither_dw);

    if (is_slcd_mipi(pdata))
        set_bit_field(&disp_com, DP_IF_SEL, 3);
    else if (is_slcd(pdata))
        set_bit_field(&disp_com, DP_IF_SEL, 2);
    else
        set_bit_field(&disp_com, DP_IF_SEL, 1);

    dpu_write(DISP_COM, disp_com);

    if (is_tft_mipi(pdata))
        init_tft_mipi(pdata);

    if (is_slcd_mipi(pdata))
        init_slcd_mipi(pdata);

    if (is_tft(pdata))
        init_tft(pdata);

    if (is_slcd(pdata))
        init_slcd(pdata);
}

void init_frame_desc(
    struct framedesc *desc,
    struct layerdesc *layer0,
    struct layerdesc *layer1,
    struct layerdesc *layer2,
    struct layerdesc *layer3)
{
    struct lcdc_data *pdata = lcd.pdata;
    desc->FrameCfgAddr = virt_to_phys(desc);
    desc->FrameSize = 0;
    set_bit_field(&desc->FrameSize, f_Width, pdata->xres);
    set_bit_field(&desc->FrameSize, f_Height, pdata->yres);

    desc->FrameCtrl = 0;
    set_bit_field(&desc->FrameCtrl, f_DirectEn, 1);
    set_bit_field(&desc->FrameCtrl, f_WriteBack, 0);
    set_bit_field(&desc->FrameCtrl, f_Change2RDMA, 0);
    set_bit_field(&desc->FrameCtrl, f_stop, !is_video_mode(pdata));

    desc->WritebackBufferAddr = 0;
    desc->WritebackStride = pdata->xres;

    desc->Layer0CfgAddr = virt_to_phys(layer0);
    desc->Layer1CfgAddr = virt_to_phys(layer1);
    desc->Layer2CfgAddr = virt_to_phys(layer2);
    desc->Layer3CfgAddr = virt_to_phys(layer3);

    desc->LayerCfgScaleEn = 0;
    set_bit_field(&desc->LayerCfgScaleEn, f_layer0order, 0);
    set_bit_field(&desc->LayerCfgScaleEn, f_layer1order, 1);
    set_bit_field(&desc->LayerCfgScaleEn, f_layer2order, 2);

    set_bit_field(&desc->LayerCfgScaleEn, f_layer3order, 3);

    desc->InterruptControl = 0;

    set_bit_field(&desc->InterruptControl, f_EOD_MSK, 1);
    set_bit_field(&desc->InterruptControl, f_SOC_MSK, 1);
    set_bit_field(&desc->InterruptControl, f_EOC_MSK, 1);

}

static void init_srdma_desc(struct srdmadesc *desc, struct lcdc_srdma_cfg *cfg)
{
    int format;
    switch (cfg->fb_fmt) {
    case fb_fmt_RGB555:
        format = 0; break;
    case fb_fmt_RGB565:
        format = 2; break;
    case fb_fmt_ARGB8888:
    case fb_fmt_RGB888:
        format = 4; break;
    default:
        panic("format err:%d\n", cfg->fb_fmt); break;
    }

    desc->RdmaNextCfgAddr = virt_to_phys(desc);

    desc->FrameCtrl = 0;
    set_bit_field(&desc->FrameCtrl, s_Format, format);
    set_bit_field(&desc->FrameCtrl, s_Color, 0);
    set_bit_field(&desc->FrameCtrl, s_CHAIN_END, !cfg->is_video);
    set_bit_field(&desc->FrameCtrl, s_Change2Comp, 1);

    desc->InterruptControl = 0;
    set_bit_field(&desc->InterruptControl, s_EOS_MSK, 1);
    // set_bit_field(&desc->InterruptControl, s_EOD_MSK, 1);

    desc->stride = cfg->stride;

    desc->FrameBufferAddr = virt_to_phys(cfg->fb_mem);

    m_flush_dcache((unsigned long)desc, sizeof(struct srdmadesc));
}

static void process_slcd_data_table(struct smart_lcd_data_table *table, unsigned int length)
{
    int i = 0;

    for (; i < length; i++) {
        switch (table[i].type) {
        case SMART_CONFIG_CMD:
            slcd_send_cmd(table[i].value);
            break;
        case SMART_CONFIG_DATA:
            slcd_send_data(table[i].value);
            break;
        case SMART_CONFIG_UDELAY:
            usleep(table[i].value);
            break;
        case SMART_CONFIG_FILL_ZERO: {
            unsigned int zero_count = table[i].value;

            while (zero_count--)
                slcd_send_data(0);
            break;
        }
        default:
            panic("why this type: %d\n", table[i].type);
            break;
        }
    }

    if (slcd_wait_busy(10 * 1000))
        panic("lcdc busy\n");
}

static void lcdc_config_layer(struct lcdc_frame *frame,
     unsigned int layer_id, struct lcdc_layer *cfg)
{
    assert(layer_id < 4);
    struct layerdesc *layer = frame->layers[layer_id];

    os_enter_critical();

    dpu_init_layer_desc(layer, cfg);
    dpu_enable_layer(frame->framedesc, layer_id, !!cfg->layer_enable);
    dpu_enable_layer_scaling(frame->framedesc, layer_id, !!cfg->scaling.enable);
    dpu_set_layer_order(frame->framedesc, layer_id, cfg->layer_order);
    dpu_set_csc(layer_id, cfg->convert_type);

    os_exit_critical();

    m_flush_dcache((unsigned long)layer, sizeof(*layer));
    m_flush_dcache((unsigned long)frame->framedesc, sizeof(*frame->framedesc));
}


static void calculate_spi_mode_delay_time(void)
{
    if (lcd.pdata->lcd_mode < SLCD_SPI_3LINE) {
        spi_send_delay = 0;
        return;
    }

    int cycle = 0;
    int width = lcd.pdata->slcd.mcu_data_width;
    int pixclock = lcd.pdata->pixclock;

    if (width == MCU_WIDTH_8BITS)
        cycle = 8;
    if (width == MCU_WIDTH_9BITS)
        cycle = 9;
    if (width == MCU_WIDTH_16BITS)
        cycle = 16;

    assert(cycle);

    spi_send_delay = cycle * 1000000 / pixclock + ((cycle * 1000000 % pixclock) != 0);
}

static void enable_fb(void)
{
    if (lcd.is_enabled++ != 0)
        return;

    unsigned int rate = lcd.pdata->pixclock;
    if (is_slcd(lcd.pdata)) {
        if (lcd.pdata->slcd.pixclock_when_init)
            rate = lcd.pdata->slcd.pixclock_when_init;
    }

    clk_enable(lcd.clk);
    clk_set_rate(lcd.pclk, rate);
    clk_enable(lcd.pclk);

    calculate_spi_mode_delay_time();

    init_lcdc();

    if (is_tft_mipi(lcd.pdata) || is_slcd_mipi(lcd.pdata))
        jz_enable_mipi_dsi();

    lcd.pdata->power_on(NULL);

    if (lcd.pdata->lcd_init)
        lcd.pdata->lcd_init();

    process_slcd_data_table(
        lcd.pdata->slcd_data_table, lcd.pdata->slcd_data_table_length);

    if (rate != lcd.pdata->pixclock)
        clk_set_rate(lcd.pclk, lcd.pdata->pixclock);


    if (is_slcd(lcd.pdata)) {
        slcd_send_cmd(lcd.pdata->slcd.cmd_of_start_frame);
        dpu_set_bit(SLCD_CFG, FMT_EN, 1);
    }

    if (is_tft_mipi(lcd.pdata))
        jz_dsi_video_cfg();

    if (is_slcd_mipi(lcd.pdata))
        jz_dsi_command_cfg();

    lcd.frame_state = state_clear;

    dpu_enable_irq();
    lcd.display_mode = COMPOSER_DISPLAY;
    dpu_set_srdma_ch();

#ifdef CONFIG_X2000_LCD_FB_MIXER_ENABLE
    lcd.mixer_cfg.xres = lcd.pdata->xres;
    lcd.mixer_cfg.yres = lcd.pdata->yres;
    lcd.mixer_cfg.format = lcd.pdata->fb_fmt;
    lcd.mixer_cfg.dst_mem = lcd.fbdev[4].info.fb_mem;
    lcd.mixer = fb_layer_mixer_create();

    fb_layer_mixer_set_output_frame(lcd.mixer, &lcd.mixer_cfg);

    lcd.display_mode = SRDMA_DISPLAY;
    dpu_set_srdma_ch();
#endif
}

static void disable_fb(void)
{
    if (--lcd.is_enabled != 0)
        return;

    lcd.is_wait_stop = 1;
    dpu_genernal_stop_display();

    while(lcd.is_wait_stop)
        msleep(1);

    dpu_disable_irq();

    lcd.pdata->power_off(NULL);

    if (is_tft_mipi(lcd.pdata) || is_slcd_mipi(lcd.pdata))
        jz_disable_mipi_dsi();


    clk_disable(lcd.clk);
    clk_disable(lcd.pclk);
}

static int check_scld_fmt(struct lcdc_data *pdata)
{
    int pix_fmt = pdata->out_format;

    if (pdata->lcd_mode >= SLCD_SPI_3LINE) {
        if (pix_fmt != OUT_FORMAT_RGB444 \
            && pix_fmt != OUT_FORMAT_RGB565 \
            && pix_fmt != OUT_FORMAT_RGB888)
            return -1;
        else
            return 0;
    }

    int width = pdata->slcd.mcu_data_width;
    if (width == MCU_WIDTH_8BITS) {
        if (pix_fmt != OUT_FORMAT_RGB565 && pix_fmt != OUT_FORMAT_RGB888)
            return -1;
    }
    if (width == MCU_WIDTH_9BITS) {
        if (pix_fmt != OUT_FORMAT_RGB666)
            return -1;
    }
    if (width == MCU_WIDTH_16BITS) {
        if (pix_fmt != OUT_FORMAT_RGB565)
            return -1;
    }

    return 0;
}

#define error_if(_cond) \
    do { \
        if (_cond) { \
            panic("fb: failed to check: %s\n", #_cond); \
        } \
    } while (0)

static void soc_fb_enable(struct fb_dev *fbdev)
{
    assert(fbdev);

    mutex_lock(&lock);
    if (fbdev->enable == 0) {
        fbdev->layer_cfg.layer_enable = 1;
        fbdev->user_cfg.layer_enable = 1;
        enable_fb();
    }

    fbdev->enable++;

    mutex_unlock(&lock);

    return;
}

static void soc_fb_disable(struct fb_dev *fbdev)
{
    assert(fbdev);

    mutex_lock(&lock);

    if(--fbdev->enable != 0) {
        mutex_unlock(&lock);
        return;
    }

    fbdev->layer_cfg.layer_enable = 0;
    fbdev->user_cfg.layer_enable = 0;
    disable_fb();

    mutex_unlock(&lock);

    if (lcd.is_enabled)
        soc_fb_pan_display(fbdev, 0);

    return;
}

static int soc_fb_is_enable(struct fb_dev *fbdev)
{
    assert(fbdev);

    return fbdev->enable;
}

static void soc_fb_get_info(struct fb_dev *fbdev, struct fb_info *info)
{
    assert(fbdev);
    assert(info);

    *info = fbdev->info;
}

static int soc_fb_set_config(struct fb_dev *fbdev, struct lcdc_layer *cfg)
{
    //检查参数....
    mutex_lock(&frames_lock);

    if (cfg->layer_order > lcdc_layer_3) {
        printf("fb: invalid order: %d\n", cfg->layer_order);
        return -EINVAL;
    }
    if (cfg->alpha.value >= 256) {
        printf("fb: invalid alpha: %x\n", cfg->alpha.value);
        return -EINVAL;
    }
    if (cfg->fb_fmt > fb_fmt_NV21) {
        printf("fb: invalid fmt: %d\n", cfg->fb_fmt);
        return -EINVAL;
    }

    if (!cfg->scaling.enable) {
        if (cfg->xres + cfg->xpos > lcd.pdata->xres) {
            printf("fb: invalid xres: %d %d\n", cfg->xres, cfg->xpos);
            return -EINVAL;
        }
        if (cfg->yres + cfg->ypos > lcd.pdata->yres) {
            printf("fb: invalid yres: %d %d\n", cfg->yres, cfg->ypos);
            return -EINVAL;
        }
    } else {
        if (cfg->scaling.xres + cfg->xpos > lcd.pdata->xres) {
            printf("fb: invalid scaling xres: %d %d\n", cfg->scaling.xres, cfg->xpos);
            return -EINVAL;
        }
        if (cfg->scaling.yres + cfg->ypos > lcd.pdata->yres) {
            printf("fb: invalid scaling yres: %d %d\n", cfg->scaling.yres, cfg->ypos);
            return -EINVAL;
        }
    }

    fbdev->user_cfg = *cfg;
    if (lcd.use_default_order)
        fbdev->user_cfg.layer_order = (fbdev - lcd.fbdev) + 2;

    mutex_unlock(&frames_lock);

    return 0;
}


static void soc_fb_enable_config(struct fb_dev *fbdev)
{
    mutex_lock(&lock);
    fbdev->is_user_setting = 1;
    mutex_unlock(&lock);

}

static void soc_fb_disable_config(struct fb_dev *fbdev)
{
    mutex_lock(&lock);
    fbdev->is_user_setting = 0;
    mutex_unlock(&lock);
}

int lcdc_irq_handler(int irq, void *data)
{
    unsigned long flags = dpu_read(INT_FLAG);

    if (get_bit_field(&flags, DISP_END)) {
        dpu_write(CLR_ST, bit_field_val(CLR_DISP_END, 1));
        lcd.frame_state = state_display_end;
        critical_thread_cond_signal(&cond);
        return 0;
    }

    if (get_bit_field(&flags, STOP_CMP_ACK)) {
        dpu_write(CLR_ST, bit_field_val(CLR_STOP_CMP_ACK, 1));
        lcd.frame_state = state_stop;
        lcd.is_wait_stop = 0;
        critical_thread_cond_signal(&cond);
        return 0;
    }

    if (get_bit_field(&flags, TFT_UNDR)) {
        printf("err: lcd underrun\n");
        dpu_write(CLR_ST, bit_field_val(CLR_TFT_UNDR, 1));
        return 0;
    }

    if (get_bit_field(&flags, SRD_END)) {
        lcd.frame_state = state_display_end;
        dpu_write(CLR_ST, bit_field_val(CLR_SRD_END, 1));
        critical_thread_cond_signal(&cond);
        return 0;
    }

    return -1;
}

static int lcdc_tft_pan_display(struct lcdc_frame *frame, struct fb_dev *fbdev)
{
    // /*等待旧的一帧刷新完成*/
    if (lcd.frame_state != state_clear && lcd.frame_state != state_display_end) {
        critical_thread_cond_wait_timeout(&cond, 300);
        if (lcd.frame_state != state_display_end)
            panic("tft pan display wait timeout %d\n", lcd.frame_state);
    }

    lcd.frame_state = state_display_start;

    if (lcd.display_mode != COMPOSER_DISPLAY) {
        dpu_write(SRD_CHAIN_ADDR, virt_to_phys(frame->srdmadesc));
        dpu_start_simple_read();
    } else {
        dpu_write(FRM_CFG_ADDR, virt_to_phys(frame->framedesc));
        dpu_start_composer();
    }

    return 0;
}

static int lcdc_slcd_pan_display(struct lcdc_frame *frame, struct fb_dev *fbdev)
{
    // /*等待旧的一帧刷新完成*/
    if (lcd.frame_state != state_clear && lcd.frame_state != state_display_end) {
        critical_thread_cond_wait_timeout(&cond, 300);
        if (lcd.frame_state != state_display_end)
            panic("slcd pan display wait timeout %d\n", lcd.frame_state);
    }

    os_exit_critical();
    slcd_wait_busy_us(10*1000);
    os_enter_critical();

    lcd.frame_state = state_display_start;

    if (lcd.display_mode != COMPOSER_DISPLAY) {
        dpu_write(SRD_CHAIN_ADDR, virt_to_phys(frame->srdmadesc));
        dpu_start_simple_read();
    } else {
        dpu_write(FRM_CFG_ADDR, virt_to_phys(frame->framedesc));
        dpu_start_composer();
    }

    return 0;
}

static void lcdc_config_composer_layer(struct fb_dev *fbdev,  struct lcdc_frame *frame, unsigned int fb_index)
{
    int layer_id = fbdev - lcd.fbdev;
    fbdev->layer_cfg.rgb.mem = fbdev->info.fb_mem + fb_index * fbdev->info.bytes_per_frame;

    if (!fbdev->is_user_setting)
        lcdc_config_layer(frame, layer_id, &fbdev->layer_cfg);
    else
        lcdc_config_layer(frame, layer_id, &fbdev->user_cfg);
}

#ifdef CONFIG_X2000_LCD_FB_MIXER_ENABLE
static void lcdc_config_mixer_layer(struct fb_dev *fbdev, struct fb_layer_mixer_dev *mixer, unsigned int fb_index)
{
    int layer_id = fbdev - lcd.fbdev;
    fbdev->layer_cfg.rgb.mem = fbdev->info.fb_mem + fb_index * fbdev->info.bytes_per_frame;

    if (!fbdev->is_user_setting)
        fb_layer_mixer_config_layer(mixer, layer_id, &fbdev->layer_cfg);
    else
        fb_layer_mixer_config_layer(mixer, layer_id, &fbdev->user_cfg);

}

static void lcdc_config_srdma(struct fb_dev *fbdev, struct lcdc_frame *frame, unsigned int wb_index)
{
    fbdev->srdma_cfg.fb_mem = fbdev->info.fb_mem + wb_index * fbdev->info.bytes_per_frame;

    init_srdma_desc(frame->srdmadesc, &fbdev->srdma_cfg);
}

static int get_wb_index(void)
{
    int index;
    struct fb_dev *fb_srdma = &lcd.fbdev[4];

    index = lcd.wb_index % fb_srdma->info.frame_count;
    lcd.wb_index++;

    return index;
}

static void run_fb_mixer(int index)
{
    struct fb_dev *fb_srdma = &lcd.fbdev[4];

    lcd.mixer_cfg.dst_mem = fb_srdma->info.fb_mem + index * fb_srdma->info.bytes_per_frame;
    fb_layer_mixer_set_output_frame(lcd.mixer, &lcd.mixer_cfg);
    fb_layer_mixer_work_out_one_frame(lcd.mixer);

}
#endif

static struct lcdc_frame *get_display_frame(void)
{
    int frame_index;
    struct lcdc_frame *display_frame;

    lcd.frame_index++;
    frame_index = lcd.frame_index % 2;
    display_frame = &lcd.frames[frame_index];

    return display_frame;
}



static void soc_fb_pan_display(struct fb_dev *fbdev, unsigned int layer_index)
{
    assert(lcd.is_enabled >= 1);
    assert(fbdev);
    assert(fbdev - lcd.fbdev < 4);
    int ret;

    flush_cache_all();

    struct lcdc_frame *ready_frame = &lcd.frames[2];

    mutex_lock(&frames_lock);

    lcd.display_count++;

    if (lcd.display_mode == COMPOSER_DISPLAY)
        lcdc_config_composer_layer(fbdev, ready_frame, layer_index);
#ifdef CONFIG_X2000_LCD_FB_MIXER_ENABLE
    else
        lcdc_config_mixer_layer(fbdev, lcd.mixer, layer_index);
#endif

    mutex_unlock(&frames_lock);

    mutex_lock(&lock);

    mutex_lock(&frames_lock);

    if (!lcd.display_count) {
        mutex_unlock(&lock);
        mutex_unlock(&frames_lock);
        return;
    }
    lcd.display_count = 0;

    struct lcdc_frame *display_frame = get_display_frame();
    *display_frame->framedesc = *ready_frame->framedesc;
    *display_frame->srdmadesc = *ready_frame->srdmadesc;
    m_flush_dcache((unsigned long)display_frame->framedesc, sizeof(*display_frame->framedesc));

    mutex_unlock(&frames_lock);

#ifdef CONFIG_X2000_LCD_FB_MIXER_ENABLE
    if (lcd.display_mode == SRDMA_DISPLAY) {
        int wb_index;
        wb_index = get_wb_index();
        run_fb_mixer(wb_index);
        lcdc_config_srdma(&lcd.fbdev[4], display_frame, wb_index);
    }
#endif

    os_enter_critical();

    if (!is_video_mode(lcd.pdata))
        ret = lcdc_slcd_pan_display(display_frame, fbdev);
    else
        ret = lcdc_tft_pan_display(display_frame, fbdev);

    if (ret < 0)
        goto unlock;

#ifdef CONFIG_X2000_LCD_PAN_DISPLAY_SYNC
    critical_thread_cond_wait_timeout(&cond, 300);
    if (lcd.frame_state != state_display_end)
        panic("pan display wait timeout\n");
#endif

unlock:
    os_exit_critical();
    mutex_unlock(&lock);
}

void lcdc_srdma_init(void)
{
    int i;
    for (i = 0; i < 3; i++)
        init_srdma_desc(lcd.frames[i].srdmadesc, &lcd.fbdev[4].srdma_cfg);
}

static int slcd_pixclock_cycle(struct lcdc_data *pdata)
{
    assert(pdata->lcd_mode == SLCD_6800 || pdata->lcd_mode == SLCD_8080);

    int cycle = 0;
    int width = pdata->slcd.mcu_data_width;
    int pix_fmt = pdata->out_format;
    if (width == MCU_WIDTH_8BITS) {
        if (pix_fmt == OUT_FORMAT_RGB565)
            cycle = 2;
        if (pix_fmt == OUT_FORMAT_RGB888)
            cycle = 3;
    }
    if (width == MCU_WIDTH_9BITS) {
            cycle = 2;
    }
    if (width == MCU_WIDTH_16BITS) {
            cycle = 1;
    }

    assert(cycle);

    return cycle * 2 + 1;
}

static int slcd_spi_pixclock_cycle(struct lcdc_data *pdata)
{
    assert(pdata->lcd_mode >= SLCD_SPI_3LINE);

    int cycle = 0;
    int width = pdata->slcd.mcu_data_width;
    int pix_fmt = pdata->out_format;
    if (width == MCU_WIDTH_8BITS) {
        if (pix_fmt == OUT_FORMAT_RGB565)
            cycle = 16;
        if (pix_fmt == OUT_FORMAT_RGB888)
            cycle = 24;
    }
    if (width == MCU_WIDTH_9BITS) {
        if (pix_fmt == OUT_FORMAT_RGB666)
            cycle = 24;
    }
    if (width == MCU_WIDTH_16BITS) {
            cycle = 16;
    }

    assert(cycle);

    return cycle * 2;
}
static void auto_calculate_pixel_clock(struct lcdc_data *pdata)
{
    int hps = pdata->hsync_len;
    int hpe = hps + pdata->left_margin + pdata->xres + pdata->right_margin;
    int vps = pdata->vsync_len;
    int vpe = vps + pdata->upper_margin + pdata->yres + pdata->lower_margin;
    if (pdata->lcd_mode == TFT_8BITS_SERIAL)
        hpe = hps + pdata->left_margin + (pdata->xres * 3) + pdata->right_margin;

    if (!pdata->refresh)
        pdata->refresh = 40;

    if (is_tft(pdata) || is_tft_mipi(pdata)) {
        if (!pdata->pixclock)
            pdata->pixclock = hpe * vpe * pdata->refresh;
    }

    if (is_slcd(pdata)) {
        if (!pdata->pixclock) {
            pdata->pixclock = pdata->xres * pdata->yres * pdata->refresh;
            if (pdata->lcd_mode >= SLCD_SPI_3LINE)
                pdata->pixclock *= slcd_spi_pixclock_cycle(pdata);
            else
                pdata->pixclock *= slcd_pixclock_cycle(pdata);
        }

        if (!pdata->slcd.pixclock_when_init)
            pdata->slcd.pixclock_when_init = pdata->xres * pdata->yres * 3;
    }

    if (is_slcd_mipi(pdata)) {
        if (!pdata->pixclock) {
            pdata->pixclock = pdata->xres * pdata->yres * pdata->refresh * 4;
        }
    }
}

static void init_fbdev_frame_layer(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        init_frame_desc(lcd.frames[i].framedesc, lcd.frames[i].layers[0],\
                                                 lcd.frames[i].layers[1],\
                                                 lcd.frames[i].layers[2],\
                                                 lcd.frames[i].layers[3]);
    }

    flush_cache_all();
}

static int init_fbdev_user_data(void)
{
#ifdef CONFIG_X2000_LCD_FB0_USER_ENABLE
    lcd.fbdev[0].enable_fbdev_user_cfg = 1;
    lcd.fbdev[0].fbdev_user_cfg.width = CONFIG_X2000_LCD_FB0_USER_WIDTH;
    lcd.fbdev[0].fbdev_user_cfg.height = CONFIG_X2000_LCD_FB0_USER_HEIGHT;
    lcd.fbdev[0].fbdev_user_cfg.xpos = CONFIG_X2000_LCD_FB0_USER_XPOS;
    lcd.fbdev[0].fbdev_user_cfg.ypos = CONFIG_X2000_LCD_FB0_USER_YPOS;

#ifdef CONFIG_X2000_LCD_FB0_USER_SCALING_ENABLE
    lcd.fbdev[0].fbdev_user_cfg.scaling_enable = 1;
    lcd.fbdev[0].fbdev_user_cfg.scaling_width = CONFIG_X2000_LCD_FB0_USER_SCALING_WIDTH;
    lcd.fbdev[0].fbdev_user_cfg.scaling_height = CONFIG_X2000_LCD_FB0_USER_SCALING_HEIGHT;
#endif
#endif

#ifdef CONFIG_X2000_LCD_FB1_USER_ENABLE
    lcd.fbdev[1].enable_fbdev_user_cfg = 1;
    lcd.fbdev[1].fbdev_user_cfg.width = CONFIG_X2000_LCD_FB1_USER_WIDTH;
    lcd.fbdev[1].fbdev_user_cfg.height = CONFIG_X2000_LCD_FB1_USER_HEIGHT;
    lcd.fbdev[1].fbdev_user_cfg.xpos = CONFIG_X2000_LCD_FB1_USER_XPOS;
    lcd.fbdev[1].fbdev_user_cfg.ypos = CONFIG_X2000_LCD_FB1_USER_YPOS;

#ifdef CONFIG_X2000_LCD_FB1_USER_SCALING_ENABLE
    lcd.fbdev[1].fbdev_user_cfg.scaling_enable = 1;
    lcd.fbdev[1].fbdev_user_cfg.scaling_width = CONFIG_X2000_LCD_FB1_USER_SCALING_WIDTH;
    lcd.fbdev[1].fbdev_user_cfg.scaling_height = CONFIG_X2000_LCD_FB1_USER_SCALING_HEIGHT;
#endif
#endif

#ifdef CONFIG_X2000_LCD_FB2_USER_ENABLE
    lcd.fbdev[2].enable_fbdev_user_cfg = 1;
    lcd.fbdev[2].fbdev_user_cfg.width = CONFIG_X2000_LCD_FB2_USER_WIDTH;
    lcd.fbdev[2].fbdev_user_cfg.height = CONFIG_X2000_LCD_FB2_USER_HEIGHT;
    lcd.fbdev[2].fbdev_user_cfg.xpos = CONFIG_X2000_LCD_FB2_USER_XPOS;
    lcd.fbdev[2].fbdev_user_cfg.ypos = CONFIG_X2000_LCD_FB2_USER_YPOS;

#ifdef CONFIG_X2000_LCD_FB2_USER_SCALING_ENABLE
    lcd.fbdev[2].fbdev_user_cfg.scaling_enable = 1;
    lcd.fbdev[2].fbdev_user_cfg.scaling_width = CONFIG_X2000_LCD_FB2_USER_SCALING_WIDTH;
    lcd.fbdev[2].fbdev_user_cfg.scaling_height = CONFIG_X2000_LCD_FB2_USER_SCALING_HEIGHT;
#endif
#endif

#ifdef CONFIG_X2000_LCD_FB3_USER_ENABLE
    lcd.fbdev[3].enable_fbdev_user_cfg = 1;
    lcd.fbdev[3].fbdev_user_cfg.width = CONFIG_X2000_LCD_FB3_USER_WIDTH;
    lcd.fbdev[3].fbdev_user_cfg.height = CONFIG_X2000_LCD_FB3_USER_HEIGHT;
    lcd.fbdev[3].fbdev_user_cfg.xpos = CONFIG_X2000_LCD_FB3_USER_XPOS;
    lcd.fbdev[3].fbdev_user_cfg.ypos = CONFIG_X2000_LCD_FB3_USER_YPOS;

#ifdef CONFIG_X2000_LCD_FB3_USER_SCALING_ENABLE
    lcd.fbdev[3].fbdev_user_cfg.scaling_enable = 1;
    lcd.fbdev[3].fbdev_user_cfg.scaling_width = CONFIG_X2000_LCD_FB3_USER_SCALING_WIDTH;
    lcd.fbdev[3].fbdev_user_cfg.scaling_height = CONFIG_X2000_LCD_FB3_USER_SCALING_HEIGHT;
#endif
#endif

    return 0;
}

static void init_fbdev_data(struct fb_dev *fbdev, const char *name, int frame_count)
{
    int bytes;
    struct fb_info *info = &fbdev->info;
    struct lcdc_data *pdata = lcd.pdata;
    int scaling_enable = 0;
    int scaling_width = 0;
    int scaling_height = 0;
    int xpos = 0;
    int ypos = 0;

    fbdev->name = name;

    info->fb_fmt = pdata->fb_fmt;

    if (fbdev->enable_fbdev_user_cfg) {
        info->xres = fbdev->fbdev_user_cfg.width;
        info->yres = fbdev->fbdev_user_cfg.height;
        scaling_enable = fbdev->fbdev_user_cfg.scaling_enable;
        scaling_width = fbdev->fbdev_user_cfg.scaling_width;
        scaling_height = fbdev->fbdev_user_cfg.scaling_height;
    } else {
        info->xres = pdata->xres;
        info->yres = pdata->yres;
    }

    bytes = fb_bytes_per_pixel(info->fb_fmt) * info->xres;
    info->bytes_per_line = ALIGN(bytes, 8);

    bytes = info->bytes_per_line * info->yres;
    info->bytes_per_frame = ALIGN(bytes, 8);

    info->frame_count = frame_count;

    info->fb_mem = cache_align_malloc(ALIGN(info->bytes_per_frame * info->frame_count, 4096));
    memset(info->fb_mem, 0, info->bytes_per_frame * info->frame_count);

    if (fbdev - lcd.fbdev < 4) {
        struct lcdc_layer *cfg = &fbdev->layer_cfg;
        cfg->xres = info->xres;
        cfg->yres = info->yres;
        cfg->fb_fmt = info->fb_fmt;
        cfg->xpos = xpos;
        cfg->ypos = ypos;
        cfg->scaling.enable = scaling_enable;
        cfg->scaling.xres = scaling_width;
        cfg->scaling.yres = scaling_height;
        cfg->rgb.mem = info->fb_mem;
        cfg->rgb.stride = info->bytes_per_line;
        cfg->alpha.enable = 0;
        cfg->alpha.value = 0xff;
    } else {
        struct lcdc_srdma_cfg *srdma_cfg = &fbdev->srdma_cfg;
        srdma_cfg->is_video = is_video_mode(lcd.pdata);
        srdma_cfg->fb_fmt = info->fb_fmt;
        srdma_cfg->fb_mem = info->fb_mem;
        srdma_cfg->stride = info->xres;
    }

    fbdev->ops.fb_enable = soc_fb_enable;
    fbdev->ops.fb_disable = soc_fb_disable;
    fbdev->ops.fb_get_info = soc_fb_get_info;
    fbdev->ops.fb_is_enable = soc_fb_is_enable;
    fbdev->ops.fb_pan_display = soc_fb_pan_display;
    fbdev->ops.fb_set_config = soc_fb_set_config;
    fbdev->ops.fb_enable_config = soc_fb_enable_config;
    fbdev->ops.fb_disable_config = soc_fb_disable_config;
}

void lcdc_init_fbdev(void)
{
#ifdef CONFIG_X2000_LCD_FB0_ENABLE
    init_fbdev_data(&lcd.fbdev[0], CONFIG_X2000_LCD_FB0_NAME, CONFIG_X2000_LCD_FB0_FRM_CNT);
    lcd.fbdev[0].layer_cfg.layer_order = lcdc_layer_0;
    fb_regiser(lcd.fbdev[0].name, &lcd.fbdev[0], &lcd.fbdev[0].ops);
#endif

#ifdef CONFIG_X2000_LCD_FB1_ENABLE
    init_fbdev_data(&lcd.fbdev[1], CONFIG_X2000_LCD_FB1_NAME, CONFIG_X2000_LCD_FB1_FRM_CNT);
    lcd.fbdev[1].layer_cfg.layer_order = lcdc_layer_1;
    fb_regiser(lcd.fbdev[1].name, &lcd.fbdev[1], &lcd.fbdev[1].ops);
#endif

#ifdef CONFIG_X2000_LCD_FB2_ENABLE
    init_fbdev_data(&lcd.fbdev[2], CONFIG_X2000_LCD_FB2_NAME, CONFIG_X2000_LCD_FB2_FRM_CNT);
    lcd.fbdev[2].layer_cfg.layer_order = lcdc_layer_2;
    fb_regiser(lcd.fbdev[2].name, &lcd.fbdev[2], &lcd.fbdev[2].ops);
#endif

#ifdef CONFIG_X2000_LCD_FB3_ENABLE
    init_fbdev_data(&lcd.fbdev[3], CONFIG_X2000_LCD_FB3_NAME, CONFIG_X2000_LCD_FB3_FRM_CNT);
    lcd.fbdev[3].layer_cfg.layer_order = lcdc_layer_3;
    fb_regiser(lcd.fbdev[3].name, &lcd.fbdev[3], &lcd.fbdev[3].ops);
#endif

#ifdef CONFIG_X2000_LCD_FB_SRDMA_ENABLE
    init_fbdev_data(&lcd.fbdev[4], CONFIG_X2000_LCD_FB_SRDMA_NAME, CONFIG_X2000_LCD_FB_SRDMA_FRM_CNT);
    fb_regiser(lcd.fbdev[4].name, &lcd.fbdev[4], &lcd.fbdev[4].ops);
#endif

}

static void lcdc_alloc_desc(void)
{
    int i;
    lcd.frames = malloc(sizeof(struct lcdc_frame) * 3);
    for (i = 0; i < 3;i++) {
        lcd.frames[i].framedesc = m_mem_alloc(ALIGN(sizeof(struct framedesc), 64));
        lcd.frames[i].layers[0] = m_mem_alloc(ALIGN(sizeof(struct layerdesc), 64));
        lcd.frames[i].layers[1] = m_mem_alloc(ALIGN(sizeof(struct layerdesc), 64));
        lcd.frames[i].layers[2] = m_mem_alloc(ALIGN(sizeof(struct layerdesc), 64));
        lcd.frames[i].layers[3] = m_mem_alloc(ALIGN(sizeof(struct layerdesc), 64));

        lcd.frames[i].srdmadesc = m_mem_alloc(ALIGN(sizeof(struct srdmadesc), 64));
    }
}

static int init_gpio(struct lcdc_data *pdata)
{
    int ret = -EINVAL;

    switch (pdata->lcd_mode) {
    case TFT_24BITS:
        if (pdata->out_format == OUT_FORMAT_RGB444)
            ret = tft_init_gpio(4, 4, 4);
        if (pdata->out_format == OUT_FORMAT_RGB555)
            ret = tft_init_gpio(5, 5, 5);
        if (pdata->out_format == OUT_FORMAT_RGB565)
            ret = tft_init_gpio(5, 6, 5);
        if (pdata->out_format == OUT_FORMAT_RGB666)
            ret = tft_init_gpio(6, 6, 6);
        if (pdata->out_format == OUT_FORMAT_RGB888)
            ret = tft_init_gpio(8, 8, 8);
        break;
    case TFT_18BITS:
        if (pdata->out_format == OUT_FORMAT_RGB666)
            ret = tft_18bit_init_gpio();
        else
            printf("lcdc: tft 18bits only support out_format_rgb666!\n");
        break;
    case TFT_16BITS:
        if (pdata->out_format == OUT_FORMAT_RGB565)
            ret = tft_16bit_init_gpio();
        else
            printf("lcdc: tft 16bits only support out_format_rgb565!\n");
        break;
    case TFT_8BITS_SERIAL:
    case TFT_8BITS_DUMMY_SERIAL:
        if (pdata->out_format == OUT_FORMAT_RGB888)
            ret = tft_serial_init_gpio_data8();
        if (pdata->out_format == OUT_FORMAT_RGB666 || pdata->out_format == OUT_FORMAT_RGB565)
            ret = tft_serial_init_gpio_data6();
        break;
    case TFT_MIPI:
        ret = 0;
        break;

    case SLCD_6800:
    case SLCD_8080: {
        int use_cs = 0;
        int use_rdy = pdata->slcd.enable_rdy_pin;
        int use_te = pdata->slcd.te_pin_mode == TE_LCDC_TRIGGER;
        int width = max(pdata->slcd.mcu_cmd_width, pdata->slcd.mcu_data_width);
        if (width == MCU_WIDTH_8BITS)
            ret = slcd_init_gpio_data8(use_rdy, use_te, use_cs);
        if (width == MCU_WIDTH_9BITS)
            ret = slcd_init_gpio_data9(use_rdy, use_te, use_cs);
        if (width == MCU_WIDTH_16BITS)
            ret = slcd_init_gpio_data16(use_rdy, use_te, use_cs);
        break;
    }
    case SLCD_MIPI:
        if (pdata->mipi.slcd_te_pin_mode == TE_LCDC_TRIGGER)
            ret = mipi_slcd_init_te();
        else
            ret = 0;
        break;
    case SLCD_SPI_3LINE:
    case SLCD_SPI_4LINE:
        ret = slcd_init_gpio_data0(pdata->slcd.enable_rdy_pin, pdata->slcd.te_pin_mode == TE_LCDC_TRIGGER, 0);
        break;

    default:
        printf("This mode is not currently implemented: %d\n", pdata->lcd_mode);
        break;
    }

    return ret;
}


static void init_clks(void)
{
    lcd.clk = clk_get("gate_lcd");
    lcd.pclk = clk_get("cgu_lcd");
    assert(lcd.clk);
    assert(lcd.pclk);
}


void soc_fb_init(struct lcdc_data *data)
{
    int ret;

    error_if(data == NULL);
    error_if(data->name == NULL);
    error_if(data->power_on == NULL);
    error_if(data->power_off == NULL);
    error_if(data->xres < 32 || data->xres >= 2048);
    error_if(data->yres < 32 || data->yres >= 2048);
    error_if(data->fb_fmt >= fb_fmt_NV12);

    lcd.frame_index = -1;
    lcd.pdata = data;

    if (is_slcd(data))
        error_if(check_scld_fmt(data));

    assert(!lcd.is_inited);
    lcd.is_inited = 1;

    init_clks();

    init_fbdev_user_data();

#ifdef CONFIG_X2000_FB_LAYER_MIXER
    fb_layer_mixer_init();
#endif


#ifdef CONFIG_X2000_LCD_FB_USE_DEFALUT_ORDER
    lcd.use_default_order = 1;
#endif

    init_gpio(data);

    lcdc_alloc_desc();

    lcdc_init_fbdev();

    init_fbdev_frame_layer();

    auto_calculate_pixel_clock(data);

    if (is_slcd_mipi(data) || is_tft_mipi(data)) {
        jz_mipi_dsi_init();

        ret = jz_mipi_dsi_data_init(data);
        if(ret < 0)
            panic("init dsi data error\n");
    }

    dpu_request_irq();
    lcd.display_count = 0;

#ifdef CONFIG_X2000_LCD_FB_SRDMA_ENABLE
    lcdc_srdma_init();
#endif

}

void lcdc_send_cmd(unsigned int cmd)
{
    mutex_lock(&lock);

    slcd_send_cmd(cmd);

    mutex_unlock(&lock);
}

void lcdc_send_data(unsigned int data)
{
    mutex_lock(&lock);

    slcd_send_data(data);

    mutex_unlock(&lock);
}

void lcdc_dump_regs(void)
{
    printf("FRM_CFG_ADDR %08x\n", dpu_read(FRM_CFG_ADDR));
    printf("FRM_CFG_CTRL %08x\n", dpu_read(FRM_CFG_CTRL));
    printf("CTRL %08x\n", dpu_read(CTRL));
    printf("ST %08x\n", dpu_read(ST));
    printf("CLR_ST %08x\n", dpu_read(CLR_ST));
    printf("INTC %08x\n", dpu_read(INTC));
    printf("INT_FLAG %08x\n", dpu_read(INT_FLAG));
    printf("COM_CFG %08x\n", dpu_read(COM_CFG));
    printf("PCFG_RD_CTRL %08x\n", dpu_read(PCFG_RD_CTRL));
    printf("PCFG_OFIFO %08x\n", dpu_read(PCFG_OFIFO));

    printf("FRM_DES %08x\n", dpu_read(FRM_DES));
    printf("FRM_DES %08x\n", dpu_read(FRM_DES));
    printf("FRM_DES %08x\n", dpu_read(FRM_DES));
    printf("FRM_DES %08x\n", dpu_read(FRM_DES));
    printf("FRM_DES %08x\n", dpu_read(FRM_DES));
    printf("FRM_DES %08x\n", dpu_read(FRM_DES));
    printf("FRM_DES %08x\n", dpu_read(FRM_DES));
    printf("FRM_DES %08x\n", dpu_read(FRM_DES));
    printf("FRM_DES %08x\n", dpu_read(FRM_DES));
    printf("FRM_DES %08x\n", dpu_read(FRM_DES));
    printf("FRM_DES %08x\n", dpu_read(FRM_DES));

    printf("LAY0_DES_READ %08x\n", dpu_read(LAY0_DES_READ));
    printf("LAY0_DES_READ %08x\n", dpu_read(LAY0_DES_READ));
    printf("LAY0_DES_READ %08x\n", dpu_read(LAY0_DES_READ));
    printf("LAY0_DES_READ %08x\n", dpu_read(LAY0_DES_READ));
    printf("LAY0_DES_READ %08x\n", dpu_read(LAY0_DES_READ));
    printf("LAY0_DES_READ %08x\n", dpu_read(LAY0_DES_READ));
    printf("LAY0_DES_READ %08x\n", dpu_read(LAY0_DES_READ));
    printf("LAY0_DES_READ %08x\n", dpu_read(LAY0_DES_READ));
    printf("LAY0_DES_READ %08x\n", dpu_read(LAY0_DES_READ));
    printf("LAY0_DES_READ %08x\n", dpu_read(LAY0_DES_READ));
    printf("LAY0_DES_READ %08x\n", dpu_read(LAY0_DES_READ));
    printf("LAY0_DES_READ %08x\n", dpu_read(LAY0_DES_READ));

    printf("LAY1_DES_READ %08x\n", dpu_read(LAY1_DES_READ));
    printf("FRM_CHAIN_SITE %08x\n", dpu_read(FRM_CHAIN_SITE));
    printf("LAY0_Y_SITE %08x\n", dpu_read(LAY0_Y_SITE));
    printf("LAY0_UV_SITE %08x\n", dpu_read(LAY0_UV_SITE));
    printf("LAY1_Y_SITE %08x\n", dpu_read(LAY1_Y_SITE));
    printf("LAY1_UV_SITE %08x\n", dpu_read(LAY1_UV_SITE));
    printf("LAY0_CSC_MULT_YRV %08x\n", dpu_read(LAY0_CSC_MULT_YRV));
    printf("LAY0_CSC_MULT_GUGV %08x\n", dpu_read(LAY0_CSC_MULT_GUGV));
    printf("LAY0_CSC_MULT_BU %08x\n", dpu_read(LAY0_CSC_MULT_BU));
    printf("LAY0_CSC_SUB_YUV %08x\n", dpu_read(LAY0_CSC_SUB_YUV));
    printf("LAY1_CSC_MULT_YRV %08x\n", dpu_read(LAY1_CSC_MULT_YRV));
    printf("LAY1_CSC_MULT_GUGV %08x\n", dpu_read(LAY1_CSC_MULT_GUGV));
    printf("LAY1_CSC_MULT_BU %08x\n", dpu_read(LAY1_CSC_MULT_BU));
    printf("LAY1_CSC_SUB_YUV %08x\n", dpu_read(LAY1_CSC_SUB_YUV));
    printf("DISP_COM %08x\n", dpu_read(DISP_COM));
    printf("TFT_HSYNC %08x\n", dpu_read(TFT_HSYNC));
    printf("TFT_VSYNC %08x\n", dpu_read(TFT_VSYNC));
    printf("TFT_HDE %08x\n", dpu_read(TFT_HDE));
    printf("TFT_VDE %08x\n", dpu_read(TFT_VDE));
    printf("TFT_CFG %08x\n", dpu_read(TFT_CFG));
    printf("TFT_ST %08x\n", dpu_read(TFT_ST));
    printf("SLCD_CFG %08x\n", dpu_read(SLCD_CFG));
    printf("SLCD_WR_DUTY %08x\n", dpu_read(SLCD_WR_DUTY));
    printf("SLCD_TIMING %08x\n", dpu_read(SLCD_TIMING));
    printf("SLCD_FRM_SIZE %08x\n", dpu_read(SLCD_FRM_SIZE));
    printf("SLCD_SLOW_TIME %08x\n", dpu_read(SLCD_SLOW_TIME));
    printf("SLCD_REG_IF %08x\n", dpu_read(SLCD_REG_IF));
    printf("SLCD_ST %08x\n", dpu_read(SLCD_ST));
    printf("SLCD_REG_CTRL %08x\n", dpu_read(SLCD_REG_CTRL));
}
