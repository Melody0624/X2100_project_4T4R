#include <common.h>
#include <os.h>
#include <driver/irq.h>
#include <driver/clk.h>
#include <driver/cache.h>
#include <bit_field.h>
#include <errno.h>

#include "rotator_regs.h"
#include <driver/rotator.h>

#define MIN_WIDTH   4
#define MIN_HEIGHT  4
#define MAX_WIDTH   2047
#define MAX_HEIGHT  2047

#define ROTATOR_TIMEOUT 400


struct frame_descriptor
{
    unsigned long next_des_addr;
    unsigned long src_buffer_addr;
    unsigned long src_stride;
    unsigned long frame_stop;
    unsigned long dst_buffer_addr;
    unsigned long dst_stride;
    unsigned long interrupt_control;
}__attribute__ ((aligned(8)));

struct rotator_device
{
    struct clk *clk;
    critical_thread_cond_t cond;
    int irq_is_request;

    struct frame_descriptor *desc;
};

static struct rotator_device rotator_dev;
/***************************************/
#define ROTATOR_IOBASE      0x13070000
#define ROTATOR_REG_BASE    KSEG1ADDR(ROTATOR_IOBASE)
#define ROTATOR_ADDR(reg)   ((volatile unsigned long *)(ROTATOR_REG_BASE + reg))

static inline void rotator_write_reg(unsigned int reg, unsigned int value)
{
    *ROTATOR_ADDR(reg) = value;
}

static inline unsigned int rotator_read_reg(unsigned int reg)
{
    return *ROTATOR_ADDR(reg);
}

static inline void rotator_set_bits(unsigned int reg, int start, int end, unsigned int val)
{
    set_bit_field_v(ROTATOR_ADDR(reg), start, end, val);
}

static inline unsigned int rotator_get_bits(unsigned int reg, int start, int end)
{
    return get_bit_field_v(ROTATOR_ADDR(reg), start, end);
}


/***************************************/
int rotator_bytes_per_pixel(enum rotator_fmt fmt)
{
    if (fmt == ROTATOR_RGB888 || fmt == ROTATOR_ARGB8888)
        return 4;
    if (fmt == ROTATOR_Y8)
        return 1;
    return 2;
}

static void rotator_init_desc(struct frame_descriptor *desc, struct rotator_config_data *config)
{
    memset(desc, 0, sizeof(struct frame_descriptor));

    desc->frame_stop = 1;

    set_bit_field_v(&desc->interrupt_control, f_EOF_MASK, 1);
    set_bit_field_v(&desc->interrupt_control, f_SOF_MASK, 1);

    desc->next_des_addr = virt_to_phys(desc);

    desc->src_buffer_addr = virt_to_phys(config->src_buf);
    desc->src_stride = config->src_stride;

    desc->dst_buffer_addr = virt_to_phys(config->dst_buf);
    desc->dst_stride = config->dst_stride;

    flush_dcache((unsigned long)desc, ALIGN(sizeof(struct frame_descriptor), cache_line_size()));
}

static void rotator_enable(void)
{
    rotator_set_bits(ROTATOR_INT_MASK, ROTATORINTMASK_EOF_MASK, 1);
    rotator_set_bits(ROTATOR_CTRL, ROTATORCTRL_START, 1);
}

