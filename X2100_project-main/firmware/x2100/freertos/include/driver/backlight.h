#ifndef _BACKLIGHT_H
#define _BACKLIGHT_H

#include <os.h>

struct backlight {
    const char *name;
    void (*backlight_set_brightness)(struct backlight *backlight, int brightness);
    unsigned long  max_brightness;
    int current_brightness;
    struct list_head node;
    struct mutex mutex;
};

void backlight_register(struct backlight *backlight);
void backlight_unregister(struct backlight *backlight);
struct backlight *backlight_open(const char *name);
void backlight_set_brightness(struct backlight *backlight, int brightness);
int backlight_get_brightness(struct backlight *backlight);
int backlight_get_maxbrightness(struct backlight *backlight);

#endif