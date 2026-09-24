#include <soc/lcdc_data.h>
#include <driver/fb.h>
#include <list.h>
#include <common.h>
#include <os.h>

static LIST_HEAD(fb_list);
static DEFINE_MUTEX_RECURSIVE(lock);

struct lcdc_data *lcdc_data_init(void);
void soc_fb_init(struct lcdc_data *data);
__weak void soc_fb_core_suspend(void) {}
__weak void soc_fb_core_resume(void) {}
int soc_fb_esd_check_work_error(void);
__weak void fb_export_cfg_for_linux(void) {}

void fb_init(void)
{
    soc_fb_init(lcdc_data_init());
}

void fb_core_suspend(void)
{
    soc_fb_core_suspend();
}

void fb_core_resume(void)
{
    soc_fb_core_resume();
}

int fb_esd_check_work_error(void)
{
    return soc_fb_esd_check_work_error();
}

struct list_head *fb_get_device_list(void)
{
    return &fb_list;
}

void fb_regiser(const char *name, struct fb_dev *fbdev, struct fb_ops *ops)
{
    mutex_lock(&lock);
    struct fb_handle *data = malloc(sizeof(struct fb_handle));

    data->fbdev = fbdev;
    data->name = name;
    data->ops = ops;

    list_add_tail(&data->node, &fb_list);

    mutex_unlock(&lock);
}

struct fb_handle *fb_open(const char *name)
{
    mutex_lock(&lock);

    struct list_head *pos;

    list_for_each(pos, &fb_list) {
        struct fb_handle *handle = list_entry(pos, struct fb_handle, node);

        if(!strcmp(handle->name, name)) {
            mutex_unlock(&lock);
            return handle;
        }
    }

    mutex_unlock(&lock);

    return NULL;
}

void fb_enable(struct fb_handle *handle)
{
    handle->ops->fb_enable(handle->fbdev);
}

void fb_disable(struct fb_handle *handle)
{
    handle->ops->fb_disable(handle->fbdev);
}

int fb_is_enable(struct fb_handle *handle)
{
    return handle->ops->fb_is_enable(handle->fbdev);
}

void fb_get_info(struct fb_handle *handle, struct fb_info *info)
{
    handle->ops->fb_get_info(handle->fbdev, info);
}

int fb_set_config(struct fb_handle *handle, struct lcdc_layer *cfg)
{
    return handle->ops->fb_set_config(handle->fbdev, cfg);
}

void fb_disable_config(struct fb_handle *handle)
{
    handle->ops->fb_disable_config(handle->fbdev);
}

void fb_enable_config(struct fb_handle *handle)
{
    handle->ops->fb_enable_config(handle->fbdev);
}

void fb_pan_display(struct fb_handle *handle, unsigned int frame_index)
{
    handle->ops->fb_pan_display(handle->fbdev, frame_index);
}

void fb_hareware_lock(struct fb_handle *handle)
{
    if (handle->ops->fb_hareware_lock)
        handle->ops->fb_hareware_lock(handle->fbdev);
}

void fb_hareware_unlock(struct fb_handle *handle)
{
    if (handle->ops->fb_hareware_unlock)
        handle->ops->fb_hareware_unlock(handle->fbdev);
}

unsigned int fb_bytes_per_pixel(enum fb_fmt fb_fmt)
{
    switch (fb_fmt) {
    case fb_fmt_RGB888:
    case fb_fmt_ARGB8888:
        return 4;
    case fb_fmt_RGB555:
    case fb_fmt_RGB565:
        return 2;
    case fb_fmt_NV12:
    case fb_fmt_NV21:
        return 2;
    default:
        return 2;
    }
}

unsigned int fb_bits_per_pixel(enum fb_fmt fb_fmt)
{
    switch (fb_fmt) {
    case fb_fmt_RGB888:
        return 24;
    case fb_fmt_ARGB8888:
        return 32;
    case fb_fmt_RGB555:
        return 15;
    case fb_fmt_RGB565:
        return 16;
    case fb_fmt_NV12:
    case fb_fmt_NV21:
        return 16;
    default:
        return 16;
    }
}

void fb_export_config(void)
{
    fb_export_cfg_for_linux();
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(fb_init);
EXPORT_SYMBOL(fb_open);
EXPORT_SYMBOL(fb_get_device_list);
EXPORT_SYMBOL(fb_set_config);
EXPORT_SYMBOL(fb_enable_config);
EXPORT_SYMBOL(fb_disable_config);
EXPORT_SYMBOL(fb_enable);
EXPORT_SYMBOL(fb_disable);
EXPORT_SYMBOL(fb_is_enable);
EXPORT_SYMBOL(fb_get_info);
EXPORT_SYMBOL(fb_pan_display);
EXPORT_SYMBOL(fb_hareware_lock);
EXPORT_SYMBOL(fb_hareware_unlock);
EXPORT_SYMBOL(fb_bytes_per_pixel);
EXPORT_SYMBOL(fb_bits_per_pixel);