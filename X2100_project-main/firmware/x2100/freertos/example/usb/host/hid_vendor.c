#include <stdio.h>
#include <common.h>
#include <os.h>
#include <usb/host_hid_vendor.h>

static void hid_vendor_read_thread(void *data)
{
    int i = 0;
    int len = 0;
    int size = 0;
    int id = (int)data;
    unsigned char *read_buf = NULL;

    /* 获取单次通信数据传输单位大小 */
    size = usb_hid_vendor_get_in_size(id);
    if (size <= 0) {
        printf("hid_vendor[%d] usb_hid_vendor_get_in_size fail!\n", id);
        return;
    }

    read_buf = (unsigned char *)malloc(size);
    if (!read_buf) {
        printf("hid_vendor[%d] malloc mem for read_buf fail!\n", id);
        return;
    }

    while (1) {
        len = usb_host_hid_vendor_read(id, read_buf, size, -1);
        if (len < 0) {
            printf("hid_vendor[%d] hid_vendor_read fail %d\n", id, len);
            break;
        }

        if (len != size)
            printf("hid_vendor[%d] hid_vendor_read ret(%d) != len\n", id, len);

        printf("hid vendor[%d] read len[%d] dump data[", id, len);

        for(i = 0; i < len; i++)
            printf("%d ", read_buf[i]);

        printf("]data end.\n");

        msleep(1000);
    }

    free(read_buf);
}

static void hid_vendor_write_thread(void *data)
{
    int i = 0;
    int len = 0;
    int size = 0;
    int id = (int)data;
    unsigned char *write_buf = NULL;

    /* 获取单次通信数据传输单位大小 */
    size = usb_hid_vendor_get_out_size(id);
    if (size <= 0) {
        printf("hid_vendor[%d] usb_hid_vendor_get_out_size fail!\n", id);
        return;
    }

    write_buf = (unsigned char *)malloc(size);
    if (!write_buf) {
        printf("hid_vendor[%d] malloc mem for write_buf fail!\n", id);
        return;
    }

    for(i = 0; i < size; i++)
        write_buf[i] = i+1;

    while (1) {
        len = usb_host_hid_vendor_write(id, write_buf, size, -1);
        if (len < 0) {
            printf("hid_vendor[%d] hid_vendor_write fail %d\n", id, len);
            break;
        }

        if (len != size)
            printf("hid_vendor[%d] hid_vendor_write ret(%d) != len\n", id, len);

        msleep(1000);
    }

    free(write_buf);
}

static u32 hid_vendor_open_bit;
static thread_ptr_t read_thread[HID_VENDOR_MINORS];
static thread_ptr_t write_thread[HID_VENDOR_MINORS];

void hid_vendor_device_callback(u32 devices_bit)
{
    int i = 0;
    int ret = 0;

    for (i = 0; i < HID_VENDOR_MINORS; i++) {
        /* open device */
        if ((devices_bit & BIT(i)) && !(hid_vendor_open_bit & BIT(i))) {
            ret = usb_host_hid_vendor_open(i);
            if (ret) {
                printf("[%d] usb_host_hid_vendor_open fail %d\n", i, ret);
                continue;
            }

            /**
             * 每次接收和发送的数据长度必须是规定的长度, 具体规定的长度大小通过
             * usb_hid_vendor_get_in_size 和 usb_hid_vendor_get_out_size 获取.
             * 建议用户使用时，保证每次接收和发送的数据长度单位为规定的长度.
             */
            read_thread[i] = thread_create("hid_vendor_read_thread", 1024, hid_vendor_read_thread, (void *)i);
            write_thread[i] = thread_create("hid_vendor_write_thread", 1024, hid_vendor_write_thread, (void *)i);
            hid_vendor_open_bit |= BIT(i);
        }

        /* close device */
        if (!(devices_bit & BIT(i)) && (hid_vendor_open_bit & BIT(i))) {
            thread_join(read_thread[i], NULL);
            thread_join(write_thread[i], NULL);
            usb_host_hid_vendor_close(i);
            hid_vendor_open_bit &= ~BIT(i);
        }
    }
}

void hid_vendor_test(void)
{
    usb_host_hid_vendor_register_callback(hid_vendor_device_callback);
}