static void rotator_config(struct frame_descriptor *desc, struct rotator_config_data *config)
{
    static const unsigned char src_fmt[] = {
        [ROTATOR_RGB555]     = 0,
        [ROTATOR_ARGB1555]   = 1,
        [ROTATOR_RGB565]     = 2,
        [ROTATOR_RGB888]     = 4,
        [ROTATOR_ARGB8888]   = 5,
        [ROTATOR_Y8]         = 6,
        [ROTATOR_YUV422]     = 10,
    };

    static const unsigned char dst_fmt[] = {
        [ROTATOR_ARGB8888]   = 0,
        [ROTATOR_RGB565]     = 1,
        [ROTATOR_RGB555]     = 2,
        [ROTATOR_YUV422]     = 3,
        [ROTATOR_Y8]         = 4,
    };

    rotator_set_bits(ROTATOR_QOS_CFG, ROTATORQOSCFG_STD_CLK, 13);
    rotator_set_bits(ROTATOR_QOS_CTRL, ROTATORQOSCTRL_FIX_EN, 1);
    rotator_set_bits(ROTATOR_GLB_CFG, ROTATORGLBCFG_WDMA_BURST_LEN, 3);
    rotator_set_bits(ROTATOR_GLB_CFG, ROTATORGLBCFG_RDMA_BURST_LEN, 3);

    rotator_write_reg(ROTATOR_FRM_CFG_ADDR, virt_to_phys(desc));

    rotator_set_bits(ROTATOR_FRM_SIZE, ROTATORFRMSIZE_FRAM_HEIGHT, config->frame_height);
    rotator_set_bits(ROTATOR_FRM_SIZE, ROTATORFRMSIZE_FRAM_WIDTH, config->frame_width);

    rotator_set_bits(ROTATOR_GLB_CFG, ROTATORGLBCFG_RDMA_FMT, src_fmt[config->src_fmt]);
    rotator_set_bits(ROTATOR_GLB_CFG, ROTATORGLBCFG_RDMA_ORDER, config->convert_order);
    rotator_set_bits(ROTATOR_GLB_CFG, ROTATORGLBCFG_WDMA_FMT, dst_fmt[config->dst_fmt]);

    rotator_set_bits(ROTATOR_GLB_CFG, ROTATORGLBCFG_ROT_ANGLE, config->rotate_angle);
    rotator_set_bits(ROTATOR_GLB_CFG, ROTATORGLBCFG_VERTICAL_MIRROR, config->vertical_mirror);
    rotator_set_bits(ROTATOR_GLB_CFG, ROTATORGLBCFG_HORIZONTAL_MIRROR, config->horizontal_mirror);
}

static int rotator_check_src_fmt_byte(struct rotator_config_data *config)
{
    switch (config->src_fmt) {
        case ROTATOR_RGB555:
            return 2;
        case ROTATOR_ARGB1555:
            return 2;
        case ROTATOR_RGB565:
            return 2;
        case ROTATOR_RGB888:
            return 3;
        case ROTATOR_ARGB8888:
            return 4;
        case ROTATOR_Y8:
            return 1;
        case ROTATOR_YUV422:
            return 2;
        default:
            break;
    }

    return 0;
}

static int rotator_check_dst_fmt_byte(struct rotator_config_data *config)
{
    switch (config->dst_fmt) {
        case ROTATOR_ARGB8888:
            return 4;
        case ROTATOR_RGB565:
            return 2;
        case ROTATOR_RGB555:
            return 2;
        case ROTATOR_YUV422:
            return 2;
        case ROTATOR_Y8:
            return 1;
        default:
            break;
    }

    return 0;
}

static int rotator_check_params(struct rotator_config_data *config)
{
    int byte;

    if (config->frame_height > MAX_HEIGHT || config->frame_height < MIN_HEIGHT) {
        printf("rotator frame_height %d not in range 4~2047\n", config->frame_height);
        return -EINVAL;
    }

    if (config->frame_width > MAX_WIDTH || config->frame_width < MIN_WIDTH) {
        printf("rotator frame_width %d not in range 4~2047\n", config->frame_width);
        return -EINVAL;
    }

    byte = rotator_check_src_fmt_byte(config);
    if (!byte) {
        printf("rotator source fmt %d no support!\n", config->src_fmt);
        return -EINVAL;
    }
    flush_dcache_force((unsigned long)config->src_buf, config->frame_height * config->frame_width * byte);

    byte = rotator_check_dst_fmt_byte(config);
    if (!byte) {
        printf("rotator destin fmt %d no support!\n", config->dst_fmt);
        return -EINVAL;
    }
    invalidate_dcache_force((unsigned long)config->dst_buf, config->frame_height * config->frame_width * byte);

    return 0;
}

