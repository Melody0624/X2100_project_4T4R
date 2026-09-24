/*
 * Copyright (C) 2020 Ingenic Semiconductor Co., Ltd.
 *
 * Camera driver for the Ingenic CIM Controller
 *
 */
#ifndef __X2000_CIM_H__
#define __X2000_CIM_H__

#include <soc/camera_sensor.h>

int soc_cim_detect(int index);
struct camera_info *soc_cim_get_info(int index);
int soc_cim_power_on(int index);
void soc_cim_power_off(int index);
int soc_cim_stream_on(int index);
void soc_cim_stream_off(int index);
camera_frame_error_type soc_cim_get_frame_error(int index);
void *soc_cim_get_frame(int index);
void *soc_cim_wait_frame(int index);
int soc_cim_put_frame(int index, void *buf);
int soc_cim_dqbuf(int index, struct frame_info *frame);
int soc_cim_dqbuf_wait(int index, struct frame_info *frame);
int soc_cim_qbuf(int index, struct frame_info *frame);
int soc_cim_set_hal_sensor_reg(int index, struct sensor_dbg_register *reg);
int soc_cim_get_hal_sensor_reg(int index, struct sensor_dbg_register *reg);
unsigned int soc_cim_get_available_frame_count(int index);
void soc_cim_skip_frames(int index, unsigned int frames);

int soc_cim_register_sensor_routine(int index, int mem_cnt, struct sensor_attr *sensor);
int soc_cim_unregister_sensor_routine(int index, struct sensor_attr *sensor);

int jz_cim_drv_init(void);
void jz_cim_drv_deinit(void);


#endif /* __X2000_CIM_H__ */
