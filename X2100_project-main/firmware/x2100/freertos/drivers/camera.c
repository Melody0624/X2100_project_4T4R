#include <driver/camera.h>
#include <common.h>

#define CIS_DPI_CFG_MAX_COUNT  5
struct cis_dpi_config support_dpi[CIS_DPI_CFG_MAX_COUNT];

/*
 * soc 实现的函数
 */
void soc_camera_init(void);
struct camera_device *soc_camera_detect(int index);
struct camera_info *soc_camera_get_info(struct camera_device *camera);
unsigned char *soc_camera_get_sensor_id(struct camera_device *camera);
int soc_camera_power_on(struct camera_device *camera);
void soc_camera_power_off(struct camera_device *camera);
int soc_camera_stream_on(struct camera_device *camera);
void soc_camera_stream_off(struct camera_device *camera);
void *soc_camera_wait_frame(struct camera_device *camera);
camera_frame_error_type soc_camera_get_frame_error(struct camera_device *camera);
void *soc_camera_get_frame(struct camera_device *camera);
int soc_camera_put_frame(struct camera_device *camera, void *buf);
int soc_camera_dqbuf(struct camera_device *camera, struct frame_info *frame);
int soc_camera_dqbuf_wait(struct camera_device *camera, struct frame_info *frame);
int soc_camera_qbuf(struct camera_device *camera, struct frame_info *frame);
int soc_camera_set_sensor_reg(struct camera_device *camera, struct sensor_dbg_register *reg);
int soc_camera_get_sensor_reg(struct camera_device *camera, struct sensor_dbg_register *reg);
unsigned int soc_camera_get_available_frame_count(struct camera_device *camera);
void soc_camera_skip_frames(struct camera_device *camera, unsigned int frames);
int soc_camera_set_luminance_area(struct camera_device *camera, int lumi_enable, int x1, int y1, int x2, int y2);
int soc_camera_get_luminance_area_total(struct camera_device *camera, void *frame, void *lumi);

int soc_camera_set_cis_dpi(struct camera_device *camera, int dpi);
int soc_camera_get_cis_dpi(struct camera_device *camera, struct cis_dpi_config *cfg);
int soc_camera_get_support_dpi(struct camera_device *camera, struct cis_dpi_config *cfg);
int soc_camera_set_cis_led_brightness(unsigned int r, unsigned int g, unsigned int b);
int soc_camera_set_cis_led_enable(int enable);

__weak int soc_camera_get_sensor_reg(struct camera_device *camera, struct sensor_dbg_register *reg)
{
    printf("%s not implemented!\n", __FUNCTION__);
    return -1;
}

__weak int soc_camera_set_sensor_reg(struct camera_device *camera, struct sensor_dbg_register *reg)
{
    printf("%s not implemented!\n", __FUNCTION__);
    return -1;
}

__weak int soc_camera_set_cis_led_brightness(unsigned int r,
                                             unsigned int g,
                                             unsigned int b)
{
    printf("%s not implemented!\n", __FUNCTION__);
    return -1;
}

__weak int soc_camera_set_cis_led_enable(int enable)
{
    printf("%s not implemented!\n", __FUNCTION__);
    return -1;
}

void camera_init(void)
{
    soc_camera_init();
}

struct camera_device *camera_detect(int index)
{
    return soc_camera_detect(index);
}

struct camera_info *camera_get_info(struct camera_device *camera)
{
    return soc_camera_get_info(camera);
}

unsigned char *camera_get_sensor_id(struct camera_device *camera)
{
    return soc_camera_get_sensor_id(camera);
}

int camera_power_on(struct camera_device *camera)
{
    return soc_camera_power_on(camera);
}

void camera_power_off(struct camera_device *camera)
{
    soc_camera_power_off(camera);
}

int camera_stream_on(struct camera_device *camera)
{
    return soc_camera_stream_on(camera);
}

void camera_stream_off(struct camera_device *camera)
{
    soc_camera_stream_off(camera);
}

