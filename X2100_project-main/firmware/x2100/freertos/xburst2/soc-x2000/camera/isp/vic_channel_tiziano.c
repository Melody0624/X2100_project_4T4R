/*
 * Copyright (C) 2020 Ingenic Semiconductor Co., Ltd.
 *
 * Camera Driver for the Ingenic VIC controller
 *
 */

#include <common.h>
#include <list.h>
#include <os.h>
#include <bit_field.h>
#include <driver/irq.h>
#include "../hal/camera_gpio.h"
#include "../hal/vic.h"
#include "isp.h"

struct jz_vic_tiziano_data {
    int index;
    int is_enable;
    int is_finish;

    int irq;
    const char *irq_name;

    struct camera_device camera;
    struct mutex lock;

    unsigned int vic_frd_c;     /* frame done cnt */
    unsigned int vic_fre_c;     /* frame err cnt */
    unsigned int vic_frov_c;    /* frame overflow cnt */
};


static struct jz_vic_tiziano_data jz_vic_tiziano_dev[2] = {
    {
        .index                  = 0,
        .is_enable              = 0,
        .irq                    = IRQ_VIC0, /* BASE + 19 */
        .irq_name               = "VIC0",
    },

    {
        .index                  = 1,
        .is_enable              = 0,
        .irq                    = IRQ_VIC1, /* BASE + 18 */
        .irq_name               = "VIC1",
    },
};

/*
 * 调试抓raw图
 */
static void vic_irq_isp_handler(int irq, void *data)
{
    struct jz_vic_tiziano_data *drv = (struct jz_vic_tiziano_data *)data;
    int index = drv->index;
    volatile unsigned long state, pending, mask;

    state = vic_read_reg(index, VIC_INT_STA);
    vic_write_reg(index, VIC_INT_CLR, state);
    mask = vic_read_reg(index, VIC_INT_MASK);
    pending = state & (~mask);

    if (get_bit_field_v(&pending, VIC_HVRES_ERR)) {
        drv->vic_fre_c++;
        printf("## VIC WARN status = 0x%08lx\n", pending);
    }
    if (get_bit_field_v(&pending, VIC_FIFO_OVF)) {
        drv->vic_frov_c++;
    }
    if (get_bit_field_v(&pending, DMA_FRD)) {
    }
    if (get_bit_field_v(&pending, VIC_FRD)) {
        drv->vic_frd_c++;
    }

    return ;
}


int vic_tiziano_stream_on(int index, struct sensor_attr *attr)
{
    struct jz_vic_tiziano_data *drv = &jz_vic_tiziano_dev[index];

    int ret = vic_stream_on(index, attr);
    if (ret) {
        printf("vic%d(tiziano) : vic stream on failed\n", index);
        return ret;
    }

    enable_irq(drv->irq);

    return 0;
}


void vic_tiziano_stream_off(int index, struct sensor_attr *attr)
{
    struct jz_vic_tiziano_data *drv = &jz_vic_tiziano_dev[index];

    disable_irq(drv->irq);

    vic_stream_off(index, attr);
}

int vic_tiziano_power_on(int index)
{
    return vic_power_on(index);
}

void vic_tiziano_power_off(int index)
{
    vic_power_off(index);
}