static void rotator_irq_handler(int irq, void *data);
int rotator_complete_conversion(struct rotator_config_data *config)
{
    int ret = 0;
    struct frame_descriptor *desc;

    ret = rotator_check_params(config);
    if (ret < 0) {
        printf("rotator params error!\n");
        return -EINVAL;
    }

    os_enter_critical();

    desc = rotator_dev.desc;

    rotator_init_desc(desc, config);

    rotator_config(desc, config);

    if (!rotator_dev.irq_is_request) {
        request_irq(IRQ_ROTATE, 0, rotator_irq_handler, "rotator", &rotator_dev);
        rotator_dev.irq_is_request = 1;
    }

    rotator_enable();

    ret = critical_thread_cond_wait_timeout(&rotator_dev.cond, ROTATOR_TIMEOUT);
    if (ret)
        printf("rotator convert timeout!\n");

    os_exit_critical();

    return (ret == 0) ? 0 : -ETIMEDOUT;
}

static void rotator_irq_handler(int irq, void *data)
{
    if (rotator_get_bits(ROTATOR_STATUS, ROTATORSTATUS_EOF)) {
        rotator_set_bits(ROTATOR_CLR_STATUS, ROTATORCLRSTATUS_CLR_EOF, 1);
        critical_thread_cond_signal(&rotator_dev.cond);
    }

    if (rotator_get_bits(ROTATOR_STATUS, ROTATORSTATUS_GEN_STOP_ACK)) {
        rotator_set_bits(ROTATOR_CLR_STATUS, ROTATORCLRSTATUS_CLR_GEN_STP_ACK, 1);
    }

    return;
}


int soc_rotator_complete_conversion(struct rotator_config_data *data)
{
    int ret = 0;

    if (data->src_fmt == ROTATOR_NV12 && data->dst_fmt == ROTATOR_NV12) {
        struct rotator_config_data uv_rotator_config;
        int y_pixel_byte = 1;
        int uv_pixel_byte = 2;
        int src_height = data->frame_height;
        int dst_height = data->frame_width;

        if (data->rotate_angle == ROTATOR_ANGLE_0 || data->rotate_angle == ROTATOR_ANGLE_180)
            dst_height = src_height;

        memcpy(&uv_rotator_config, data, sizeof(uv_rotator_config));
        uv_rotator_config.src_fmt              = ROTATOR_RGB565;
        uv_rotator_config.dst_fmt              = ROTATOR_RGB565;
        uv_rotator_config.src_buf              = data->src_buf + data->src_stride * src_height * y_pixel_byte;
        uv_rotator_config.dst_buf              = data->dst_buf + data->dst_stride * dst_height * y_pixel_byte;
        uv_rotator_config.frame_width          = data->frame_width / 2;
        uv_rotator_config.frame_height         = data->frame_height / 2;
        uv_rotator_config.src_stride           = data->src_stride / uv_pixel_byte;
        uv_rotator_config.dst_stride           = data->dst_stride / uv_pixel_byte;

        ret = rotator_complete_conversion(&uv_rotator_config);
        if (ret < 0) {
            printf("rotator converrion failed\n");
            return ret;
        }

        data->src_fmt = ROTATOR_Y8;
        data->dst_fmt = ROTATOR_Y8;

        ret = rotator_complete_conversion(data);
        if (ret < 0)
            printf("rotator converrion failed\n");

        data->src_fmt = ROTATOR_NV12;
        data->dst_fmt = ROTATOR_NV12;

        return ret;
    }

    ret = rotator_complete_conversion(data);
    if (ret < 0)
        printf("rotator converrion failed\n");

    return ret;
}

int soc_rotator_init(void)
{
    critical_thread_cond_init(&rotator_dev.cond);

    rotator_dev.clk = clk_get("gate_rot");
    if (IS_ERR(rotator_dev.clk)) {
        printf("get rotator clk fail!\n");
        return -1;
    }

    clk_enable(rotator_dev.clk);

    rotator_dev.irq_is_request = 0;
    rotator_dev.desc = cache_align_malloc(sizeof(struct frame_descriptor));
    assert(rotator_dev.desc);

    return 0;
}

__attribute__((__unused__)) void soc_rotator_exit(void)
{
    free((void *)rotator_dev.desc);
    if (rotator_dev.irq_is_request == 1) {
        release_irq(IRQ_ROTATE);
    }
    clk_disable(rotator_dev.clk);
    clk_put(rotator_dev.clk);
}
