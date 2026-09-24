#include <os.h>
#include <common.h>
#include <kernel_symbol.h>
#include <driver/gpio.h>
#include <config.h>
#include <driver/input.h>
#include <errno.h>
#include <little_things.h>
#include <ring_buffer.h>

struct input_dev {
    struct list_head node;
    critical_thread_cond_t wait;
    struct ring_buffer_writer writer;
    char *dev_name;
    struct input_event *keyboard_buf;
    int open_flag;
    int free_flag;
};

struct input_handle {
    struct input_dev *device;
    struct ring_buffer_reader reader;
    int read_flag;
};

static LIST_HEAD(keyboard_list_head);

struct input_dev *input_device_alloc(char *device_name, int max_event_count)
{
    struct list_head *p = NULL;
    struct input_dev *temp = NULL;
    struct input_dev *device = NULL;

    os_enter_critical();

    list_for_each(p, &keyboard_list_head) {
        temp = list_entry(p, struct input_dev, node);
        if (strcmp(device_name, temp->dev_name) == 0)
            panic("error!! %s has been alloc\n",device_name);
    }

    if(max_event_count == 0)
        max_event_count = 64;

    if(max_event_count < 0)
        panic("error!! max_event_count cannot be less than zero\n");

    device = malloc(sizeof(*device) +
                    sizeof(struct input_event) * max_event_count +
                    strlen(device_name) + 1);

    device->keyboard_buf = (struct input_event *)&device[1];
    device->dev_name = (char*)&device->keyboard_buf[max_event_count];

    device->open_flag = 0;
    device->free_flag = 0;
    strcpy(device->dev_name, device_name);
    critical_thread_cond_init(&device->wait);
    ring_buffer_writer_init(&device->writer, device->keyboard_buf, sizeof(struct input_event) * max_event_count);

    list_add_tail(&device->node, &keyboard_list_head);

    os_exit_critical();

    return device;
}

void input_device_free(struct input_dev *device)
{
    os_enter_critical();

    device->free_flag = 1;

    list_del(&device->node);

    if (device->open_flag != 0) {
        critical_thread_cond_broadcast(&device->wait);
        goto unlock;
    }

    free(device);

unlock:
    os_exit_critical();

    return;
}

int intput_event_report(struct input_dev *device, struct input_event *event)
{
    int ret;

    os_enter_critical();

    if(device->free_flag == 1) {
        ret = -ENODEV;
        goto unlock;
    }

    event->time_stamp = systick_get_time_us();

    ring_buffer_write(&device->writer, event, sizeof(*event));

    critical_thread_cond_broadcast(&device->wait);

unlock:
    os_exit_critical();

    return ret;
}

struct input_handle *input_open(char *device_name)
{
    struct list_head *p = NULL;
    struct input_dev *temp = NULL;
    struct input_handle *handle = NULL;

    os_enter_critical();

    list_for_each(p, &keyboard_list_head) {
        temp = list_entry(p, struct input_dev, node);
        if (strcmp(device_name, temp->dev_name) == 0) {
            handle = malloc(sizeof(struct input_handle));
            handle->read_flag = 0;
            handle->device = temp;
            handle->device->open_flag++;
            ring_buffer_reader_init(&handle->reader, &handle->device->writer);
            break;
        }
    }

    os_exit_critical();

    return handle;
}

int input_read(struct input_handle *handle, struct input_event *event, int timeout_ms)
{
    int ret;

    assert(handle);

    os_enter_critical();

    handle->read_flag = 1;

    if(handle->device->free_flag == 1) {
        ret = -ENODEV;
        goto unlock;
    }

    if (ring_buffer_used_size(&handle->reader) < sizeof(*event))
        critical_thread_cond_wait_timeout(&handle->device->wait, timeout_ms);

    if (ring_buffer_used_size(&handle->reader) < sizeof(*event)) {
        ret = -ETIMEDOUT;
        goto unlock;
     }

    ring_buffer_read(&handle->reader, event, sizeof(*event));
    ret = sizeof(*event);

unlock:
    handle->read_flag = 0;

    os_exit_critical();

    return ret;
}

int input_close(struct input_handle *handle)
{
    int ret = 0;

    assert(handle);

    os_enter_critical();

    if (handle->read_flag == 1) {
        ret = -ENODEV;
        goto unlock;
    }

    handle->device->open_flag--;

    if ((handle->device->free_flag == 1) && (handle->device->open_flag == 0))
        input_device_free(handle->device);

    free(handle);

unlock:
    os_exit_critical();

    return ret;
}