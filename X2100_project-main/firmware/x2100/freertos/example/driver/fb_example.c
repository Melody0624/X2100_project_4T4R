#include <driver/fb.h>
#include <os.h>

void fb_test(void)
{
    struct fb_info fb_info;
    struct fb_handle *fb;


    fb = fb_open("fb0");

    if (fb == NULL) {
        printf("open fb0 error!\n");
        return;
    }

    fb_enable(fb);
    fb_get_info(fb, &fb_info);

    if (fb_info.fb_fmt != fb_fmt_RGB888 && fb_info.fb_fmt != fb_fmt_ARGB8888 ) {
        printf("fb_test: this just support rgb888! you should change this demo\n");
        return;
    }

    int i, j;
    unsigned int *p = fb_info.fb_mem;

    for (j = 0; j < fb_info.yres; j++) {
        for (i = 0; i < fb_info.xres; i++) {
            *p++ = 0xffff0000;
        }
    }

    fb_pan_display(fb, 0);
    msleep(300);

    p = fb_info.fb_mem;
    for (j = 0; j < fb_info.yres; j++) {
        for (i = 0; i < fb_info.xres; i++) {
            *p++ = 0xff00ff00;
        }
    }

    fb_pan_display(fb, 0);
    msleep(300);

    p = fb_info.fb_mem;
    for (j = 0; j < fb_info.yres; j++) {
        for (i = 0; i < fb_info.xres; i++) {
            *p++ = 0xff0000ff;
        }
    }

    fb_pan_display(fb, 0);
    msleep(300);
}