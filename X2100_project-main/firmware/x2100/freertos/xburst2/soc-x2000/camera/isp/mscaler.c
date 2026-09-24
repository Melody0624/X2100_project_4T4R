/*
 * Copyright (C) 2020 Ingenic Semiconductor Co., Ltd.
 *
 * Mulitiple Channel Scaler Driver
 *
 */

#include <common.h>
#include <list.h>
#include <os.h>
#include <errno.h>
#include <bit_field.h>
#include <wake_lock.h>
#include <driver/irq.h>
#include <driver/cache.h>

#include "mscaler_regs.h"
#include "mscaler.h"
#include "isp.h"
#include "../hal/vic.h"


#define MSCALER_ALIGN_SIZE              2

enum mscaler_channel_num {
    ISP_MSCALER_CHANNEL_NUM0        = 0,
    ISP_MSCALER_CHANNEL_NUM1,
    ISP_MSCALER_CHANNEL_NUM2,
    ISP_MSCALER_CHANNEL_MAX,
};

struct mscaler_data;

struct mscaler_channel {
    int index;
    int channel;
    char device_name[16];   /* 设备节点名字 */
    camera_hd_t hd;

    int is_init;
    int is_power_on;
    int is_stream_on;     /* 0: stopped, 1: streaming */

    unsigned int wait_timeout;
    volatile unsigned int dma_index;
    critical_thread_cond_t cond;

    void *mem;
    unsigned int mem_cnt;
    unsigned int frm_size;
    unsigned int uv_data_offset;
    volatile unsigned int frame_counter;

    struct list_head free_list;
    struct list_head usable_list;
    struct frame_data *t[2];
    struct frame_data *frames;

    struct mscaler_data *drv;

    struct mutex lock;
    struct spinlock spinlock;

    struct camera_info info;                /* 返回信息,应用保存映射地址 */
    struct frame_image_format output_fmt;   /* 输出信息 */

    unsigned int chn_done_cnt;      /* 通道处理完成帧计数 */
    unsigned int chn_output_cnt;    /* 通道输出完成帧计数 */
};

struct mscaler_data {
    int index;
    int is_enable;
    int is_finish;

    /* Camera Device */
    char *mscaler_name;              /* 设备名字 */
    struct camera_device camera;
    struct mscaler_channel mscaler_ch[MSCALER_MAX_CH];

    struct mutex lock;
};

static struct mscaler_data jz_mscaler_dev[2] = {
    {
        .index                  = 0,
        .mscaler_name           = "mscaler0",
    },

    {
        .index                  = 1,
        .mscaler_name           = "mscaler1",
    },
};

camera_frame_error_type soc_mscaler_get_frame_error(camera_hd_t *camera_hd);

/*
 * MScaler Operation
 */
static const unsigned long mscaler_iobase[] = {
        KSEG1ADDR(MSCALER0_IOBASE),
        KSEG1ADDR(MSCALER1_IOBASE),
};

#define MSCALER_ADDR(index, reg)        ((volatile unsigned long *)((mscaler_iobase[index]) + (reg)))

static inline void mscaler_write_reg(int index, unsigned int reg, unsigned int val)
{
    *MSCALER_ADDR(index, reg) = val;
}

static inline unsigned int mscaler_read_reg(int index, unsigned int reg)
{
    return *MSCALER_ADDR(index, reg);
}

static inline void mscaler_set_bit(int index, unsigned int reg, unsigned int start, unsigned int end, unsigned int val)
{
    set_bit_field_v(MSCALER_ADDR(index, reg), start, end, val);
}

static inline unsigned int mscaler_get_bit(int index, unsigned int reg, unsigned int start, unsigned int end)
{
    return get_bit_field_v(MSCALER_ADDR(index, reg), start, end);
}

static inline void mscaler_module_clock_disable(int index)
{
    mscaler_write_reg(index, MSCA_CLK_DIS, 1);
}

static inline void mscaler_module_clock_enable(int index)
{
    mscaler_write_reg(index, MSCA_CLK_DIS, 0);
}

static void add_to_free_list(struct mscaler_channel *data, struct frame_data *frm)
{
    frm->status = frame_status_free;
    list_add_tail(&frm->link, &data->free_list);
}

static struct frame_data *get_free_frm(struct mscaler_channel *data)
{
    if (list_empty(&data->free_list))
        return NULL;

    struct frame_data *frm = list_first_entry(&data->free_list, struct frame_data, link);

    list_del_init(&frm->link);

    return frm;
}

static void add_to_usable_list(struct mscaler_channel *data, struct frame_data *frm)
{
    frm->status = frame_status_usable;
    data->frame_counter++;
    list_add_tail(&frm->link, &data->usable_list);
}

static struct frame_data *get_usable_frm(struct mscaler_channel *data)
{
    if (list_empty(&data->usable_list))
        return NULL;

    data->frame_counter--;

    struct frame_data *frm = list_first_entry(&data->usable_list, struct frame_data, link);

    list_del_init(&frm->link);

    return frm;
}

static inline void tiziano_mscaler_frame_rate_control(struct mscaler_channel *data, unsigned char ratio)
{
    int index = data->index;
    int channel = data->channel;

    if (ratio > 100)
        ratio = 100;

    switch (ratio) {
    case 100:
        mscaler_write_reg(index, FRA_CTRL_LOOP(channel), 31);
        mscaler_write_reg(index, FRA_CTRL_MASK(channel), 0xFFFFFFFF);
        break;

    case 90 ... 99:
        mscaler_write_reg(index, FRA_CTRL_LOOP(channel), 9);
        mscaler_write_reg(index, FRA_CTRL_MASK(channel), 0x1FF);
        break;

    case 80 ... 89:
        mscaler_write_reg(index, FRA_CTRL_LOOP(channel), 9);
        mscaler_write_reg(index, FRA_CTRL_MASK(channel), 0x377);
        break;

    case 70 ... 79:
        mscaler_write_reg(index, FRA_CTRL_LOOP(channel), 9);
        mscaler_write_reg(index, FRA_CTRL_MASK(channel), 0x2DB);
        break;

    case 60 ... 69:
        mscaler_write_reg(index, FRA_CTRL_LOOP(channel), 9);
        mscaler_write_reg(index, FRA_CTRL_MASK(channel), 0x1AD);
        break;

    case 50 ... 59:
        mscaler_write_reg(index, FRA_CTRL_LOOP(channel), 9);
        mscaler_write_reg(index, FRA_CTRL_MASK(channel), 0x155);
        break;

    case 40 ... 49:
        mscaler_write_reg(index, FRA_CTRL_LOOP(channel), 9);
        mscaler_write_reg(index, FRA_CTRL_MASK(channel), 0x129);
        break;

    case 30 ... 39:
        mscaler_write_reg(index, FRA_CTRL_LOOP(channel), 9);
        mscaler_write_reg(index, FRA_CTRL_MASK(channel), 0x122);
        break;

    case 20 ... 29:
        mscaler_write_reg(index, FRA_CTRL_LOOP(channel), 9);
        mscaler_write_reg(index, FRA_CTRL_MASK(channel), 0x210);
        break;

    case 10 ... 19:
        mscaler_write_reg(index, FRA_CTRL_LOOP(channel), 9);
        mscaler_write_reg(index, FRA_CTRL_MASK(channel), 0x010);
        break;

    case 0 ... 9:
        mscaler_write_reg(index, FRA_CTRL_LOOP(channel), 31);
        mscaler_write_reg(index, FRA_CTRL_MASK(channel), 1);
        break;
    }
}

