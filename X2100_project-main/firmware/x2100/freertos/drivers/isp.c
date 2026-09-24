#include <driver/camera_isp.h>

/*
 * soc 实现的函数
 */

camera_hd_t *soc_isp_detect(int index, int channel);
void soc_isp_release(camera_hd_t *camera_hd);
struct camera_info *soc_isp_get_info(camera_hd_t *camera_hd);
struct camera_info *soc_isp_get_sensor_info(camera_hd_t *camera_hd);
int soc_isp_power_on(camera_hd_t *camera_hd);
void soc_isp_power_off(camera_hd_t *camera_hd);
int soc_isp_stream_on(camera_hd_t *camera_hd);
void soc_isp_stream_off(camera_hd_t *camera_hd);
camera_frame_error_type soc_isp_get_frame_error(camera_hd_t *camera_hd);
void *soc_isp_wait_frame(camera_hd_t *camera_hd);
void *soc_isp_get_frame(camera_hd_t *camera_hd);
int soc_isp_put_frame(camera_hd_t *camera_hd, void *frame);
int soc_isp_dqbuf(camera_hd_t *camera_hd, struct frame_info *frame);
int soc_isp_qbuf(camera_hd_t *camera_hd, struct frame_info *frame);
int soc_isp_dqbuf_wait(camera_hd_t *camera_hd, struct frame_info *frame);
unsigned int soc_isp_get_available_frame_count(camera_hd_t *camera_hd);
void soc_isp_skip_frames(camera_hd_t *camera_hd, unsigned int frames);
int soc_isp_get_max_scaler_size(camera_hd_t *camera_hd, int *width, int *height);
int soc_isp_get_line_align_size(camera_hd_t *camera_hd, int *align_size);
int soc_isp_set_format(camera_hd_t *camera_hd, struct frame_image_format *fmt);
int soc_isp_get_format(camera_hd_t *camera_hd, struct frame_image_format *fmt);
int soc_isp_request_buffer(camera_hd_t *camera_hd, struct frame_image_format *fmt);
int soc_isp_free_buffer(camera_hd_t *camera_hd);
int soc_isp_get_sensor_reg(camera_hd_t *camera_hd, struct sensor_dbg_register *reg);
int soc_isp_set_sensor_reg(camera_hd_t *camera_hd, struct sensor_dbg_register *reg);


camera_hd_t *isp_detect(int index, int channel)
{
    return soc_isp_detect(index, channel);
}

void isp_release(camera_hd_t *camera_hd)
{
    soc_isp_release(camera_hd);
}

struct camera_info *isp_get_info(camera_hd_t *camera_hd)
{
    return soc_isp_get_info(camera_hd);
}

struct camera_info *isp_get_sensor_info(camera_hd_t *camera_hd)
{
    return soc_isp_get_sensor_info(camera_hd);
}

int isp_power_on(camera_hd_t *camera_hd)
{
    return soc_isp_power_on(camera_hd);
}

void isp_power_off(camera_hd_t *camera_hd)
{
    soc_isp_power_off(camera_hd);
}

int isp_stream_on(camera_hd_t *camera_hd)
{
    return soc_isp_stream_on(camera_hd);
}

void isp_stream_off(camera_hd_t *camera_hd)
{
    soc_isp_stream_off(camera_hd);
}

camera_frame_error_type isp_get_frame_error(camera_hd_t *camera_hd)
{
    return soc_isp_get_frame_error(camera_hd);
}

void *isp_wait_frame(camera_hd_t *camera_hd)
{
    return soc_isp_wait_frame(camera_hd);
}

void *isp_get_frame(camera_hd_t *camera_hd)
{
    return soc_isp_get_frame(camera_hd);
}

int isp_put_frame(camera_hd_t *camera_hd, void *frame)
{
    return soc_isp_put_frame(camera_hd, frame);
}

int isp_dqbuf(camera_hd_t *camera_hd, struct frame_info *frame)
{
    return soc_isp_dqbuf(camera_hd, frame);
}

int isp_qbuf(camera_hd_t *camera_hd, struct frame_info *frame)
{
    return soc_isp_qbuf(camera_hd, frame);
}

int isp_dqbuf_wait(camera_hd_t *camera_hd, struct frame_info *frame)
{
    return soc_isp_dqbuf_wait(camera_hd, frame);
}

unsigned int isp_get_available_frame_count(camera_hd_t *camera_hd)
{
    return soc_isp_get_available_frame_count(camera_hd);
}

void isp_skip_frames(camera_hd_t *camera_hd, unsigned int frames)
{
    soc_isp_skip_frames(camera_hd, frames);
}

int isp_get_max_scaler_size(camera_hd_t *camera_hd, int *width, int *height)
{
    return soc_isp_get_max_scaler_size(camera_hd, width, height);
}

int isp_get_line_align_size(camera_hd_t *camera_hd, int *align_size)
{
    return soc_isp_get_line_align_size(camera_hd, align_size);
}

int isp_set_format(camera_hd_t *camera_hd, struct frame_image_format *fmt)
{
    return soc_isp_set_format(camera_hd, fmt);
}

int isp_get_format(camera_hd_t *camera_hd, struct frame_image_format *fmt)
{
    return soc_isp_get_format(camera_hd, fmt);
}

int isp_request_buffer(camera_hd_t *camera_hd, struct frame_image_format *fmt)
{
    return soc_isp_request_buffer(camera_hd, fmt);
}

int isp_free_buffer(camera_hd_t *camera_hd)
{
    return soc_isp_free_buffer(camera_hd);
}

int isp_get_sensor_reg(camera_hd_t *camera_hd, struct sensor_dbg_register *reg)
{
    return soc_isp_get_sensor_reg(camera_hd, reg);
}

int isp_set_sensor_reg(camera_hd_t *camera_hd, struct sensor_dbg_register *reg)
{
    return soc_isp_set_sensor_reg(camera_hd, reg);
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(isp_detect);
EXPORT_SYMBOL(isp_release);
EXPORT_SYMBOL(isp_get_info);
EXPORT_SYMBOL(isp_get_info);
EXPORT_SYMBOL(isp_get_sensor_info);
EXPORT_SYMBOL(isp_power_on);
EXPORT_SYMBOL(isp_power_off);
EXPORT_SYMBOL(isp_stream_on);
EXPORT_SYMBOL(isp_stream_off);
EXPORT_SYMBOL(isp_get_frame_error);
EXPORT_SYMBOL(isp_wait_frame);
EXPORT_SYMBOL(isp_get_frame);
EXPORT_SYMBOL(isp_put_frame);
EXPORT_SYMBOL(isp_dqbuf);
EXPORT_SYMBOL(isp_qbuf);
EXPORT_SYMBOL(isp_dqbuf_wait);
EXPORT_SYMBOL(isp_get_available_frame_count);
EXPORT_SYMBOL(isp_skip_frames);
EXPORT_SYMBOL(isp_get_max_scaler_size);
EXPORT_SYMBOL(isp_get_line_align_size);
EXPORT_SYMBOL(isp_set_format);
EXPORT_SYMBOL(isp_get_format);
EXPORT_SYMBOL(isp_request_buffer);
EXPORT_SYMBOL(isp_free_buffer);
EXPORT_SYMBOL(isp_get_sensor_reg);
EXPORT_SYMBOL(isp_set_sensor_reg);