#define error_if(_cond)                                                 \
    do {                                                                \
        if (_cond) {                                                    \
            printf("vic(tiziano): failed to check: %s\n", #_cond);\
            ret = -1;                                                   \
            goto unlock;                                                \
        }                                                               \
    } while (0)


/*
 * 格式信息转换 转换后的格式提供给ISP
 * sensor format : Sensor格式在sensor driver中根据setting指定
 * camera format : Camera格式在camera driver中使用,并暴露给应用
 *
 */
struct format_mapping {
    sensor_pixel_fmt sensor_fmt; /* sensor格式 */
    camera_pixel_fmt camera_fmt; /* Camera格式(Mscaler输出) */
};

static struct format_mapping sensor_camera_format_map[] = {
    {SENSOR_PIXEL_FMT_SBGGR8_1X8,       CAMERA_PIX_FMT_SBGGR8},
    {SENSOR_PIXEL_FMT_SGBRG8_1X8,       CAMERA_PIX_FMT_SGBRG8},
    {SENSOR_PIXEL_FMT_SGRBG8_1X8,       CAMERA_PIX_FMT_SGRBG8},
    {SENSOR_PIXEL_FMT_SRGGB8_1X8,       CAMERA_PIX_FMT_SRGGB8},
    {SENSOR_PIXEL_FMT_SBGGR10_1X10,     CAMERA_PIX_FMT_SBGGR10},
    {SENSOR_PIXEL_FMT_SGBRG10_1X10,     CAMERA_PIX_FMT_SGBRG10},
    {SENSOR_PIXEL_FMT_SGRBG10_1X10,     CAMERA_PIX_FMT_SGRBG10},
    {SENSOR_PIXEL_FMT_SRGGB10_1X10,     CAMERA_PIX_FMT_SRGGB10},
    {SENSOR_PIXEL_FMT_SBGGR12_1X12,     CAMERA_PIX_FMT_SBGGR12},
    {SENSOR_PIXEL_FMT_SGBRG12_1X12,     CAMERA_PIX_FMT_SGBRG12},
    {SENSOR_PIXEL_FMT_SGRBG12_1X12,     CAMERA_PIX_FMT_SGRBG12},
    {SENSOR_PIXEL_FMT_SRGGB12_1X12,     CAMERA_PIX_FMT_SRGGB12},
};

static int sensor_attribute_check_init(int index, struct sensor_attr *sensor)
{
    int ret = -EINVAL;

    error_if(!sensor->device_name);
    error_if(sensor->sensor_info.width < 128 || sensor->sensor_info.width > 2048);
    error_if(sensor->sensor_info.height < 128);
    error_if(!sensor->ops.power_on);
    error_if(!sensor->ops.power_off);
    error_if(!sensor->ops.stream_on);
    error_if(!sensor->ops.stream_off);

    memset(sensor->info.name, 0x00, sizeof(sensor->info.name));
    memcpy(sensor->info.name, sensor->device_name, strlen(sensor->device_name));

    if ( (sensor->dbus_type == SENSOR_DATA_BUS_MIPI) && (sensor->mipi.mipi_crop.enable) ) {
        sensor->info.width =  sensor->mipi.mipi_crop.output_width;
        sensor->info.height =  sensor->mipi.mipi_crop.output_height;
    } else {
        sensor->info.width =  sensor->sensor_info.width;
        sensor->info.height =  sensor->sensor_info.height;
    }

    int i = 0;
    int size = ARRAY_SIZE(sensor_camera_format_map);
    for (i = 0; i < size; i++) {
        if (sensor->sensor_info.fmt == sensor_camera_format_map[i].sensor_fmt)
            break;
    }

    if (i >= size) {
        printf("attribute check: sensor data_fmt(0x%x) is NOT support.\n", sensor->sensor_info.fmt);
        goto unlock;
    }

    sensor->info.data_fmt = sensor_camera_format_map[i].camera_fmt;

    if (sensor->dbus_type == SENSOR_DATA_BUS_DVP) {
        switch (sensor->dvp.gpio_mode) {
        case DVP_PA_LOW_10BIT:
        case DVP_PA_HIGH_10BIT:
            if (sensor->dvp.data_fmt > DVP_RAW10) {
                printf("attribute check: data_fmt set error,should be less than DVP_RAW12.\n");
                goto unlock;
            }
            break;

        case DVP_PA_12BIT:
            break;

        case DVP_PA_LOW_8BIT:
        case DVP_PA_HIGH_8BIT:
            if (sensor->dvp.data_fmt < DVP_YUV422){
                if (sensor->dvp.data_fmt > DVP_RAW8) {
                    printf("attribute check: data_fmt set error,should be DVP_RAW8.\n");
                    goto unlock;
                }
            }
            break;

        default:
            printf("attribute check: Unsupported this format.\n");
            goto unlock;
        }
    }
    return 0;

unlock:
    return ret;
}

int soc_vic_register_sensor_tiziano_routine(int index, struct sensor_attr *sensor)
{
    struct jz_vic_tiziano_data *drv = &jz_vic_tiziano_dev[index];
    int ret = 0;

    assert(drv->is_finish > 0);
    assert(!drv->camera.sensor);

    sensor_attribute_check_init(index, sensor);
    assert(ret == 0);

    mutex_lock(&drv->lock);

    ret = tiziano_isp_component_bind_sensor_routine(index, sensor);
    if (!ret)
        drv->camera.sensor = sensor;

    mutex_unlock(&drv->lock);

    return ret;
}

void soc_vic_unregister_sensor_tiziano_routine(int index, struct sensor_attr *sensor)
{
    assert(index < 2);
    struct jz_vic_tiziano_data *drv = &jz_vic_tiziano_dev[index];
    assert(drv->is_finish > 0);
    assert(drv->camera.sensor);
    assert(sensor == drv->camera.sensor);

    mutex_lock(&drv->lock);

    tiziano_isp_component_unbind_sensor_routine(index, sensor);

    drv->camera.sensor = NULL;

    mutex_unlock(&drv->lock);
}


int jz_vic_tiziano_drv_init(int index)
{
    struct jz_vic_tiziano_data *drv = &jz_vic_tiziano_dev[index];
    int ret;

    mutex_init(&drv->lock);

    request_irq_disabled(drv->irq, 0, vic_irq_isp_handler, drv->irq_name, drv);

    ret = jz_isp_drv_init(index);
    if (ret) {
        printf("camera: failed to init isp%d resources\n", index);
        goto error_isp_drv_init;
    }

    drv->is_finish = 1;

    //printf("vic%d(tiziano) register successfully\n", index);
    return 0;

error_isp_drv_init:
    release_irq(drv->irq);
    return ret;
}

void jz_vic_tiziano_drv_deinit(int index)
{
    struct jz_vic_tiziano_data *drv = &jz_vic_tiziano_dev[index];

    if (!drv->is_finish)
        return ;

    drv->is_finish = 0;

    jz_isp_drv_deinit(index);

    release_irq(drv->irq);
}