static int tiziano_mscaler_hal_stream_on(struct mscaler_channel *data)
{
    int index = data->index;
    int channel = data->channel;
    struct mscaler_data *drv = data->drv;
    int ret = 0;

    struct sensor_attr *attr = drv->camera.sensor;
    struct frame_image_format *output_fmt = &data->output_fmt;

    unsigned int val = 0;
    unsigned int fmt = 0;

    int input_width;
    int input_height;

    if ( (attr->dbus_type == SENSOR_DATA_BUS_MIPI) && (attr->mipi.mipi_crop.enable) ) {
        input_width = attr->mipi.mipi_crop.output_width;
        input_height = attr->mipi.mipi_crop.output_height;
    } else {
        input_width = attr->sensor_info.width;
        input_height = attr->sensor_info.height;
    }

    unsigned int output_width = input_width;
    unsigned int output_height = input_height;
    unsigned int output_format = output_fmt->pixel_format;

    switch (output_format) {
    case CAMERA_PIX_FMT_NV21:
        fmt = OUT_FMT_NV21;
        break;

    case CAMERA_PIX_FMT_NV12:
    case CAMERA_PIX_FMT_YVU420:
    case CAMERA_PIX_FMT_JZ420B:
        fmt = OUT_FMT_NV12;
        break;

    case CAMERA_PIX_FMT_GREY:
        fmt = OUT_FMT_NV12;
        fmt |= OUT_FMT_Y_OUT_ONLY;
        break;

    default:
        printf("%s : output format not supported!\n", data->device_name);
        return -EINVAL;
    }

    /*
     * 1. mscaler_hw_configure
     */

    /* scaler */
    if (output_fmt->scaler.enable) {
        output_width = output_fmt->scaler.width;
        output_width = ALIGN(output_width, MSCALER_ALIGN_SIZE);
        output_height = output_fmt->scaler.height;
    }

    unsigned int step_w = 0, step_h = 0;
    step_w = input_width * 512 / output_width;
    step_h = input_height * 512 / output_height;

    unsigned long resize_step = 0;
    set_bit_field(&resize_step, RESIZW_STEP_WIDTH, step_w);
    set_bit_field(&resize_step, RESIZW_STEP_HEIGHT, step_h);
    mscaler_write_reg(index, RSZ_STEP(channel), resize_step);


    unsigned long resize_output_image = 0;
    set_bit_field(&resize_output_image, RESIZE_OSIZE_WIDTH, output_width);
    set_bit_field(&resize_output_image, RESIZE_OSIZE_HEIGHT, output_height);
    mscaler_write_reg(index, RSZ_OSIZE(channel), resize_output_image);

    /* set scaler coef */
    if (output_fmt->scaler.enable) {
        if ((input_width > output_fmt->scaler.width || input_height > output_fmt->scaler.height)) {
            if (output_fmt->scaler.width >= 1280 && output_fmt->scaler.height >= 720) {
                mscaler_write_reg(index, COE_ZERO_VRSZ_H(channel), (0 << 11) | 512);
                mscaler_write_reg(index, COE_ZERO_VRSZ_L(channel), (0 << 11) | 0);
                mscaler_write_reg(index, COE_ZERO_HRSZ_H(channel), (0 << 11) | 512);
                mscaler_write_reg(index, COE_ZERO_HRSZ_L(channel), (0 << 11) | 0);
            } else if (output_fmt->scaler.width >= 960 && output_fmt->scaler.height >= 540) {
                mscaler_write_reg(index, COE_ZERO_VRSZ_H(channel), (0 << 11) | 256);
                mscaler_write_reg(index, COE_ZERO_VRSZ_L(channel), (0 << 11) | 256);
                mscaler_write_reg(index, COE_ZERO_HRSZ_H(channel), (0 << 11) | 256);
                mscaler_write_reg(index, COE_ZERO_HRSZ_L(channel), (0 << 11) | 256);
            } else {
                mscaler_write_reg(index, COE_ZERO_VRSZ_H(channel), (128 << 11) | 128);
                mscaler_write_reg(index, COE_ZERO_VRSZ_L(channel), (128 << 11) | 128);
                mscaler_write_reg(index, COE_ZERO_HRSZ_H(channel), (128 << 11) | 128);
                mscaler_write_reg(index, COE_ZERO_HRSZ_L(channel), (128 << 11) | 128);
            }
        } else {
            mscaler_write_reg(index, COE_ZERO_VRSZ_H(channel), (0 << 11) | 512);
            mscaler_write_reg(index, COE_ZERO_VRSZ_L(channel), (0 << 11) | 0);
            mscaler_write_reg(index, COE_ZERO_HRSZ_H(channel), (0 << 11) | 512);
            mscaler_write_reg(index, COE_ZERO_HRSZ_L(channel), (0 << 11) | 0);
        }
    }

    /* crop */
    unsigned long crop_pos = 0;
    if (output_fmt->crop.enable) {
        output_width = output_fmt->crop.width;
        output_width = ALIGN(output_width, MSCALER_ALIGN_SIZE);
        output_height = output_fmt->crop.height;

        set_bit_field(&crop_pos, CROP_OPOS_START_X, output_fmt->crop.left);
        set_bit_field(&crop_pos, CROP_OPOS_START_Y, output_fmt->crop.top);
    }

    mscaler_write_reg(index, CROP_OPOS(channel), crop_pos);

    unsigned long crop_size = 0;
    set_bit_field(&crop_size, CROP_OSIZE_WIDTH, output_width);
    set_bit_field(&crop_size, CROP_OSIZE_HEIGHT, output_height);
    mscaler_write_reg(index, CROP_OSIZE(channel), crop_size);

    /* output image stride */
    mscaler_write_reg(index, DMAOUT_Y_STRI(channel), output_width);
    mscaler_write_reg(index, DMAOUT_UV_STRI(channel), output_width);


    /* uv pixel re-sample mode  */
    val = mscaler_read_reg(index, MSCA_DMAOUT_ARB);
    val |= 1 << (channel + 1);
    mscaler_write_reg(index, MSCA_DMAOUT_ARB, val);

    /* output format set */
    mscaler_write_reg(index, OUT_FMT(channel), fmt);

    /*
     * 2. mscaler_hw_start
     */
    val = mscaler_read_reg(index, MSCA_CH_EN);
    val |= 1 << channel;            /* resize enable */
    val |= (1 << 8) << channel;     /* vertical resize enable */
    val |= (1 << 11) << channel;    /* horizontal resize_enable */
    mscaler_write_reg(index, MSCA_CH_EN, val);

    return ret;
}

static int tiziano_mscaler_hal_stream_off(struct mscaler_channel *data)
{
    int index = data->index;
    int channel = data->channel;

    /* resize disable */
    unsigned int timeout = 0xffffff;
    unsigned int val = 0;
    val = mscaler_read_reg(index, MSCA_CH_EN);
    val &= ~(1 << channel);
    mscaler_write_reg(index, MSCA_CH_EN, val);

    /* polling status to make sure channel stopped */
    do {
        val = mscaler_read_reg(index, MSCA_CH_STA);
    } while(!!(val & (1<<channel)) && --timeout);

    if(!timeout)
        printf("%s : [Warning] mscaler disable timeout!\n", data->device_name);

    /* clear fifo */
    mscaler_write_reg(index, DMAOUT_Y_ADDR_CLR(channel), 1);
    mscaler_write_reg(index, DMAOUT_UV_ADDR_CLR(channel), 1);
    mscaler_write_reg(index, DMAOUT_Y_LAST_ADDR_CLR(channel), 1);
    mscaler_write_reg(index, DMAOUT_UV_LAST_ADDR_CLR(channel), 1);

    return 0;
}

