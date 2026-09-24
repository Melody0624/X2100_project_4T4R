#ifndef _INPUT_H_
#define _INPUT_H_

#include <driver/input_key.h>

struct input_event {
    int code;
    union
    {
        int value;
        struct {
            int x;
            int y;
        } pos;
    };
    int id;

    unsigned long int time_stamp;
};

struct input_dev;
struct input_handle;

struct input_handle *input_open(char *device_name);
int input_read(struct input_handle *handle, struct input_event *event, int timeout_ms);
int input_close(struct input_handle *handle);

struct input_dev *input_device_alloc(char *device_name, int max_event_count);
void input_device_free(struct input_dev *device);
int intput_event_report(struct input_dev *device, struct input_event *event);

#endif