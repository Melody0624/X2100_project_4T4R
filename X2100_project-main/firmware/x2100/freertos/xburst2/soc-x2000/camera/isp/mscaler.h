/*
 * Copyright (C) 2020 Ingenic Semiconductor Co., Ltd.
 *
 * Mulitiple Channel Scaler
 *
 */


#ifndef __X2000_MSCALER_H__
#define __X2000_MSCALER_H__

#include <driver/camera_isp.h>
#include "mscaler_regs.h"
#include <soc/camera_sensor.h>


#define MSCALER_MAX_CH                  3
#define MSCALER_PAD_SINK                0
#define MSCALER_PAD_SOURCE_CH0          1
#define MSCALER_PAD_SOURCE_CH1          2
#define MSCALER_PAD_SOURCE_CH2          3
#define MSCALER_NUM_PADS                4

#define MSCALER_INPUT_MAX_WIDTH         2048
#define MSCALER_INPUT_MAX_HEIGHT        2048

#define MSCALER_OUTPUT0_MAX_WIDTH       2048
#define MSCALER_OUTPUT0_MAX_HEIGHT      2048

#define MSCALER_OUTPUT1_MAX_WIDTH       1280
#define MSCALER_OUTPUT1_MAX_HEIGHT      1280

#define MSCALER_OUTPUT2_MAX_WIDTH       640
#define MSCALER_OUTPUT2_MAX_HEIGHT      640


camera_hd_t *soc_mscaler_detect(int index, int channel);
void soc_mscaler_release(camera_hd_t *hd);
struct camera_info *soc_mscaler_get_info(camera_hd_t *hd);
struct camera_info *soc_mscaler_get_sensor_info(camera_hd_t *hd);
int soc_mscaler_power_on(camera_hd_t *hd);
void soc_mscaler_power_off(camera_hd_t *hd);
int soc_mscaler_stream_on(camera_hd_t *hd);
void soc_mscaler_stream_off(camera_hd_t *hd);
camera_frame_error_type soc_mscaler_get_frame_error(camera_hd_t *hd);
void *soc_mscaler_wait_frame(camera_hd_t *hd);
void *soc_mscaler_get_frame(camera_hd_t *hd);
int soc_mscaler_put_frame(camera_hd_t *hd, void *buf);
int soc_mscaler_dqbuf(camera_hd_t *camera_hd, struct frame_info *frame);
int soc_mscaler_qbuf(camera_hd_t *camera_hd, struct frame_info *frame);
int soc_mscaler_dqbuf_wait(camera_hd_t *camera_hd, struct frame_info *frame);
unsigned int soc_mscaler_get_available_frame_count(camera_hd_t *hd);
void soc_mscaler_skip_frames(camera_hd_t *hd, unsigned int frames);
int soc_mscaler_get_max_scaler_size(camera_hd_t *hd, int *width, int *height);
int soc_mscaler_get_line_align_size(camera_hd_t *hd, int *align_size);
int soc_mscaler_set_format(camera_hd_t *hd, struct frame_image_format *fmt);
int soc_mscaler_get_format(camera_hd_t *hd, struct frame_image_format *fmt);
int soc_mscaler_request_buffer(camera_hd_t *hd, struct frame_image_format *fmt);
int soc_mscaler_free_buffer(camera_hd_t *hd);
int soc_mscaler_get_sensor_reg(camera_hd_t *hd, struct sensor_dbg_register *reg);
int soc_mscaler_set_sensor_reg(camera_hd_t *hd, struct sensor_dbg_register *reg);

int tiziano_mscaler_interrupt_service_routine(int index, unsigned int status);

int tiziano_mscaler_register_sensor_routine(int index, struct sensor_attr *sensor);
void tiziano_mscaler_unregister_sensor_routine(int index, struct sensor_attr *sensor);

int jz_mscaler_drv_init(int index);
void jz_mscaler_drv_deinit(int index);

#endif /* __X2000_MSCALER_H__ */
