#ifndef _FT6262_TOUCH_H_
#define _FT6236_TOUCH_H_

#include <os/thread_waiter.h>

struct ft6236_dev {
    int init_flag;
    struct input_dev *input;
    struct ft6236_read_data *data;
    thread_waiter_t waiter;
};

void ft6236_touch_init(struct ft6236_dev *ft6236);
void ft6236_touch_deinit(struct ft6236_dev *ft6236);

#endif