static inline void tiziano_mscaler_set_dma_addr(struct mscaler_channel *data, struct frame_data *frm)
{
    unsigned int fifo_full = 0;
    unsigned int y_fifo_status = 0, uv_fifo_status = 0;
    int index = data->index;
    int channel = data->channel;

    y_fifo_status = mscaler_read_reg(index, Y_ADDR_FIFO_STA(channel));
    uv_fifo_status = mscaler_read_reg(index, UV_ADDR_FIFO_STA(channel));

    if ((y_fifo_status & MSCA_CHx_Y_ADDR_FIFO_STA_FULL) ||
            (uv_fifo_status & MSCA_CHx_UV_ADDR_FIFO_STA_FULL)) {
        fifo_full = 1;
    }

    /* Set DMA Address */
    if (fifo_full)
        panic("%s : failed to set dma addr\n", data->device_name);

    unsigned long y_addr = 0;
    unsigned long uv_addr = 0;

    frm->status = frame_status_trans;
    /*Y*/
    y_addr = virt_to_phys(frm->addr);
    mscaler_write_reg(index, DMAOUT_Y_ADDR(channel), y_addr);

    /*UV*/
    uv_addr = y_addr + data->uv_data_offset;
    mscaler_write_reg(index, DMAOUT_UV_ADDR(channel), uv_addr);
}

/*
 * In irq context
 */
static int tiziano_mscaler_irq_notify_ch_done(int index, int channel)
{
    struct mscaler_data *drv = &jz_mscaler_dev[index];
    struct mscaler_channel *data = &drv->mscaler_ch[channel];

    unsigned long flags = 0;

    spin_lock_irqsave(&data->spinlock, flags);

    data->chn_done_cnt++;

    /* 由于mscaler stop stream时, ISP 可能还会产生中断,这个时候不用处理 */
    if(data->is_stream_on == 0) {
        spin_unlock_irqrestore(&data->spinlock, flags);
        return 0;
    }

    struct frame_data *frm, *usable_frm = NULL;

    volatile int frame_index = data->dma_index;
    data->dma_index = !frame_index;

    /*
     * 当传输列表只有一帧的时候，不能用这一帧
     */
    if (data->t[0] != data->t[1]) {
        usable_frm = data->t[frame_index];
    }

    /*
     * 1 优先从空闲列表中获取新的帧加入传输
     * 2 如果空闲列表没有帧，那么传输完成列表中获取
     * 3 如果完成列表也没有，那么用下一帧做保底
     */
    frm = get_free_frm(data);
    if (!frm)
        frm = get_usable_frm(data);
    if (!frm)
        frm = data->t[!frame_index];

    data->t[frame_index] = frm;

    tiziano_mscaler_set_dma_addr(data, frm);
    if (usable_frm) {
        usable_frm->info.sequence = data->chn_done_cnt;
        usable_frm->info.timestamp = get_time_us();

        add_to_usable_list(data, usable_frm);
        critical_thread_cond_signal(&data->cond);
    }

    spin_unlock_irqrestore(&data->spinlock, flags);

    return 0;
}

static inline void tiziano_mscaler_dump_reg(int index)
{
    printf("MSCA_CTRL                       :0x%08x\n", mscaler_read_reg(index, MSCA_CTRL));
    printf("MSCA_CH_EN                      :0x%08x\n", mscaler_read_reg(index, MSCA_CH_EN));
    printf("MSCA_CH_STA                     :0x%08x\n", mscaler_read_reg(index, MSCA_CH_STA));
    printf("MSCA_DMAOUT_ARB                 :0x%08x\n", mscaler_read_reg(index, MSCA_DMAOUT_ARB));
    printf("MSCA_CLK_GATE_EN                :0x%08x\n", mscaler_read_reg(index, MSCA_CLK_GATE_EN));
    printf("MSCA_CLK_DIS                    :0x%08x\n", mscaler_read_reg(index, MSCA_CLK_DIS));
    printf("MSCA_SRC_IN                     :0x%08x\n", mscaler_read_reg(index, MSCA_SRC_IN));
    printf("MSCA_GLO_RSZ_COEF_WR            :0x%08x\n", mscaler_read_reg(index, MSCA_GLO_RSZ_COEF_WR));
    printf("MSCA_SYS_PRO_CLK_EN             :0x%08x\n", mscaler_read_reg(index, MSCA_SYS_PRO_CLK_EN));
    printf("MSCA_DS0_CLK_NUM                :0x%08x\n", mscaler_read_reg(index, MSCA_DS0_CLK_NUM));
    printf("MSCA_DS1_CLK_NUM                :0x%08x\n", mscaler_read_reg(index, MSCA_DS1_CLK_NUM));
    printf("MSCA_DS2_CLK_NUM                :0x%08x\n", mscaler_read_reg(index, MSCA_DS2_CLK_NUM));
    printf("\n");
    printf("\n");

    int channel = 0;
    for (channel = 0; channel < MSCALER_MAX_CH; channel++) {
        printf("RSZ_OSIZE(%d)               :0x%08x\n", channel, mscaler_read_reg(index, RSZ_OSIZE(channel)));
        printf("RSZ_STEP(%d)                :0x%08x\n", channel, mscaler_read_reg(index, RSZ_STEP(channel)));
        printf("CROP_OPOS(%d)               :0x%08x\n", channel, mscaler_read_reg(index, CROP_OPOS(channel)));
        printf("CROP_OSIZE(%d)              :0x%08x\n", channel, mscaler_read_reg(index, CROP_OSIZE(channel)));
        printf("FRA_CTRL_LOOP(%d)           :0x%08x\n", channel, mscaler_read_reg(index, FRA_CTRL_LOOP(channel)));
        printf("FRA_CTRL_MASK(%d)           :0x%08x\n", channel, mscaler_read_reg(index, FRA_CTRL_MASK(channel)));
        printf("MS0_POS(%d)                 :0x%08x\n", channel, mscaler_read_reg(index, MS0_POS(channel)));
        printf("MS0_SIZE(%d)                :0x%08x\n", channel, mscaler_read_reg(index, MS0_SIZE(channel)));
        printf("MS0_VALUE(%d)               :0x%08x\n", channel, mscaler_read_reg(index, MS0_VALUE(channel)));
        printf("MS1_POS(%d)                 :0x%08x\n", channel, mscaler_read_reg(index, MS1_POS(channel)));
        printf("MS1_SIZE(%d)                :0x%08x\n", channel, mscaler_read_reg(index, MS1_SIZE(channel)));
        printf("MS1_VALUE(%d)               :0x%08x\n", channel, mscaler_read_reg(index, MS1_VALUE(channel)));
        printf("MS2_POS(%d)                 :0x%08x\n", channel, mscaler_read_reg(index, MS2_POS(channel)));
        printf("MS2_SIZE(%d)                :0x%08x\n", channel, mscaler_read_reg(index, MS2_SIZE(channel)));
        printf("MS2_VALUE(%d)               :0x%08x\n", channel, mscaler_read_reg(index, MS2_VALUE(channel)));
        printf("MS3_POS(%d)                 :0x%08x\n", channel, mscaler_read_reg(index, MS3_POS(channel)));
        printf("MS3_SIZE(%d)                :0x%08x\n", channel, mscaler_read_reg(index, MS3_SIZE(channel)));
        printf("MS3_VALUE(%d)               :0x%08x\n", channel, mscaler_read_reg(index, MS3_VALUE(channel)));
        printf("OUT_FMT(%d)                 :0x%08x\n", channel, mscaler_read_reg(index, OUT_FMT(channel)));
        printf("DMAOUT_Y_ADDR(%d)           :0x%08x\n", channel, mscaler_read_reg(index, DMAOUT_Y_ADDR(channel)));
        printf("Y_ADDR_FIFO_STA(%d)         :0x%08x\n", channel, mscaler_read_reg(index, Y_ADDR_FIFO_STA(channel)));

        printf("Y_LAST_ADDR_FIFO_STA(%d)    :0x%08x\n", channel, mscaler_read_reg(index, Y_LAST_ADDR_FIFO_STA(channel)));
        printf("DMAOUT_Y_STRI(%d)           :0x%08x\n", channel, mscaler_read_reg(index, DMAOUT_Y_STRI(channel)));
        printf("DMAOUT_UV_ADDR(%d)          :0x%08x\n", channel, mscaler_read_reg(index, DMAOUT_UV_ADDR(channel)));
        printf("UV_ADDR_FIFO_STA(%d)        :0x%08x\n", channel, mscaler_read_reg(index, UV_ADDR_FIFO_STA(channel)));

        printf("UV_LAST_ADDR_FIFO_STA(%d)   :0x%08x\n", channel, mscaler_read_reg(index, UV_LAST_ADDR_FIFO_STA(channel)));
        printf("DMAOUT_UV_STRI(%d)          :0x%08x\n", channel, mscaler_read_reg(index, DMAOUT_UV_STRI(channel)));
        printf("DMAOUT_Y_ADDR_CLR(%d)       :0x%08x\n", channel, mscaler_read_reg(index, DMAOUT_Y_ADDR_CLR(channel)));
        printf("DMAOUT_UV_ADDR_CLR(%d)      :0x%08x\n", channel, mscaler_read_reg(index, DMAOUT_UV_ADDR_CLR(channel)));
        printf("DMAOUT_Y_LAST_ADDR_CLR(%d)  :0x%08x\n", channel, mscaler_read_reg(index, DMAOUT_Y_LAST_ADDR_CLR(channel)));
        printf("DMAOUT_UV_LAST_ADDR_CLR(%d) :0x%08x\n", channel, mscaler_read_reg(index, DMAOUT_UV_LAST_ADDR_CLR(channel)));
        printf("DMAOUT_Y_ADDR_SEL(%d)       :0x%08x\n", channel, mscaler_read_reg(index, DMAOUT_Y_ADDR_SEL(channel)));
        printf("DMAOUT_UV_ADDR_SEL(%d)      :0x%08x\n", channel, mscaler_read_reg(index, DMAOUT_UV_ADDR_SEL(channel)));
        printf("\n");
    }
}

