#ifndef _GSLX680_TOUCH_H_
#define _GSLX680_TOUCH_H_

struct gls1680_dev {
    int init_flag;
    char *touch_data;
    struct i2c_device i2c;
    struct input_dev *input;
    struct gsl_ts_data *data;
    thread_waiter_t waiter;
};


void gls1680_touch_init(struct gls1680_dev *gsl1680);
void gsl1680_touch_deinit(struct gls1680_dev *gsl1680);

#endif
