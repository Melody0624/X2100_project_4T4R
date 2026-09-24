#include <stdio.h>
#include <common.h>
#include <os.h>
#include <include/usb/host_wwan.h>

static u32 wwan_open_bit;
static thread_ptr_t wwan_write_threads[WWAN_MINORS];
static thread_ptr_t wwan_read_threads[WWAN_MINORS];

static char *wwan_write_buf = "AT\r";
static unsigned char wwan_read_buf[128];

void wwan_notify_callback(u8 id, struct usb_wwan_port_private *data) {}

static void wwan_write_thread(void *data)
{
    int ret;
    int id = (int)data;
    int len = strlen(wwan_write_buf);

    while (1)
    {
        ret = usb_host_wwan_write(id, (void *)wwan_write_buf, len, -1);
        if (ret < 0) {
            printf("[%d] wwan_write fail %d\n", id, ret);
            break;
        }

        if (ret != len)
            printf("[%d] wwan_write ret(%d) != len\n", id, ret);

        printf("[%d] wwan_write: AT\n", id);

        msleep(1000 * 5);
    }
}

static void wwan_read_thread(void *data)
{
    int i;
    int ret;
    int id = (int)data;

    while (1)
    {
        ret = usb_host_wwan_read(id, (void *)wwan_read_buf, sizeof(wwan_read_buf), -1);
        if (ret < 0) {
            printf("[%d] wwan_read fail %d\n", id, ret);
            break;
        } else {
            printf("[%d] wwan_read %d: ", id, ret);
            for (i = 0; i < ret; i++)
                printf("%c", wwan_read_buf[i]);
            printf("\n");
        }
    }
}

void wwan_device_callback(u32 devices_bit)
{
    int i;
    int ret;

    for (i = 0; i < WWAN_MINORS; i++) {
        /* open device */
        if ((devices_bit & BIT(i)) && !(wwan_open_bit & BIT(i))) {
            ret = usb_host_wwan_open(i, wwan_notify_callback);
            if (ret < 0)
                printf("[%d] usb_host_wwan_open fail %d\n", i, ret);

            usb_host_wwan_set_dtr_rts(i, 1, 1);

            wwan_write_threads[i] = thread_create("wwan_write_thread", 1024, wwan_write_thread, (void *)i);
            wwan_read_threads[i] = thread_create("wwan_read_thread", 1024, wwan_read_thread, (void *)i);
            wwan_open_bit |= BIT(i);
        }

        /* close device */
        if (!(devices_bit & BIT(i)) && (wwan_open_bit & BIT(i))) {
            thread_join(wwan_write_threads[i], NULL);
            thread_join(wwan_read_threads[i], NULL);
            usb_host_wwan_close(i);
            wwan_open_bit &= ~BIT(i);
        }
    }
}

void wwan_test(void)
{
    usb_host_wwan_register_callback(wwan_device_callback);
}