static void init_frm_lists(struct mscaler_channel *data)
{
    INIT_LIST_HEAD(&data->free_list);
    INIT_LIST_HEAD(&data->usable_list);
    data->dma_index = 0;
    data->frame_counter = 0;
    data->wait_timeout = 0;

    int i;
    for (i = 0; i < data->mem_cnt; i++) {
        struct frame_data *frm = &data->frames[i];
        frm->addr = data->mem + i * data->frm_size;

        frm->info.index = i;
        frm->info.width = data->output_fmt.width;
        frm->info.height = data->output_fmt.height;
        frm->info.pixfmt = data->output_fmt.pixel_format;
        frm->info.size = data->output_fmt.frame_size;
        frm->info.vaddr = frm->addr;
        frm->info.paddr = virt_to_phys(frm->addr);
        add_to_free_list(data, frm);
    }
}

static void reset_frm_lists(struct mscaler_channel *data)
{
    int i;

    for (i = 0; i < data->mem_cnt; i++) {
        struct frame_data *frm = &data->frames[i];
        if (frm->status == frame_status_user)
            continue;

        if (data->t[0] != frm && data->t[1] != frm)
            list_del_init(&frm->link);
        add_to_free_list(data, frm);
    }

    data->dma_index = 0;
    data->frame_counter = 0;
    data->wait_timeout = 0;
}

static inline void *m_dma_alloc_coherent(int size)
{
    return memalign(4096, ALIGN(size, cache_line_size()));
}
static inline void m_dma_free_coherent(void *mem, int size)
{
    free(mem);
}

static inline void m_cache_sync(void *mem, int size)
{
    invalidate_dcache_force((unsigned long) mem, size);
}

static int tiziano_mscaler_alloc_mem(struct mscaler_channel *data)
{
    struct mscaler_data *drv = data->drv;
    struct frame_image_format *output_fmt = &data->output_fmt;

    int mem_cnt = output_fmt->frame_nums + 1;
    int frame_size;     /* 帧对齐之后大小 */
    int line_size;      /* 行对齐之后大小 */
    int uv_data_offset;
    int output_width;
    int output_height;
    int frame_align_size;

    assert(mem_cnt >= 2);
    assert(data->mem == NULL);

    /*
     * 数据输出分辨率判断顺序(用于申请buf):
     * 首先crop使能, 则为 crop输出分辨率
     * 其次scaler使能,则为scaler输出分辨率
     * 最后,上述功能均是禁止,则输出分辨率与输入分辨率相同
     */
    if (output_fmt->crop.enable) {
        output_width = output_fmt->crop.width;
        output_height = output_fmt->crop.height;
    } else if (output_fmt->scaler.enable) {
        output_width = output_fmt->scaler.width;
        output_height = output_fmt->scaler.height;
    } else {
        if ( (drv->camera.sensor->dbus_type == SENSOR_DATA_BUS_MIPI) && (drv->camera.sensor->mipi.mipi_crop.enable) ) {
            output_width = drv->camera.sensor->mipi.mipi_crop.output_width;
            output_height = drv->camera.sensor->mipi.mipi_crop.output_height;
        } else {
            output_width = drv->camera.sensor->sensor_info.width;
            output_height = drv->camera.sensor->sensor_info.height;
        }
    }

    output_width = ALIGN(output_width, MSCALER_ALIGN_SIZE);

    if (camera_fmt_is_NV12(output_fmt->pixel_format)) {
        line_size = output_width;
        frame_size = line_size * output_height;
        uv_data_offset = frame_size;
        frame_size += frame_size / 2;

        output_fmt->frame_size = output_width * output_height * 3 / 2;

    } else if (camera_fmt_is_8BIT(output_fmt->pixel_format)) {
        line_size = output_width;
        frame_size = line_size * output_height;
        uv_data_offset = 0;

        output_fmt->frame_size = output_width * output_height;

    } else {
        line_size = output_width * 2;
        frame_size = line_size * output_height;
        uv_data_offset = 0;

        output_fmt->frame_size = output_width * output_height * 2;
    }

    frame_align_size = ALIGN(frame_size, 4096);
    data->mem = m_dma_alloc_coherent(frame_align_size * mem_cnt);
    if (data->mem == NULL) {
        printf("%s : failed to alloc mem: %u\n", data->device_name, frame_align_size * mem_cnt);
        return -ENOMEM;
    }

    data->frm_size = frame_align_size;
    data->mem_cnt = mem_cnt;
    data->uv_data_offset = uv_data_offset;

    data->frames = malloc(data->mem_cnt * sizeof(data->frames[0]));
    assert(data->frames);

    init_frm_lists(data);

    /* 计算一帧时间 :单位us */
    drv->camera.sensor->info.fps =
            (drv->camera.sensor->sensor_info.fps >> 16) / (drv->camera.sensor->sensor_info.fps & 0xFFFF);
    /* 设置输出的info信息 */
    memcpy(&data->info, &drv->camera.sensor->info, sizeof(data->info));
    data->info.width = output_width;
    data->info.height = output_height;
    data->info.frame_size = frame_size;
    data->info.frame_align_size = frame_align_size;
    data->info.line_length = line_size;
    data->info.data_fmt   = output_fmt->pixel_format;

    data->info.frame_nums = mem_cnt;
    data->info.phys_mem = virt_to_phys(data->mem);

    return 0;
}

