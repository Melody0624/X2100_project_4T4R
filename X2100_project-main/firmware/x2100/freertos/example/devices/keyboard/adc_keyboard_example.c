#include <stdio.h>
#include <driver/input.h>
#include <os.h>
#include <devices/adc_keyboard.h>

void adc_keyboard_test(void)
{
    int ret;
    struct input_event adc_key;
    struct input_handle *handle;

/*将adc按键注册到输入子系统，在init.c中已调用*/
    //adc_keyboard_init();

    handle = input_open("adc_keyboard");
    while (1) {
        ret = input_read(handle, &adc_key, 1000);
        if (ret < 0) {
            printf("read adc key timeout!!\n");
        } else {
            printf("read_adc_key.code = %d %s\n",adc_key.code, adc_key.value ? "pressed" : "released");
        }
    }
    input_close(handle);

/*将adc按键从输入字系统中注销*/
    //adc_keyboard_deinit();

    return;
}