void *camera_wait_frame(struct camera_device *camera)
{
    return soc_camera_wait_frame(camera);
}

camera_frame_error_type camera_get_frame_error(struct camera_device *camera)
{
    return soc_camera_get_frame_error(camera);
}

void camera_put_frame(struct camera_device *camera, void *frame)
{
    soc_camera_put_frame(camera, frame);
}

void *camera_get_frame(struct camera_device *camera)
{
    return soc_camera_get_frame(camera);
}

int camera_dqbuf(struct camera_device *camera, struct frame_info *frame)
{
    return soc_camera_dqbuf(camera, frame);
}

int camera_dqbuf_wait(struct camera_device *camera, struct frame_info *frame)
{
    return soc_camera_dqbuf_wait(camera, frame);
}

int camera_qbuf(struct camera_device *camera, struct frame_info *frame)
{
    return soc_camera_qbuf(camera, frame);
}

int camera_get_sensor_reg(struct camera_device *camera, struct sensor_dbg_register *reg)
{
    return soc_camera_get_sensor_reg(camera, reg);
}

int camera_set_sensor_reg(struct camera_device *camera, struct sensor_dbg_register *reg)
{
    return soc_camera_set_sensor_reg(camera, reg);
}

unsigned int camera_get_available_frame_count(struct camera_device *camera)
{
    return soc_camera_get_available_frame_count(camera);
}

void camera_skip_frames(struct camera_device *camera, unsigned int frames)
{
    soc_camera_skip_frames(camera, frames);
}

int camera_set_luminance_area(struct camera_device *camera, int lumi_enable, int x1, int y1, int x2, int y2)
{
    return soc_camera_set_luminance_area(camera, lumi_enable, x1, y1, x2, y2);
}

int camera_get_luminance_area_total(struct camera_device *camera, void *frame, void *lumi)
{
    return soc_camera_get_luminance_area_total(camera, frame, lumi);
}

/* cis devices */
int camera_get_cis_dpi(struct camera_device *camera, struct cis_dpi_config *cfg)
{
    return soc_camera_get_cis_dpi(camera, cfg);
}

struct cis_dpi_config* camera_get_support_dpi(struct camera_device *camera, int* size)
{
    *size = soc_camera_get_support_dpi(camera, support_dpi);
    return support_dpi;
}

int camera_set_cis_dpi(struct camera_device *camera, int dpi)
{
    return soc_camera_set_cis_dpi(camera, dpi);
}

int camera_set_cis_led_brightness(unsigned int r, unsigned int g, unsigned int b)
{
    return soc_camera_set_cis_led_brightness(r, g, b);
}

int camera_set_cis_led_enable(int enable)
{
    return soc_camera_set_cis_led_enable(enable);
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(camera_init);
EXPORT_SYMBOL(camera_detect);
EXPORT_SYMBOL(camera_get_info);
EXPORT_SYMBOL(camera_power_on);
EXPORT_SYMBOL(camera_power_off);
EXPORT_SYMBOL(camera_stream_on);
EXPORT_SYMBOL(camera_stream_off);
EXPORT_SYMBOL(camera_wait_frame);
EXPORT_SYMBOL(camera_put_frame);
EXPORT_SYMBOL(camera_get_frame);
EXPORT_SYMBOL(camera_dqbuf);
EXPORT_SYMBOL(camera_dqbuf_wait);
EXPORT_SYMBOL(camera_qbuf);
EXPORT_SYMBOL(camera_set_sensor_reg);
EXPORT_SYMBOL(camera_get_sensor_reg);
EXPORT_SYMBOL(camera_get_frame_error);
EXPORT_SYMBOL(camera_get_available_frame_count);
EXPORT_SYMBOL(camera_skip_frames);

EXPORT_SYMBOL(camera_get_cis_dpi);
EXPORT_SYMBOL(camera_get_support_dpi);
EXPORT_SYMBOL(camera_set_cis_dpi);
EXPORT_SYMBOL(camera_set_cis_led_brightness);
EXPORT_SYMBOL(camera_set_cis_led_enable);