static int tiziano_mscaler_free_mem(struct mscaler_channel *data)
{
    if (data->mem) {
        m_dma_free_coherent(data->mem, data->mem_cnt * data->frm_size);
        free(data->frames);
        data->t[0] = NULL;
        data->t[1] = NULL;
        data->mem = NULL;
        data->frames = NULL;
    }

    return 0;
}

static void init_dma_addr(struct mscaler_channel *data)
{
    struct frame_data *frm;

    frm = get_free_frm(data);
    data->t[0] = frm;
    tiziano_mscaler_set_dma_addr(data, frm);

    frm = get_free_frm(data);
    if (!frm)
        frm = data->t[0];
    data->t[1] = frm;

    tiziano_mscaler_set_dma_addr(data, frm);
}

int tiziano_mscaler_interrupt_service_routine(int index, unsigned int status)
{
    int ret = 0;
    //int ret = -EIO;
    /* ingenic: 注意检查AE中断 status = 0x10000  */
    //printf("==tiziano_mscaler_interrupt_service_routine==status=0x%x\n", status);
    if(status & (1 << MSCA_CH0_FRM_DONE_INT)) {
        ret = tiziano_mscaler_irq_notify_ch_done(index, 0);
    }

    if(status & (1 << MSCA_CH1_FRM_DONE_INT)) {
        ret = tiziano_mscaler_irq_notify_ch_done(index, 1);
    }

    if(status & (1 << MSCA_CH2_FRM_DONE_INT)) {
        ret = tiziano_mscaler_irq_notify_ch_done(index, 2);
    }

    /*TODO: add handler.*/
    if(status & (1 << MSCA_CH0_CROP_ERR_INT)) {

    }

    if(status & (1 << MSCA_CH1_CROP_ERR_INT)) {

    }

    if(status & (1 << MSCA_CH2_CROP_ERR_INT)) {

    }

    return ret;
}


static unsigned int mscaler_output_res[][2] = {
    {MSCALER_OUTPUT0_MAX_WIDTH, MSCALER_OUTPUT0_MAX_HEIGHT},
    {MSCALER_OUTPUT1_MAX_WIDTH, MSCALER_OUTPUT1_MAX_HEIGHT},
    {MSCALER_OUTPUT2_MAX_WIDTH, MSCALER_OUTPUT2_MAX_HEIGHT},
};

static void tiziano_mscaler_subdev_stream_off(struct mscaler_channel *data)
{
    struct mscaler_data *drv = data->drv;
    int index = data->index;

    mutex_lock(&drv->lock);

    if (--drv->camera.is_stream_on == 0) {
        tiziano_isp_stream_off(index, drv->camera.sensor);
    }

    mutex_unlock(&drv->lock);
}

static int tiziano_mscaler_subdev_stream_on(struct mscaler_channel *data)
{
    struct mscaler_data *drv = data->drv;
    int index = data->index;
    int ret = 0;

    mutex_lock(&drv->lock);

    if (drv->camera.is_stream_on++ == 0) {
        tiziano_isp_stream_on(index,  drv->camera.sensor);
    }

    mutex_unlock(&drv->lock);

    return ret;
}

static int tiziano_mscaler_subdev_power_on(struct mscaler_channel *data)
{
    struct mscaler_data *drv = data->drv;
    int ret = 0;

    mutex_lock(&drv->lock);

    if (drv->camera.is_power_on == 0)
        ret = tiziano_isp_power_on(drv->index);

    if (!ret)
        drv->camera.is_power_on++;

    mutex_unlock(&drv->lock);

    return ret;
}

static void tiziano_mscaler_subdev_power_off(struct mscaler_channel *data)
{
    struct mscaler_data *drv = data->drv;

    mutex_lock(&drv->lock);

    drv->camera.is_power_on--;
    if (drv->camera.is_power_on == 0)
        tiziano_isp_power_off(drv->index);

    mutex_unlock(&drv->lock);
}

static void tiziano_mscaler_stream_off_nolock(struct mscaler_channel *data)
{
    unsigned long flags = 0;

    if (!data->is_stream_on)
        return;

    spin_lock_irqsave(&data->spinlock, flags);

    tiziano_mscaler_hal_stream_off(data);

    data->is_stream_on = 0;

    spin_unlock_irqrestore(&data->spinlock, flags);

    tiziano_mscaler_subdev_stream_off(data);
}

static int tiziano_mscaler_stream_on_nolock(struct mscaler_channel *data)
{
    int ret = 0;
    unsigned long flags = 0;

    if (data->is_stream_on)
        return 0;

    reset_frm_lists(data);

    init_dma_addr(data);

    spin_lock_irqsave(&data->spinlock, flags);

    ret = tiziano_mscaler_hal_stream_on(data);
    if (ret < 0) {
        spin_unlock_irqrestore(&data->spinlock, flags);
        printf("%s : failed to stream on mscaler, %d\n", data->device_name, ret);
        return ret;
    }

    spin_unlock_irqrestore(&data->spinlock, flags);

    ret = tiziano_mscaler_subdev_stream_on(data);
    if (ret) {
        printf("%s : failed to stream on camera, %d\n", data->device_name, ret);

        spin_lock_irqsave(&data->spinlock, flags);
        tiziano_mscaler_hal_stream_off(data);
        spin_unlock_irqrestore(&data->spinlock, flags);
        return ret;
    }

    data->is_stream_on = 1;

    return 0;
}

static void tiziano_mscaler_power_off_nolock(struct mscaler_channel *data)
{
    if (!data->is_power_on)
        return;

    tiziano_mscaler_stream_off_nolock(data);

    tiziano_mscaler_subdev_power_off(data);

    data->is_power_on = 0;
}

static int tiziano_mscaler_power_on_nolock(struct mscaler_channel *data)
{
    if (data->is_power_on)
        return 0;

    int ret = tiziano_mscaler_subdev_power_on(data);
    if (ret) {
        printf("%s : failed to power on camera, %d\n", data->device_name, ret);
        return ret;
    }

    data->is_power_on = 1;

    return ret;
}

static inline void camera_dump_frame_info(struct frame_image_format *fmt)
{
    if (fmt == NULL) {
        printf("fmt is NULL");
        return ;
    }

    printf("========dump frame image format=======\n");

    printf("width    = %d\n", fmt->width);
    printf("height    = %d\n", fmt->height);
    printf("pixel_format    = %d\n", fmt->pixel_format);
    printf("frame_size      = %d\n", fmt->frame_size);

    printf("scaler_enable   = %d\n", fmt->scaler.enable);
    printf("scaler_width    = %d\n", fmt->scaler.width);
    printf("scaler_height   = %d\n", fmt->scaler.height);

    printf("crop_enable     = %d\n", fmt->crop.enable);
    printf("crop_top        = %d\n", fmt->crop.top);
    printf("crop_left       = %d\n", fmt->crop.left);
    printf("crop_width      = %d\n", fmt->crop.width);
    printf("crop_height     = %d\n", fmt->crop.height);

    printf("frame_nums      = %d\n", fmt->frame_nums);
}

