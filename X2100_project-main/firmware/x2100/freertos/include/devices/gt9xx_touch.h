#ifndef _GT9XX_TOUCH_H_
#define _GT9XX_TOUCH_H_

#include <os/thread_waiter.h>

struct goodix_ts_data {
    int init_flag;
    struct i2c_device *device;
    struct input_dev  *input;
    thread_waiter_t waiter;
    struct touch_data *data;
};

void goodix_touch_init(struct goodix_ts_data *data);
void goodix_touch_deinit(struct goodix_ts_data *data);

#endif