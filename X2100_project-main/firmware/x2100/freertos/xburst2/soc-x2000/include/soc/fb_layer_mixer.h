#ifndef _FB_LAYER_MIXER_H_
#define _FB_LAYER_MIXER_H_

#include <driver/fb.h>

struct fb_layer_mixer_dev;

struct fb_layer_mixer_output_cfg {
    int xres;
    int yres;
    enum fb_fmt format;
    void *dst_mem;
};

void fb_layer_mixer_init(void);

struct fb_layer_mixer_dev *fb_layer_mixer_create(void);
void fb_layer_mixer_delete(struct fb_layer_mixer_dev *mixer);
void fb_layer_mixer_config_layer(struct fb_layer_mixer_dev *mixer, int layer_id, struct lcdc_layer *cfg);
void fb_layer_mixer_enable_layer(struct fb_layer_mixer_dev *mixer, unsigned int layer_id, int enable);

void fb_layer_mixer_set_output_frame(struct fb_layer_mixer_dev *mixer, struct fb_layer_mixer_output_cfg *mixer_cfg);
void fb_layer_mixer_work_out_one_frame(struct fb_layer_mixer_dev *mixer);

#endif