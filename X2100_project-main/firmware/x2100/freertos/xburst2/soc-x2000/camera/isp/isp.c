/*
 * Copyright (C) 2020 Ingenic Semiconductor Co., Ltd.
 *
 * ISP Driver
 */

#include <errno.h>
#include <common.h>
#include <bit_field.h>
#include <driver/irq.h>
#include <os.h>


#include "isp-core/inc/tiziano_core.h"
#include "isp-core/inc/tiziano_isp.h"
#include "isp-core/inc/tiziano_sys.h"
#include "../hal/camera_cpm.h"
#include "../hal/vic.h"
#include "../version_log.h"
#include "isp_regs.h"
#include "isp.h"
#include "mscaler.h"

#include "vic_channel_tiziano.h"
#include "ddrc.c"


static struct jz_isp_data jz_isp_dev[2] = {
    {
        .index                  = 0,
        .irq                    = IRQ_ISP0, /* BASE + 21 */
        .irq_name               = "ISP0",
        .device_name            = "isp0",
    },

    {
        .index                  = 1,
        .irq                    = IRQ_ISP1, /* BASE + 20 */
        .irq_name               = "ISP1",
        .device_name            = "isp1",
    },
};


/*
 * ISP Operation
 */
static const unsigned long isp_iobase[] = {
        KSEG1ADDR(ISP0_IOBASE),
        KSEG1ADDR(ISP1_IOBASE),
};

#define ISP_ADDR(index, reg)            ((volatile unsigned long *)((isp_iobase[index]) + (reg)))

static inline void isp_write_reg(int index, unsigned int reg, unsigned int val)
{
    *ISP_ADDR(index, reg) = val;
}

static inline unsigned int isp_read_reg(int index, unsigned int reg)
{
    return *ISP_ADDR(index, reg);
}

static inline void isp_set_bit(int index, unsigned int reg, unsigned int start, unsigned int end, unsigned int val)
{
    set_bit_field_v(ISP_ADDR(index, reg), start, end, val);
}

static inline unsigned int isp_get_bit(int index, unsigned int reg, unsigned int start, unsigned int end)
{
    return get_bit_field_v(ISP_ADDR(index, reg), start, end);
}


/*
 * interface used by isp-core
 */
int system_reg_write(void *isp, unsigned int reg, unsigned int value)
{
    struct jz_isp_data *drv = (struct jz_isp_data *)isp;

    isp_write_reg(drv->index, reg, value);

    return 0;
}

unsigned int system_reg_read(void *isp, unsigned int reg)
{
    struct jz_isp_data *drv = (struct jz_isp_data *)isp;

    return isp_read_reg(drv->index, reg);
}

int system_irq_func_set(void *hdl, int irq, void *func, void *data)
{
    struct jz_isp_data *drv = (struct jz_isp_data *)hdl;

    drv->irq_func_cb[irq] = func;
    drv->irq_func_data[irq] = data;

    return 0;
}

/*
 * ISP Base Interface
 */
