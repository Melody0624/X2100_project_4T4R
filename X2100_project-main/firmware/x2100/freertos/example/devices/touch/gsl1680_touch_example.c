#include <errno.h>
#include <driver/input.h>
#include <devices/gsl1680_touch.h>


void touch_gsl1680_test(void)
{
    int ret;
    struct input_handle *handle;
    struct input_event event;

    struct gls1680_dev gsl1680;
    /* 初始化gsl1680触摸屏 */
    gls1680_touch_init(&gsl1680);

    /* 开启gsl1680触摸屏 */
    handle = input_open("gsl1680");

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
    gsl1680_touch_deinit(&gsl1680);

    return;
}