static inline int is_camera_frame_parameter_valid(struct mscaler_channel *data, struct frame_image_format *fmt)
{
    struct mscaler_data *drv = data->drv;
    struct sensor_attr *sensor = drv->camera.sensor;
    int channel = data->channel;
    unsigned int output_width, output_height;

    /* 分辨率检查 */
    if (fmt->width < 8 || fmt->height < 8) {
        printf("%s : width(%d) or height(%d) < 8\n", data->device_name, fmt->width, fmt->height);
        return -EINVAL;
    }

    if ( (sensor->dbus_type == SENSOR_DATA_BUS_MIPI) && (sensor->mipi.mipi_crop.enable) ) {
        output_width = sensor->mipi.mipi_crop.output_width;
        output_height = sensor->mipi.mipi_crop.output_height;
    } else {
        output_width = sensor->sensor_info.width;
        output_height = sensor->sensor_info.height;
    }

    if (fmt->scaler.enable) {
        if ( (fmt->scaler.width > mscaler_output_res[channel][0]) ||
            (fmt->scaler.height > mscaler_output_res[channel][1])) {
        printf("%s : scaler(%d X %d) out of range(%d X %d)\n", data->device_name,
                fmt->scaler.width, fmt->scaler.height,
                mscaler_output_res[channel][0], mscaler_output_res[channel][1]);
            return -EINVAL;
        }

        /* Crop分辨率需在Scaler的范围之内 */
        if (fmt->crop.enable) {
            if ( (fmt->crop.height > fmt->scaler.height)  ||
                    (fmt->crop.width > fmt->scaler.width) ) {
            printf("%s : coordinate(%d,%d) crop(%d X %d) out of scaler range(%d X %d)\n", data->device_name,
                    fmt->crop.top,  fmt->crop.left, fmt->crop.width, fmt->crop.height,
                    fmt->scaler.width, fmt->scaler.height);
                return -EINVAL;
            }
        }

        output_width = fmt->scaler.width;
        output_height = fmt->scaler.height;
    }

    if (fmt->crop.enable) {
        if ( (fmt->crop.height > mscaler_output_res[channel][0])  ||
                (fmt->crop.width > mscaler_output_res[channel][1]) ) {
            printf("%s : coordinate(%d,%d) crop(%d X %d) out of range(%d X %d)\n", data->device_name,
                    fmt->crop.top,  fmt->crop.left, fmt->crop.width, fmt->crop.height,
                    mscaler_output_res[channel][0], mscaler_output_res[channel][1]);
            return -EINVAL;
        }
        output_width = fmt->crop.width;
        output_height = fmt->crop.height;
    }

    if (output_width != fmt->width || output_height != fmt->height) {
        printf("%s : mscaler resulation config error (%d!=%d, %d!=%d)\n",
                data->device_name,output_width, fmt->width, output_height, fmt->height);
        camera_dump_frame_info(fmt);
        return -EINVAL;
    }

    return 0;
}

static int tiziano_mscaler_request_buffer(struct mscaler_channel *data, struct frame_image_format *fmt)
{
    struct frame_image_format *output_fmt = &data->output_fmt;
    int i, ret = 0;

    mutex_lock(&data->lock);

    if (fmt->frame_nums < 1) {
        printf("%s : buffer count too small, fix to 1\n", data->device_name);
        fmt->frame_nums = 1;
    }

    if (is_camera_frame_parameter_valid(data, output_fmt) < 0) {
        printf("%s : frame parameter is invalid\n", data->device_name);
        ret = -EINVAL;
        goto unlock;
    }

    output_fmt->frame_nums = fmt->frame_nums;

    if (data->is_init) {
        for (i = 0; i < data->mem_cnt; i++) {
            unsigned int timeout_cnt = 100;
            struct frame_data *frm = &data->frames[i];
            if (frm->status != frame_status_user)
                continue;

            while(timeout_cnt--) {
                if (frm->status != frame_status_user)
                    break;
                mutex_unlock(&data->lock);
                usleep(1500);
                mutex_lock(&data->lock);
            }

            if (!timeout_cnt) {
                printf("%s : Failed to request buffer\n", data->device_name);
                ret = -EBUSY;
                goto unlock;
            }
        }

        tiziano_mscaler_power_off_nolock(data);

        tiziano_mscaler_free_mem(data);
    }

    //camera_dump_frame_info(output_fmt);

    ret = tiziano_mscaler_alloc_mem(data);

    data->is_init = 1;

unlock:
    mutex_unlock(&data->lock);

    return ret;
}

static int tiziano_mscaler_free_buffer(struct mscaler_channel *data)
{
    int ret;

    mutex_lock(&data->lock);

    if (data->is_init)
        ret = tiziano_mscaler_free_mem(data);

    data->is_init = 0;

    mutex_unlock(&data->lock);

    return ret;
}

__attribute__((__unused__)) static int tiziano_mscaler_set_frame_rate(struct mscaler_channel *data, unsigned char frame_rate)
{
    mutex_lock(&data->lock);

    /* 设置帧率 */
    tiziano_mscaler_frame_rate_control(data, frame_rate);

    mutex_unlock(&data->lock);

    return 0;
}

static int tiziano_mscaler_get_frames_format(struct mscaler_channel *data, struct frame_image_format *fmt)
{
    struct frame_image_format fmt_ = data->output_fmt;

    mutex_lock(&data->lock);

    memcpy(fmt, &fmt_, sizeof(*fmt));

    mutex_unlock(&data->lock);

    return 0;
}

static int tiziano_mscaler_set_frames_format(struct mscaler_channel *data, struct frame_image_format *fmt)
{
    struct frame_image_format *output_fmt = &data->output_fmt;

    mutex_lock(&data->lock);


    if (is_camera_frame_parameter_valid(data, fmt) < 0) {
        char fmt_a = (char)(fmt->pixel_format >> 0);
        char fmt_b = (char)(fmt->pixel_format >> 8);
        char fmt_c = (char)(fmt->pixel_format >> 16);
        char fmt_d = (char)(fmt->pixel_format >> 24);
        printf("%s : frame parameter(%c%c%c%c) is invalid\n", data->device_name, fmt_a, fmt_b, fmt_c, fmt_d);
        return -EINVAL;
    }

    output_fmt->width = fmt->width;
    output_fmt->height = fmt->height;
    output_fmt->pixel_format = fmt->pixel_format;

    output_fmt->crop.enable   = fmt->crop.enable;
    output_fmt->crop.top      = fmt->crop.top;
    output_fmt->crop.left     = fmt->crop.left;
    output_fmt->crop.width    = fmt->crop.width;
    output_fmt->crop.height   = fmt->crop.height;

    output_fmt->scaler.enable = fmt->scaler.enable;
    output_fmt->scaler.width  = fmt->scaler.width;
    output_fmt->scaler.height = fmt->scaler.height;

    output_fmt->frame_nums    = fmt->frame_nums;

//    camera_dump_frame_info(output_fmt);

    mutex_unlock(&data->lock);

    return 0;
}

static void tiziano_mscaler_skip_frames(struct mscaler_channel *data, unsigned int frames)
{
    unsigned long flags;

    spin_lock_irqsave(&data->spinlock, flags);

    while (frames--) {
        struct frame_data *frm = get_usable_frm(data);
        if (frm == NULL)
            break;
        add_to_free_list(data, frm);
    }

    spin_unlock_irqrestore(&data->spinlock, flags);
}

static unsigned int tiziano_mscaler_get_available_frame_count(struct mscaler_channel *data)
{
    return data->frame_counter;
}

static int tiziano_mscaler_check_frame_mem(struct mscaler_channel *data, void *mem)
{
    unsigned int size = mem - data->mem;

    if (mem < data->mem)
        return -1;

    if (size % data->frm_size)
        return -1;

    if (size / data->frm_size >= data->mem_cnt)
        return -1;
    return 0;
}

