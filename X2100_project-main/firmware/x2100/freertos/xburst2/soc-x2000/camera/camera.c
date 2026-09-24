#include <driver/camera.h>
#include <common.h>
#include <list.h>
#include <os.h>
#include <soc/camera_sensor.h>
#include <soc/base.h>
#include <hal/vic.h>
#include <cim/cim.h>
#include <isp/mscaler.h>

/*
 * vic/camera应用调用接口
 */
struct camera_device *soc_camera_detect(int index)
{
    struct camera_device *camera;

    camera = soc_camera_hal_detect(index);

    return camera;
}

struct camera_info *soc_camera_get_info(struct camera_device *camera)
{
    return soc_camera_hal_get_info(camera);
}

int soc_camera_power_on(struct camera_device *camera)
{
    return soc_camera_hal_power_on(camera);
}

void soc_camera_power_off(struct camera_device *camera)
{
    soc_camera_hal_power_off(camera);
}

int soc_camera_stream_on(struct camera_device *camera)
{
    return soc_camera_hal_stream_on(camera);
}

void soc_camera_stream_off(struct camera_device *camera)
{
    soc_camera_hal_stream_off(camera);
}

camera_frame_error_type soc_camera_get_frame_error(struct camera_device *camera)
{
    return soc_camera_hal_get_frame_error(camera);
}

void *soc_camera_wait_frame(struct camera_device *camera)
{
    return soc_camera_hal_wait_frame(camera);
}

int soc_camera_put_frame(struct camera_device *camera, void *buf)
{
    return soc_camera_hal_put_frame(camera, buf);
}

void *soc_camera_get_frame(struct camera_device *camera)
{
      return soc_camera_hal_get_frame(camera);
}

int soc_camera_dqbuf(struct camera_device *camera, struct frame_info *frame)
{
      return soc_camera_hal_dqbuf(camera, frame);
}

int soc_camera_dqbuf_wait(struct camera_device *camera, struct frame_info *frame)
{
      return soc_camera_hal_dqbuf_wait(camera, frame);
}

int soc_camera_qbuf(struct camera_device *camera, struct frame_info *frame)
{
      return soc_camera_hal_qbuf(camera, frame);
}

int soc_camera_set_sensor_reg(struct camera_device *camera, struct sensor_dbg_register *reg)
{
      return soc_camera_set_hal_sensor_reg(camera, reg);
}

int soc_camera_get_sensor_reg(struct camera_device *camera, struct sensor_dbg_register *reg)
{
      return soc_camera_get_hal_sensor_reg(camera, reg);
}

unsigned int soc_camera_get_available_frame_count(struct camera_device *camera)
{
    return soc_camera_hal_get_available_frame_count(camera);
}

void soc_camera_skip_frames(struct camera_device *camera, unsigned int frames)
{
    soc_camera_hal_skip_frames(camera, frames);
}

int soc_camera_set_luminance_area(struct camera_device *camera,
    int lumi_enable, int x1, int y1, int x2, int y2)
{
    /* 不支持该功能 */
    return -EPERM;
}

int soc_camera_get_luminance_area_total(struct camera_device *camera, void *frame, void *lumi)
{
    /* 不支持该功能 */
    return 0;
}



/*
 * isp应用调用接口
 */
camera_hd_t *soc_isp_detect(int index, int channel)
{
    return soc_mscaler_detect(index, channel);
}

void soc_isp_release(camera_hd_t *hd)
{
    soc_mscaler_release(hd);
}

struct camera_info *soc_isp_get_info(camera_hd_t *hd)
{
    return soc_mscaler_get_info(hd);
}

struct camera_info *soc_isp_get_sensor_info(camera_hd_t *hd)
{
    return soc_mscaler_get_sensor_info(hd);
}

int soc_isp_power_on(camera_hd_t *hd)
{
    return soc_mscaler_power_on(hd);
}

void soc_isp_power_off(camera_hd_t *hd)
{
    soc_mscaler_power_off(hd);
}

int soc_isp_stream_on(camera_hd_t *hd)
{
    return soc_mscaler_stream_on(hd);
}

void soc_isp_stream_off(camera_hd_t *hd)
{
    soc_mscaler_stream_off(hd);
}

camera_frame_error_type soc_isp_get_frame_error(camera_hd_t *hd)
{
    return soc_mscaler_get_frame_error(hd);
}

void *soc_isp_wait_frame(camera_hd_t *hd)
{
    return soc_mscaler_wait_frame(hd);
}

void *soc_isp_get_frame(camera_hd_t *hd)
{
    return soc_mscaler_get_frame(hd);
}

int soc_isp_put_frame(camera_hd_t *hd, void *frame)
{
    return soc_mscaler_put_frame(hd, frame);
}

int soc_isp_dqbuf(camera_hd_t *hd, struct frame_info *frame)
{
    return soc_mscaler_dqbuf(hd, frame);
}

int soc_isp_qbuf(camera_hd_t *hd, struct frame_info *frame)
{
    return soc_mscaler_qbuf(hd, frame);
}

int soc_isp_dqbuf_wait(camera_hd_t *hd, struct frame_info *frame)
{
    return soc_mscaler_dqbuf_wait(hd, frame);
}

unsigned int soc_isp_get_available_frame_count(camera_hd_t *hd)
{
    return soc_mscaler_get_available_frame_count(hd);
}

void soc_isp_skip_frames(camera_hd_t *hd, unsigned int frames)
{
    soc_mscaler_skip_frames(hd, frames);
}

int soc_isp_get_max_scaler_size(camera_hd_t *hd, int *width, int *height)
{
    return soc_mscaler_get_max_scaler_size(hd, width, height);
}

int soc_isp_get_line_align_size(camera_hd_t *hd, int *align_size)
{
    return soc_mscaler_get_line_align_size(hd, align_size);
}

int soc_isp_set_format(camera_hd_t *hd, struct frame_image_format *fmt)
{
    return soc_mscaler_set_format(hd, fmt);
}

int soc_isp_get_format(camera_hd_t *hd, struct frame_image_format *fmt)
{
    return soc_mscaler_get_format(hd, fmt);
}

int soc_isp_request_buffer(camera_hd_t *hd, struct frame_image_format *fmt)
{
    return soc_mscaler_request_buffer(hd, fmt);
}

int soc_isp_free_buffer(camera_hd_t *hd)
{
    return soc_mscaler_free_buffer(hd);
}


int soc_isp_get_sensor_reg(camera_hd_t *hd, struct sensor_dbg_register *reg)
{
    return soc_mscaler_get_sensor_reg(hd, reg);
}

int soc_isp_set_sensor_reg(camera_hd_t *hd, struct sensor_dbg_register *reg)
{
    return soc_mscaler_set_sensor_reg(hd, reg);
}

/*
 * sensor调用用接口
 */
void camera_enable_sensor_mclk(int index, unsigned long clk_rate)
{
    soc_vic_camera_enable_sensor_mclk(index, clk_rate);
}

void camera_disable_sensor_mclk(int index)
{
    soc_vic_camera_disable_sensor_mclk(index);
}

int camera_register_sensor(int index, struct sensor_attr *sensor)
{
    return soc_vic_camera_register_sensor(index, sensor);
}

void camera_unregister_sensor(int index, struct sensor_attr *sensor)
{
    soc_vic_camera_unregister_sensor(index, sensor);
}

/*
 * 初始化入口
 */
void soc_camera_init(void)
{
    jz_arch_vic_init();
}
