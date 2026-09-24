/*
 * Copyright (C) 2020 Ingenic Semiconductor Co., Ltd.
 *
 * Camera driver for the Ingenic VIC
 *
 */
#ifndef __X2000_VIC_CHANNEL_MEM_H__
#define __X2000_VIC_CHANNEL_MEM_H__

#include <soc/camera_sensor.h>

int soc_vic_chan_mem_detect(int index);
struct camera_info *soc_vic_chan_mem_get_info(int index);
int soc_vic_chan_mem_stream_on(int index);
void soc_vic_chan_mem_stream_off(int index);
int soc_vic_chan_mem_power_on(int index);
void soc_vic_chan_mem_power_off(int index);
camera_frame_error_type soc_vic_chan_mem_get_frame_error(int index);
void *soc_vic_chan_mem_wait_frame(int index);
void *soc_vic_chan_mem_get_frame(int index);
int soc_vic_chan_mem_put_frame(int index, void *buf);
int soc_vic_chan_mem_dqbuf(int index, struct frame_info *frame);
int soc_vic_chan_mem_dqbuf_wait(int index, struct frame_info *frame);
int soc_vic_chan_mem_qbuf(int index, struct frame_info *frame);
int soc_vic_chan_set_hal_sensor_reg(int index,struct sensor_dbg_register *reg);
int soc_vic_chan_get_hal_sensor_reg(int index,struct sensor_dbg_register *reg);
unsigned int soc_vic_chan_mem_get_available_frame_count(int index);
void soc_vic_chan_mem_skip_frames(int index, unsigned int frames);

int soc_vic_register_sensor_mem_routine(int index, int mem_cnt, struct sensor_attr *sensor);
void soc_vic_unregister_sensor_mem_routine(int index, struct sensor_attr *sensor);

int jz_vic_mem_drv_init(int index);
void jz_vic_mem_drv_deinit(int index);


#endif /* __X2000_VIC_CHANNEL_MEM_H__ */