static inline struct frame_data *tiziano_mscaler_mem_2_frame_data(struct mscaler_channel *data, void *mem)
{
    unsigned int size = mem - data->mem;
    unsigned int count = size / data->frm_size;

    assert(!(size % data->frm_size));
    assert(count < data->mem_cnt);

    return &data->frames[count];;
}

static int tiziano_mscaler_check_frame_info(struct mscaler_channel *data, struct frame_info *info)
{
    struct frame_data *frm = &data->frames[info->index];

    if (info->index >= data->mem_cnt)
        return -1;

    if (frm->info.paddr != info->paddr)
        return -1;

    return 0;
}

static inline struct frame_data *tiziano_mscaler_frame_info_2_frame_data(struct mscaler_channel *data, struct frame_info *info)
{
    if (!tiziano_mscaler_check_frame_info(data, info))
        return &data->frames[info->index];
    else {
        //camera_dump_frame_info(info);
        return NULL;
    }
}

static int tiziano_mscaler_put_frame_node(struct mscaler_channel *data, struct frame_data *frm)
{
    int ret = 0;

    os_enter_critical();

    if (frm->status != frame_status_user) {
        printf("%s : camera double free frame index %d\n", data->device_name, frm->info.index);
        ret = -1;
        goto unlock;
    }

    m_cache_sync(frm->addr, frm->info.size);
    list_del_init(&frm->link);
    add_to_free_list(data, frm);

unlock:
    os_exit_critical();
    return ret;
}

static int tiziano_mscaler_get_frame_node(struct mscaler_channel *data, struct frame_data **frame, unsigned int timeout_ms)
{
    struct frame_data *frm;
    int ret = -EAGAIN;

    os_enter_critical();

    frm = get_usable_frm(data);
    if (!frm && timeout_ms) {
        ret = critical_thread_cond_wait_timeout(&data->cond, timeout_ms);
        if (ret) {
            data->wait_timeout = 1;
            printf("%s : wait frame time out\n", data->device_name);
            goto unlock;
        }

        frm = get_usable_frm(data);
    }

    if (frm) {
        ret = 0;
        frm->status = frame_status_user;
        m_cache_sync(frm->addr, frm->info.size);
        *frame = frm;

        data->chn_output_cnt++;
    }

unlock:
    os_exit_critical();

    return ret;
}

static void *tiziano_mscaler_get_frame(struct mscaler_channel *data)
{
    struct frame_data *frm;
    int ret;

    ret = tiziano_mscaler_get_frame_node(data, &frm, 0);
    if (ret)
        return NULL;

    return frm->info.vaddr;
}

static void *tiziano_mscaler_wait_frame(struct mscaler_channel *data)
{
    struct frame_data *frm = NULL;
    int ret;

    ret = tiziano_mscaler_get_frame_node(data, &frm, 3000);
    if (ret)
        return NULL;

    return frm->info.vaddr;
}

static int tiziano_mscaler_put_frame(struct mscaler_channel *data, void *buf)
{
    struct frame_data *frm;

    if (tiziano_mscaler_check_frame_mem(data, buf)) {
        printf("tiziano_mscaler_check_frame_mem fail\n");
        return -EINVAL;
    }

    frm = tiziano_mscaler_mem_2_frame_data(data, buf);
    tiziano_mscaler_put_frame_node(data, frm);

    return 0;
}

static int tiziano_mscaler_dqbuf(struct mscaler_channel *data, struct frame_info *frame)
{
    struct frame_data *frm;
    int ret;

    assert(frame);

    ret = tiziano_mscaler_get_frame_node(data, &frm, 0);
    if (ret)
        return ret;

    memcpy(frame, &frm->info, sizeof(struct frame_info));

    return 0;
}

static int tiziano_mscaler_dqbuf_wait(struct mscaler_channel *data, struct frame_info *frame)
{
    struct frame_data *frm = NULL;
    int ret;

    assert(frame);

    ret = tiziano_mscaler_get_frame_node(data, &frm, 3000);
    if (ret)
        return ret;

    memcpy(frame, &frm->info, sizeof(struct frame_info));

    return 0;
}


static int tiziano_mscaler_qbuf(struct mscaler_channel *data, struct frame_info *frame)
{
    struct frame_data *frm;

    assert(frame);

    frm = tiziano_mscaler_frame_info_2_frame_data(data, frame);
    if (!frm) {
        printf("tiziano_mscaler_frame_info_2_frame_data fail\n");
        return -EINVAL;
    }
    tiziano_mscaler_put_frame_node(data, frm);

    return 0;
}

static int tiziano_mscaler_stream_off(struct mscaler_channel *data)
{
    mutex_lock(&data->lock);

    tiziano_mscaler_stream_off_nolock(data);

    mutex_unlock(&data->lock);

    return 0;
}

static int tiziano_mscaler_stream_on(struct mscaler_channel *data)
{
    int ret = 0;

    mutex_lock(&data->lock);

    if (!data->is_power_on) {
        printf("%s : camera can't stream on when not power on\n", data->device_name);
        ret = -EINVAL;
        goto unlock;
    }

    ret = tiziano_mscaler_stream_on_nolock(data);

unlock:
    mutex_unlock(&data->lock);

    return ret;
}

static int tiziano_mscaler_power_on(struct mscaler_channel *data)
{
    int ret = 0;

    mutex_lock(&data->lock);

    ret = tiziano_mscaler_power_on_nolock(data);

    mutex_unlock(&data->lock);

    return ret;
}

static void tiziano_mscaler_power_off(struct mscaler_channel *data)
{
    mutex_lock(&data->lock);

    tiziano_mscaler_power_off_nolock(data);

    mutex_unlock(&data->lock);
}

static int tiziano_mscaler_get_sensor_reg(struct mscaler_channel *data, struct sensor_dbg_register *reg)
{
    struct sensor_attr *sensor = data->drv->camera.sensor;
    int ret;

    if (sensor->ops.get_register) {
        ret = sensor->ops.get_register(reg);
        if (ret < 0) {
            printf("%s get_register fail\n", __func__);
            return ret;
        }
    } else {
        printf("sensor->ops.get_register is NULL!\n");
        return -EINVAL;
    }

    return 0;
}

static int tiziano_mscaler_set_sensor_reg(struct mscaler_channel *data, struct sensor_dbg_register *reg)
{
    struct sensor_attr *sensor = data->drv->camera.sensor;
    int ret;

    if (sensor->ops.set_register) {
        ret = sensor->ops.set_register(reg);
        if (ret < 0) {
            printf("%s set_register fail\n", __func__);
            return ret;
        }
    } else {
        printf("sensor->ops.get_register is NULL!\n");
        return -EINVAL;
    }

    return 0;
}


/******************************************************************************
 *
 * 应用调用 函数实现
 *
 *****************************************************************************/
static struct mscaler_channel *hd_to_mscaler(camera_hd_t *camera_hd)
{
    assert(camera_hd);
    struct mscaler_channel *data = container_of(camera_hd, struct mscaler_channel, hd);
    int index = data->index;
    int channel = data->channel;

    if (index < 0 || index > 1) {
        printf("mscaler index(%d) out of range[0~1]\n", index);
        goto error;
    }

    if (channel < 0 || channel > 2) {
        printf("mscaler channel(%d) out of range[0~2]\n", channel);
        goto error;
    }

    if (data == &jz_mscaler_dev[index].mscaler_ch[channel])
        return data;

error:
    panic("mscaler camera handler is invalid. please check parameter\n");
}

