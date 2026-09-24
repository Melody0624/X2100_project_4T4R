/*
 * Copyright (C) 2020 Ingenic Semiconductor Co., Ltd.
 *
 * Camera driver for the Ingenic VIC
 *
 */
#ifndef __X2000_VIC_CHANNEL_TIZIANO_H__
#define __X2000_VIC_CHANNEL_TIZIANO_H__

#include <soc/camera_sensor.h>

int vic_tiziano_stream_on(int index, struct sensor_attr *attr);
void vic_tiziano_stream_off(int index, struct sensor_attr *attr);
int vic_tiziano_power_on(int index);
void vic_tiziano_power_off(int index);

int soc_vic_register_sensor_tiziano_routine(int index, struct sensor_attr *sensor);
void soc_vic_unregister_sensor_tiziano_routine(int index, struct sensor_attr *sensor);

int jz_vic_tiziano_drv_init(int index);
void jz_vic_tiziano_drv_deinit(int index);


#endif /* __X2000_VIC_CHANNEL_TIZIANO_H__ */
