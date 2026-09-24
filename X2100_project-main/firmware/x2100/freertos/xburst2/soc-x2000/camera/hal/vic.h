/*
 * Copyright (C) 2020 Ingenic Semiconductor Co., Ltd.
 *
 * Camera driver for the Ingenic VIC
 *
 */
#ifndef __X2000_VIC_H__
#define __X2000_VIC_H__

#include <common.h>
#include <bit_field.h>
#include <soc/base.h>
#include <soc/camera_sensor.h>

#include "vic_regs.h"

/*
 * VIC Operation
 */
static const unsigned long vic_iobase[] = {
        KSEG1ADDR(VIC0_IOBASE),
        KSEG1ADDR(VIC1_IOBASE),
};

#define VIC_ADDR(id, reg)               ((volatile unsigned long *)((vic_iobase[id]) + (reg)))

static inline void vic_write_reg(int index, unsigned int reg, unsigned int val)
{
    *VIC_ADDR(index, reg) = val;
}

static inline unsigned int vic_read_reg(int index, unsigned int reg)
{
    return *VIC_ADDR(index, reg);
}

static inline void vic_set_bit(int index, unsigned int reg, unsigned int start, unsigned int end, unsigned int val)
{
    set_bit_field_v(VIC_ADDR(index, reg), start, end, val);
}

static inline unsigned int vic_get_bit(int index, unsigned int reg, unsigned int start, unsigned int end)
{
    return get_bit_field_v(VIC_ADDR(index, reg), start, end);
}


unsigned int is_output_y8(int index, struct sensor_attr *attr);
unsigned int is_output_yuv422(int index, struct sensor_attr *attr);
int vic_stream_on(int index, struct sensor_attr *attr);
void vic_stream_off(int index, struct sensor_attr *attr);
int vic_power_on(int index);
void vic_power_off(int index);
int vic_power_state(int index);
int vic_stream_state(int index);

/*
 * 对外接口
 */
void soc_vic_camera_enable_sensor_mclk(int index, unsigned long clk_rate);
void soc_vic_camera_disable_sensor_mclk(int index);
int soc_vic_camera_register_sensor(int index, struct sensor_attr *sensor);
void soc_vic_camera_unregister_sensor(int index, struct sensor_attr *sensor);

struct camera_device *soc_camera_hal_detect(int index);
struct camera_info *soc_camera_hal_get_info(struct camera_device *camera);
int soc_camera_hal_power_on(struct camera_device *camera);
void soc_camera_hal_power_off(struct camera_device *camera);
int soc_camera_hal_stream_on(struct camera_device *camera);
void soc_camera_hal_stream_off(struct camera_device *camera);
camera_frame_error_type soc_camera_hal_get_frame_error(struct camera_device *camera);
void *soc_camera_hal_get_frame(struct camera_device *camera);
void *soc_camera_hal_wait_frame(struct camera_device *camera);
int soc_camera_hal_put_frame(struct camera_device *camera, void *buf);
int soc_camera_hal_dqbuf(struct camera_device *camera, struct frame_info *frame);
int soc_camera_hal_dqbuf_wait(struct camera_device *camera, struct frame_info *frame);
int soc_camera_hal_qbuf(struct camera_device *camera, struct frame_info *frame);
int soc_camera_set_hal_sensor_reg(struct camera_device *camera, struct sensor_dbg_register *reg);
int soc_camera_get_hal_sensor_reg(struct camera_device *camera, struct sensor_dbg_register *reg);
unsigned int soc_camera_hal_get_available_frame_count(struct camera_device *camera);
void soc_camera_hal_skip_frames(struct camera_device *camera, unsigned int frames);

int jz_arch_vic_init(void);
void jz_arch_vic_exit(void);


#endif /* __X2000_VIC_H__ */