camera_hd_t *soc_mscaler_detect(int index, int channel)
{
    assert(index < 2);
    assert(channel < MSCALER_MAX_CH);

    struct mscaler_data *drv = &jz_mscaler_dev[index];
    struct mscaler_channel *data = &drv->mscaler_ch[channel];

    if (!drv->camera.sensor) {
        printf("mscaler%d: no sensor regisered\n", index);
        return NULL;
    }

    if (drv->camera.is_power_on) {
        return &data->hd;
    }

    if (!tiziano_mscaler_power_on(data)) {
        data->chn_done_cnt = 0;
        data->chn_output_cnt = 0;
    }

    return &data->hd;
}

void soc_mscaler_release(camera_hd_t *camera_hd)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    tiziano_mscaler_power_off(data);
}

struct camera_info *soc_mscaler_get_info(camera_hd_t *camera_hd)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    return &data->info;
}

struct camera_info *soc_mscaler_get_sensor_info(camera_hd_t *camera_hd)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);
    struct mscaler_data *drv = data->drv;

    return &drv->camera.sensor->info;
}

int soc_mscaler_power_on(camera_hd_t *camera_hd)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    return tiziano_mscaler_power_on(data);
}

void soc_mscaler_power_off(camera_hd_t *camera_hd)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    tiziano_mscaler_power_off(data);
}

int soc_mscaler_stream_on(camera_hd_t *camera_hd)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    return tiziano_mscaler_stream_on(data);
}

void soc_mscaler_stream_off(camera_hd_t *camera_hd)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    tiziano_mscaler_stream_off(data);
}

camera_frame_error_type soc_mscaler_get_frame_error(camera_hd_t *camera_hd)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    if (data->wait_timeout)
        return camera_error_timeout;

    if (!data->is_stream_on)
        return camera_error_stream_is_off;

    return camera_error_null;
}

void *soc_mscaler_get_frame(camera_hd_t *camera_hd)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    return tiziano_mscaler_get_frame(data);
}

void *soc_mscaler_wait_frame(camera_hd_t *camera_hd)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    return tiziano_mscaler_wait_frame(data);
}

int soc_mscaler_put_frame(camera_hd_t *camera_hd, void *buf)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    return tiziano_mscaler_put_frame(data, buf);
}

int soc_mscaler_dqbuf(camera_hd_t *camera_hd, struct frame_info *frame)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    return tiziano_mscaler_dqbuf(data, frame);
}

int soc_mscaler_dqbuf_wait(camera_hd_t *camera_hd, struct frame_info *frame)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    return tiziano_mscaler_dqbuf_wait(data, frame);
}

int soc_mscaler_qbuf(camera_hd_t *camera_hd, struct frame_info *frame)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    return tiziano_mscaler_qbuf(data, frame);
}

unsigned int soc_mscaler_get_available_frame_count(camera_hd_t *camera_hd)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    return tiziano_mscaler_get_available_frame_count(data);
}

void soc_mscaler_skip_frames(camera_hd_t *camera_hd, unsigned int frames)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    tiziano_mscaler_skip_frames(data, frames);
}

int soc_mscaler_get_max_scaler_size(camera_hd_t *camera_hd, int *width, int *height)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    *width = mscaler_output_res[data->channel][0];
    *height = mscaler_output_res[data->channel][1];

    return 0;
}

int soc_mscaler_get_line_align_size(camera_hd_t *camera_hd, int *align_size)
{
    *align_size = MSCALER_ALIGN_SIZE;

    return 0;
}

int soc_mscaler_set_format(camera_hd_t *camera_hd, struct frame_image_format *fmt)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    return tiziano_mscaler_set_frames_format(data, fmt);
}

int soc_mscaler_get_format(camera_hd_t *camera_hd, struct frame_image_format *fmt)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    return tiziano_mscaler_get_frames_format(data, fmt);
}

int soc_mscaler_request_buffer(camera_hd_t *camera_hd, struct frame_image_format *fmt)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    return tiziano_mscaler_request_buffer(data, fmt);
}

int soc_mscaler_free_buffer(camera_hd_t *camera_hd)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    return tiziano_mscaler_free_buffer(data);
}

int soc_mscaler_get_sensor_reg(camera_hd_t *camera_hd, struct sensor_dbg_register *reg)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    return tiziano_mscaler_get_sensor_reg(data, reg);
}

int soc_mscaler_set_sensor_reg(camera_hd_t *camera_hd, struct sensor_dbg_register *reg)
{
    struct mscaler_channel *data = hd_to_mscaler(camera_hd);

    return tiziano_mscaler_set_sensor_reg(data, reg);
}

#define error_if(_cond)                                         \
    do {                                                        \
        if (_cond) {                                            \
            printf("mscaler: failed to check: %s\n", #_cond);   \
            ret = -1;                                           \
            goto unlock;                                        \
        }                                                       \
    } while (0)

int tiziano_mscaler_register_sensor_routine(int index, struct sensor_attr *sensor)
{
    struct mscaler_data *drv = &jz_mscaler_dev[index];
    int ret = 0;

    assert(drv->is_finish > 0);
    assert(!drv->camera.sensor);

    mutex_lock(&drv->lock);

    error_if (drv->camera.sensor);
    error_if(sensor->sensor_info.width < 8 || sensor->sensor_info.width > 2048);
    error_if(sensor->sensor_info.height < 8 || sensor->sensor_info.height > 2048);

    drv->camera.sensor = sensor;
    drv->camera.is_power_on = 0;
    drv->camera.is_stream_on = 0;

unlock:
    mutex_unlock(&drv->lock);

    return ret;
}

void tiziano_mscaler_unregister_sensor_routine(int index, struct sensor_attr *sensor)
{
    struct mscaler_data *drv = &jz_mscaler_dev[index];

    assert(drv->is_finish > 0);
    assert(drv->camera.sensor);
    assert(sensor == drv->camera.sensor);

    mutex_lock(&drv->lock);

    if (drv->camera.is_stream_on)
        panic("mscaler%d : failed to unregister, when camera stream on!\n", index);

    if (drv->camera.is_power_on) {
        tiziano_isp_power_off(drv->index);
        drv->camera.is_power_on = 0;
    }

    drv->camera.sensor = NULL;

    mutex_unlock(&drv->lock);
}

int jz_mscaler_drv_init(int index)
{
    struct mscaler_data *drv = &jz_mscaler_dev[index];

    mutex_init(&drv->lock);

    int ch;
    for(ch = 0; ch < MSCALER_MAX_CH; ch++) {
        drv->mscaler_ch[ch].index = index;
        drv->mscaler_ch[ch].channel = ch;
        drv->mscaler_ch[ch].drv = drv;

        critical_thread_cond_init(&drv->mscaler_ch[ch].cond);

        mutex_init(&drv->mscaler_ch[ch].lock);

        spin_lock_init(&drv->mscaler_ch[ch].spinlock);

        memset(drv->mscaler_ch[ch].device_name, 0x00, sizeof(drv->mscaler_ch[ch].device_name));
        sprintf(drv->mscaler_ch[ch].device_name, "%s-ch%d", drv->mscaler_name, ch);
        drv->mscaler_ch[ch].hd.ptr = (void *)&drv->mscaler_ch[ch].device_name;
    }

    drv->is_finish = 1;

    //printf("mscaler%d initialization successfully\n", index);
    return 0;
}

void jz_mscaler_drv_deinit(int index)
{
    struct mscaler_data *drv = &jz_mscaler_dev[index];

    if (!drv->is_finish)
        return ;

    drv->is_finish = 0;
    /* Nothing TODO */
    return ;
}
