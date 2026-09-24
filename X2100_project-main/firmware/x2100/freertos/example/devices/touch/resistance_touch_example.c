#include <errno.h>
#include <os/thread.h>
#include <driver/gpio.h>
#include <driver/input.h>
#include <devices/resistance_touch.h>

#define MODIFY_FILTER_PARAMS            0

void resistance_touch_test(void)
{
    int ret;
    struct input_event event;
    struct input_handle *handle;
    struct adc_rts_dev *dev = NULL;

    /* 初始化电阻触摸屏参数 */
    struct rts_params params = {
        .dev_name = "rts",
        .delay_ms = 1,
        .x = {
            .adc_channel = 4,          /* x power */
            .adc_max = 3260,
            .adc_min = 750,
            .coords_max = 480,
            .power = GPIO_PE(9),
            .minus = GPIO_PE(5),
        },
        .y = {
            .adc_channel = 1,          /* y minus */
            .adc_max = 3700,
            .adc_min = 370,
            .coords_max = 800,
            .power = GPIO_PE(10),
            .minus = GPIO_PE(6),
        },
        .x_coords_flip = 1,
        .y_coords_flip = 1,
        .x_y_coords_exchange = 1,
    };

    /* 初始化电阻触摸屏 */
    dev = adc_resistance_touch_init(&params);
    if (!dev) {
        printf("init resistance touchscreen failure\n");
        goto err_out;
    }

#if MODIFY_FILTER_PARAMS
    struct rts_filter_params filter = {
        .dither_delay_ms = 10,
        .press_threshold = 3000,
        .valid_range = 125,
        .z1_threshold = 55,
    };

    rts_modify_filter_params(dev, filter);
#endif

    /* 开启电阻触摸屏 */
    handle = input_open(params.dev_name);

    while (1) {
        ret = input_read(handle, &event, 5000);

        if (ret == -ETIMEDOUT) {
            continue;
        } else if (ret < 0) {
            printf("read touch pos failure ret == %d", ret);
            break;
        }

        printf("x == %d   y == %d\n", event.pos.x, event.pos.y);
    }

    input_close(handle);
    adc_resistance_touch_deinit(dev);

err_out:
    return;
}