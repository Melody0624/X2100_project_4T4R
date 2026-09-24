#include <driver/pwm.h>
#include <driver/gpio.h>
#include <os.h>
#include <common.h>
#include <driver/backlight.h>
#include <little_things.h>

static LIST_HEAD(backlight_list_head);

void backlight_register(struct backlight *backlight)
{
    struct list_head *p = NULL;
    struct backlight *temp = NULL;

    os_enter_critical();

    if (backlight == NULL)
        panic("error!! backlight is NULL");

    if (backlight->backlight_set_brightness == NULL)
        panic("error!! backlight_set_brightness not define\n");

    if (backlight->max_brightness < 1)
        panic("error!! backlight_max_brightness less than 1\n");

    if (backlight->name == NULL)
        panic("error!! backlight_name is NULL\n");

    list_for_each(p, &backlight_list_head) {
        temp = list_entry(p, struct backlight, node);
        if (strcmp(backlight->name, temp->name) == 0)
            panic("error!! %s has been alloc\n",backlight->name);
    }

    backlight->current_brightness = 0;

    mutex_init(&backlight->mutex);

    list_add_tail(&backlight->node, &backlight_list_head);

    os_exit_critical();
}

void backlight_unregister(struct backlight *backlight)
{
    os_enter_critical();

    list_del(&backlight->node);

    os_exit_critical();
}

struct backlight *backlight_open(const char *name)
{
    struct list_head *p = NULL;
    struct backlight *backlight = NULL;
    struct backlight *temp = NULL;

    os_enter_critical();

    list_for_each(p, &backlight_list_head) {
        temp = list_entry(p, struct backlight, node);
        if (strcmp(name, temp->name) == 0)
            backlight = temp;
    }

    os_exit_critical();

    return backlight;
}

int backlight_get_brightness(struct backlight *backlight)
{
    return backlight->current_brightness;
}

int backlight_get_maxbrightness(struct backlight *backlight)
{
    return backlight->max_brightness;
}

void backlight_set_brightness(struct backlight *backlight, int brightness)
{
    mutex_lock(&backlight->mutex);

    if (brightness > backlight->max_brightness) {
        printf("set %s backlight fail!! can not set more than %lu\n", backlight->name, backlight->max_brightness);

        mutex_unlock(&backlight->mutex);
        return;
    }

    backlight->current_brightness = brightness;
    backlight->backlight_set_brightness(backlight, brightness);

    mutex_unlock(&backlight->mutex);
}