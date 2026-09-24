#include <stdio.h>
#include <os.h>
#include <devices/gpio_keyboard.h>
#include <driver/input.h>
#include <errno.h>
#include <common.h>
#include <time.h>
#include <stdint.h>
#include "iscan_ui/iscan_ui.h"
#include <driver/gpio.h>
#include <filesystem/filesystem_hotplug.h>
#include <dfs_fs.h>

#define SCAN_IMG_PATH "/mmcblk0p0/DCIM/100MEDIA/"

void iscan_input_listen_thread(void *data)
{
    int ret;
    struct input_handle *handle;
    struct input_event event;

    /* 开启独立按键 */
    handle = input_open("gpio_keyboard");

    while (1) {
        ret = input_read(handle, &event, 5000);

        if (ret == -ETIMEDOUT) {
            continue;
        } else if (ret < 0) {
            printf("read key failure ret == %d", ret);
            break;
        }

        iscan_ui_key_clicked(event.code, event.value);

    }

}

void iscan_get_usb_insert_cb(int *is_insert)
{
    *is_insert = gadget_get_usb_insert();
}

void iscan_get_cal_status(int *status)
{
    static int cnt = 0;
    cnt++;

    if (cnt == 10) {
        *status = 1;
        cnt = 0;
    }

    return;
}


void iscan_get_battery(enum iscan_bat_status *status)
{

}

void iscan_delete_file(char *file_path)
{
    unlink(file_path);
}



void iscan_format_sdcard(void)
{
    file_system_remove_partition("mmcblk0p0");
    dfs_mkfs("elm", "mmcblk0p0", 0);
    file_system_insert_partition("mmcblk0p0");
}

struct iscan_ui_callback scan_callback = {
    .get_usb_insert = iscan_get_usb_insert_cb,
    .get_cal_status = iscan_get_cal_status,
    .get_battery = iscan_get_battery,
    .delete_file = iscan_delete_file,
    .format_sdcard = iscan_format_sdcard,
};

void vendor_init(void *arg)
{
    iscan_ui_init(&scan_callback, SCAN_IMG_PATH);

    // gadget_usb_card_storage_init();

    thread_create("input_listen_thread", 32 * 1024, iscan_input_listen_thread, NULL);
}