static inline void tiziano_isp_dump_reg(int index)
{
    printf("==========dump isp%d register============\n", index);

    printf("TOP_CTRL_ADDR_VERSION            :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_VERSION            ));
    printf("TOP_CTRL_ADDR_FM_SIZE            :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_FM_SIZE            ));
    printf("TOP_CTRL_ADDR_BAYER_TYPE         :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_BAYER_TYPE         ));
    printf("TOP_CTRL_ADDR_BYPASS_CON         :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_BYPASS_CON         ));
    printf("TOP_CTRL_ADDR_TOP_CON            :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_TOP_CON            ));
    printf("TOP_CTRL_ADDR_TOP_STATE          :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_TOP_STATE          ));
    printf("TOP_CTRL_ADDR_LINE_SPACE         :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_LINE_SPACE         ));
    printf("TOP_CTRL_ADDR_REG_CON            :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_REG_CON            ));
    printf("TOP_CTRL_ADDR_DMA_RC_TRIG        :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_DMA_RC_TRIG        ));
    printf("TOP_CTRL_ADDR_DMA_RC_CON         :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_DMA_RC_CON         ));
    printf("TOP_CTRL_ADDR_DMA_RC_ADDR        :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_DMA_RC_ADDR        ));
    printf("TOP_CTRL_ADDR_DMA_RC_STATE       :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_DMA_RC_STATE       ));
    printf("TOP_CTRL_ADDR_DMA_RC_APB_WR_DATA :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_DMA_RC_APB_WR_DATA ));
    printf("TOP_CTRL_ADDR_DMA_RC_APB_WR_ADDR :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_DMA_RC_APB_WR_ADDR ));
    printf("TOP_CTRL_ADDR_DMA_RD_CON         :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_DMA_RD_CON         ));
    printf("TOP_CTRL_ADDR_DMA_WR_CON         :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_DMA_WR_CON         ));
    printf("TOP_CTRL_ADDR_DMA_FR_WR_CON      :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_DMA_FR_WR_CON      ));
    printf("TOP_CTRL_ADDR_DMA_STA_WR_CON     :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_DMA_STA_WR_CON     ));
    printf("TOP_CTRL_ADDR_DMA_RD_DEBUG       :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_DMA_RD_DEBUG       ));
    printf("TOP_CTRL_ADDR_DMA_WR_DEBUG       :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_DMA_WR_DEBUG       ));
    printf("TOP_CTRL_ADDR_DMA_FR_WR_DEBUG    :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_DMA_FR_WR_DEBUG    ));
    printf("TOP_CTRL_ADDR_DMA_STA_WR_DEBUG   :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_DMA_STA_WR_DEBUG   ));
    printf("TOP_CTRL_ADDR_INT_EN             :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_INT_EN             ));
    printf("TOP_CTRL_ADDR_INT_REG            :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_INT_REG            ));
    printf("TOP_CTRL_ADDR_INT_CLR            :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_INT_CLR            ));
    printf("TOP_CTRL_ADDR_TP_FREERUN         :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_TP_FREERUN         ));
    printf("TOP_CTRL_ADDR_TP_CON             :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_TP_CON             ));
    printf("TOP_CTRL_ADDR_TP_SIZE            :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_TP_SIZE            ));
    printf("TOP_CTRL_ADDR_TP_FONT            :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_TP_FONT            ));
    printf("TOP_CTRL_ADDR_TP_FLICK           :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_TP_FLICK           ));
    printf("TOP_CTRL_ADDR_TP_CS_TYPE         :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_TP_CS_TYPE         ));
    printf("TOP_CTRL_ADDR_TP_CS_FCLO         :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_TP_CS_FCLO         ));
    printf("TOP_CTRL_ADDR_TP_CS_BCLO         :0x%08x\n", isp_read_reg(index, TOP_CTRL_ADDR_TP_CS_BCLO         ));
}

/*
 * ISP总线复位后mscaler,isp相关模块均被复位
 */
static int tiziano_module_soft_reset(int index)
{
    int timeout = 0xffffff;
    struct jz_isp_data *drv = &jz_isp_dev[index];

    if (drv->camera.is_power_on != 0) /* 同一ISP的不同通道只需reset一次 */
        return 0;

    switch (index) {
    case 0: {   /* ISP0 */
        /* stop request */
        cpm_set_bit(CPM_SRBC, CPM_SRBC_ISP0_STOP_TRANSFER, 1);
        while ( !cpm_get_bit(CPM_SRBC, CPM_SRBC_ISP0_STOP_ACK) && --timeout );
        if (timeout == 0) {
            printf("isp%d wait stop timeout\n", index);
            return  -ETIMEDOUT;
        }

        /* reset */
        unsigned long isp0_reset = cpm_read_reg(CPM_SRBC);

        set_bit_field(&isp0_reset, CPM_SRBC_ISP0_STOP_TRANSFER, 0);
        set_bit_field(&isp0_reset, CPM_SRBC_ISP0_SOFT_RESET, 1);
        cpm_write_reg(CPM_SRBC, isp0_reset);

        udelay(10);

        isp0_reset = cpm_read_reg(CPM_SRBC);
        set_bit_field(&isp0_reset, CPM_SRBC_ISP0_SOFT_RESET, 0);
        cpm_write_reg(CPM_SRBC, isp0_reset);

        break;
    }

    case 1: {   /* ISP1 */
        /* stop request */
        cpm_set_bit(CPM_SRBC, CPM_SRBC_ISP1_STOP_TRANSFER, 1);
        while ( !cpm_get_bit(CPM_SRBC, CPM_SRBC_ISP1_STOP_ACK) && --timeout );
        if (timeout == 0) {
            printf("isp%d wait stop timeout\n", index);
            return  -ETIMEDOUT;
        }

        /* reset */
        unsigned long isp1_reset = cpm_read_reg(CPM_SRBC);

        set_bit_field(&isp1_reset, CPM_SRBC_ISP1_STOP_TRANSFER, 0);
        set_bit_field(&isp1_reset, CPM_SRBC_ISP1_SOFT_RESET, 1);
        cpm_write_reg(CPM_SRBC, isp1_reset);

        udelay(10);

        isp1_reset = cpm_read_reg(CPM_SRBC);
        set_bit_field(&isp1_reset, CPM_SRBC_ISP1_SOFT_RESET, 0);
        cpm_write_reg(CPM_SRBC, isp1_reset);
        break;
    }

    default:
        printf("isp%d out of range\n", index);
        break;
    }

    return 0;
}

static void tiziano_isp_firmware_process(void *data)
{
    struct jz_isp_data *drv = (struct jz_isp_data *)data;

    while(1){
        tisp_fw_process(drv->core);
    }
}

int tiziano_isp_stream_on(int index, struct sensor_attr *attr)
{
    struct jz_isp_data *drv = &jz_isp_dev[index];
    int ret;

    /*
     * ISP firmware parameter
     */
    tisp_init_param_t iparam;
    int sensor_data_fmt = attr->sensor_info.fmt;

    /* 添加不同格式的实现 */
    switch(sensor_data_fmt) {
    case SENSOR_PIXEL_FMT_SBGGR8_1X8:
    case SENSOR_PIXEL_FMT_SBGGR10_1X10:
    case SENSOR_PIXEL_FMT_SBGGR12_1X12:
        iparam.bayer = 2;   /* BGGR */
        break;

    case SENSOR_PIXEL_FMT_SGBRG8_1X8:
    case SENSOR_PIXEL_FMT_SGBRG10_1X10:
    case SENSOR_PIXEL_FMT_SGBRG12_1X12:
        iparam.bayer = 3;   /* GBRG */
        break;

    case SENSOR_PIXEL_FMT_SGRBG8_1X8:
    case SENSOR_PIXEL_FMT_SGRBG10_1X10:
    case SENSOR_PIXEL_FMT_SGRBG12_1X12:
        iparam.bayer = 1;   /* GRBG */
        break;

    case SENSOR_PIXEL_FMT_SRGGB8_1X8:
    case SENSOR_PIXEL_FMT_SRGGB10_1X10:
    case SENSOR_PIXEL_FMT_SRGGB12_1X12:
        iparam.bayer = 0;   /* RGGB */
        break;

    default:
        printf("%s[%d] the format(0x%08x) of input couldn't be handled!\n",
                __func__,__LINE__, sensor_data_fmt);
        return -EINVAL;
    }

    if ( (attr->dbus_type == SENSOR_DATA_BUS_MIPI) && (attr->mipi.mipi_crop.enable) ) {
        iparam.width  = attr->mipi.mipi_crop.output_width;
        iparam.height = attr->mipi.mipi_crop.output_height;
    } else {
        iparam.width  = attr->sensor_info.width;
        iparam.height = attr->sensor_info.height;
    }

    strncpy(iparam.sensor, attr->device_name, sizeof(iparam.sensor));
    iparam.isp_bin_data = attr->isp_bin_data;

    tisp_core_init(drv->core, &iparam, drv);

    /*
     * For event engine
     */
    drv->process_thread = thread_create("isp_firmware_process", 8192, tiziano_isp_firmware_process, drv);
    assert(drv->process_thread);
    thread_set_priority(drv->process_thread, OS_priority_high);

    tiziano_isp_tuning_activate(drv->tuning);

    isp_write_reg(index, TOP_CTRL_ADDR_INT_EN, 0x81f03ff);
    enable_irq(drv->irq);

    ret = vic_tiziano_stream_on(index, attr);  /* VIC Interface Stream ON */
    if (ret) {
        printf("%s[%d] vic tiziano stream on failed!\n",__func__,__LINE__);
        ret = -EINVAL;
        goto tiziano_stream_on_failed;
    }

    drv->camera.is_stream_on = 1;

    return 0;

tiziano_stream_on_failed:
    disable_irq(drv->irq);
    tiziano_isp_tuning_slake(drv->tuning);
    thread_delete(drv->process_thread);

    tisp_core_deinit(drv->core);
    return ret;
}

int tiziano_isp_stream_off(int index, struct sensor_attr *attr)
{
    struct jz_isp_data *drv = &jz_isp_dev[index];

    vic_tiziano_stream_off(index, attr);

    disable_irq(drv->irq);

    /* disable irq */
    isp_write_reg(index, TOP_CTRL_ADDR_INT_EN, 0);

    /* isp tirger */
    isp_write_reg(index, INPUT_CTRL_ADDR_IP_TRIG, 0x0);

    thread_delete(drv->process_thread);

    tiziano_isp_tuning_slake(drv->tuning);

    tisp_core_deinit(drv->core);

    drv->camera.is_stream_on = 0;

    return 0;
}

int tiziano_isp_power_on(int index)
{
    struct jz_isp_data *drv = &jz_isp_dev[index];
    int ret = 0;

    mutex_lock(&drv->lock);

    if (drv->camera.is_power_on == 0) {
        ret = vic_tiziano_power_on(index);
        tiziano_module_soft_reset(index);
    }

    if (!ret)
        drv->camera.is_power_on++;

    mutex_unlock(&drv->lock);

    return ret;
}

void tiziano_isp_power_off(int index)
{
    struct jz_isp_data *drv = &jz_isp_dev[index];

    mutex_lock(&drv->lock);

    vic_tiziano_power_off(index);
    drv->camera.is_power_on--;

    mutex_unlock(&drv->lock);
}

static void tiziano_isp_irq_handler(int irq, void *data)
{
    struct jz_isp_data *drv = (struct jz_isp_data *)data;
    int index = drv->index;
    unsigned int irqstatus;
    unsigned int ispstatus;
    int ret;
    int retry = 0;

    irqstatus = isp_read_reg(index, TOP_CTRL_ADDR_INT_REG);
    isp_write_reg(index, TOP_CTRL_ADDR_INT_CLR, irqstatus);
#if 0
    unsigned int irqen;
    int retry = 0;
    irqen = isp_read_reg(index, TOP_CTRL_ADDR_INT_EN);
    isp_write_reg(index, TOP_CTRL_ADDR_INT_EN, irqen & (~irqstatus));

    //err cnt
    if (irqstatus & 0x3f8){
        printf("ispcore: irq-status %08x\n",irqstatus);
        drv->isp_err++;
    }
    if (irqstatus & 0xf8){
        drv->isp_err1++;
    }
    if (irqstatus & 0x200){
        drv->isp_overflow++;
    }
    if (irqstatus & 0x100){
        drv->isp_breakfrm++;
    }
#endif

    //breakfrm & overflow
    if (irqstatus & 0x300){
        printf("%s breakfrm 0x%08x\n", __func__, irqstatus);
        ispstatus = isp_read_reg(index, TOP_CTRL_ADDR_TOP_CON);
        isp_write_reg(index, TOP_CTRL_ADDR_TOP_CON, ispstatus | 0x2);//[1]:1
        while(retry < 60){
            ret = isp_read_reg(index, TOP_CTRL_ADDR_TOP_STATE);
            if(ret == 1)
                break;
            retry++;
        }
        if(retry < 60){
            //reset isp
            isp_write_reg(index, TOP_CTRL_ADDR_TOP_CON, ((ispstatus & 0xfffffffd) | 0x10));//[1]:0 [4]:1
            isp_write_reg(index, TOP_CTRL_ADDR_TOP_CON, (ispstatus & 0xffffffed));//[4]:0
            isp_write_reg(index, TOP_CTRL_ADDR_INT_CLR, 0x300);
            isp_write_reg(index, 0x100, 0x1);
        } else {
            printf("retry failed!!!!\n");
        }
    }

    //frmdone isp set
    if (irqstatus & 0x07) {
        if (1 == drv->daynight_change) {
            drv->daynight_change = 0;
            tisp_day_or_night_s_ctrl(&drv->core->core_tuning, drv->dn_state);
        }
        if(1 == drv->hflip_change){
            drv->hflip_change = 0;
            tisp_mirror_enable(&drv->core->core_tuning, drv->hflip_state);
        }
        if(1 == drv->vflip_change){
            drv->vflip_change = 0;
            tisp_flip_enable(&drv->core->core_tuning, drv->vflip_state);
        }
    }

    /*
     * 1. mscaler irq callback
     */
    ret = tiziano_mscaler_interrupt_service_routine(index, irqstatus);
    if(ret < 0)
        printf("mscaler interrupt handle error. ret=%d\n", ret);

    /*
     * 2. isp-core irq callbacks
     */
    int i;
    for (i = 0; i < 32; i++) {
        if ( (irqstatus & (1 << i)) && drv->irq_func_cb[i]) {
            ret = drv->irq_func_cb[i](drv->irq_func_data[i]);
            if (IRQ_WAKE_THREAD == ret) {
                thread_waiter_wakeup(&drv->irq_wakeup_waiter);
            }
        }
    }

#if 0
    isp_write_reg(index, TOP_CTRL_ADDR_INT_EN, irqen);
#endif
}


static void tiziano_isp_irq_thread_handler(void *data)
{
    struct jz_isp_data *isp = (struct jz_isp_data *)data;
    int i;

    if(!isp)
        return;

    while (1) {
        thread_waiter_wait(&isp->irq_wakeup_waiter);

        struct sensor_ctrl_ops *ops = &isp->camera.sensor->ops;
        for(i = 0; i < TISP_I2C_SET_BUTTON; i++){
            if(isp->i2c_msgs[i].flag == 0)
                continue;
            isp->i2c_msgs[i].flag = 0;
            switch(i){
                case TISP_I2C_SET_INTEGRATION:
                    if (ops->set_integration_time)
                        ops->set_integration_time(isp->i2c_msgs[i].value);
                    else
                        printf("sensor_attr->ops.set_integration_time is NULL!\n");
                    break;
                case TISP_I2C_SET_AGAIN:
                    if (ops->set_analog_gain)
                        ops->set_analog_gain(isp->i2c_msgs[i].value);
                    else
                        printf("sensor_attr->ops.set_analog_gain is NULL!\n");
                    break;
                case TISP_I2C_SET_DGAIN:
                    if (ops->set_digital_gain)
                        ops->set_digital_gain(isp->i2c_msgs[i].value);
                    else
                        printf("sensor_attr->ops.set_digital_gain is NULL!\n");
                    break;
                default:
                    break;
            }
        }
    }
}

int tiziano_isp_component_bind_sensor_routine(int index, struct sensor_attr *sensor)
{
    struct jz_isp_data *drv = &jz_isp_dev[index];
    int ret = 0;

    assert(drv->is_finish > 0);
    assert(!drv->camera.sensor);

    mutex_lock(&drv->lock);

    drv->core = malloc(sizeof(tisp_core_t));
    assert(drv->core);

    ret = tiziano_mscaler_register_sensor_routine(index, sensor);
    if (ret != 0) {
        printf("Failed to register sensor!\n");
        goto failed_to_register_sensor;
    }

    //tuning init
    ret = tiziano_isp_tuning_init(drv);
    if (ret != 0) {
        printf("Failed to init tuning module!\n");
        ret = -EINVAL;
        goto failed_to_tuning;
    }

    drv->camera.sensor = sensor;
    drv->camera.is_power_on = 0;
    drv->camera.is_stream_on = 0;
    drv->isp_err = 0;
    drv->isp_err1 = 0;
    drv->isp_overflow = 0;
    drv->isp_breakfrm = 0;

    mutex_unlock(&drv->lock);

    return 0;

failed_to_tuning:
    tiziano_mscaler_unregister_sensor_routine(index, sensor);
failed_to_register_sensor:
    mutex_unlock(&drv->lock);
    return ret;
}

void tiziano_isp_component_unbind_sensor_routine(int index, struct sensor_attr *sensor)
{
    struct jz_isp_data *drv = &jz_isp_dev[index];

    assert(drv->is_finish > 0);
    assert(drv->camera.sensor);
    assert(sensor == drv->camera.sensor);

    mutex_lock(&drv->lock);

    tiziano_isp_tuning_deinit(drv);

    tiziano_mscaler_unregister_sensor_routine(index, sensor);

    drv->camera.sensor = NULL;

    if (drv->core)
        free(drv->core);
    drv->core = NULL;

    mutex_unlock(&drv->lock);
}

int jz_isp_drv_init(int index)
{
    struct jz_isp_data *drv = &jz_isp_dev[index];
    int ret;

    mutex_init(&drv->lock);

    request_irq_disabled(drv->irq, 0, tiziano_isp_irq_handler, drv->irq_name, (void *)drv);

    thread_waiter_init(&drv->irq_wakeup_waiter);
    drv->irq_wakeup_thread = thread_create("isp_irq_thread_handler", 1024, tiziano_isp_irq_thread_handler, (void *)drv);
    assert(drv->irq_wakeup_thread);

    ret = jz_mscaler_drv_init(index);
    if (ret) {
        printf("camera: failed to init mscaler\n");
        goto error_mscaler_init;
    }

    ddrc_adjust_controller_channel_priority();

    drv->is_finish = 1;

    //printf("isp%d initialization successfully\n", index);
    return 0;

error_mscaler_init:
    release_irq(drv->irq);
    return ret;
}

void jz_isp_drv_deinit(int index)
{
    struct jz_isp_data *drv = &jz_isp_dev[index];

    if (!drv->is_finish)
        return ;

    drv->is_finish = 0;

    jz_mscaler_drv_deinit(index);

    thread_delete(drv->irq_wakeup_thread);
    release_irq(drv->irq);

    return ;
}
