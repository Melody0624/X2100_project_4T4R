#include <stdio.h>
#include <common.h>
#include <os.h>
#include <usb/host_ch341.h>

void ch341_notify_callback(u8 id, u8 *data, int len)
{
    u8 status;
    u8 type = data[0];
    u8 handled = 0;

    if (len < 4)
        return;

    if (type & CH341_CTT_M) {
        status = ~data[2] & CH341_CTI_ST;

        printf("[%d] control lines: cts%c dsr%c ring%c dcd%c\n",
            id, status & CH341_CTI_C ? '+' : '-',	status & CH341_CTI_DS ? '+' : '-',
            status & CH341_CTRL_RI ? '+' : '-',	status & CH341_CTI_DC  ? '+' : '-');
        handled = 1;
    }

    if (type & CH341_CTT_O)
        handled = 1;

    if ((type & CH341_CTT_F) == CH341_CTT_F) {
        handled = 1;
    } else if (type & CH341_CTT_P) {
        handled = 1;
    }

    if (!handled)
        printf("%s - unknown status received:len:%d, data0:0x%x, data1:0x%x\n",
            __func__, (int)len, data[0], data[1]);
}

static char *ch341_write_buf = "ch341 tx\n";
static void ch341_write_thread(void *data)
{
    int ret;
    int id = (int)data;
    int len = strlen(ch341_write_buf);

    while (1)
    {
        ret = usb_host_ch341_write(id, ch341_write_buf, len, -1);
        if (ret < 0) {
            printf("[%d] ch341_write fail %d\n", id, ret);
            break;
        }

        if (ret != len)
            printf("[%d] ch341_write ret(%d) != len\n", id, ret);

        msleep(1000);
    }
    
}

static unsigned char ch341_read_buf[128];
static void ch341_read_thread(void *data)
{
    int i;
    int ret;
    int id = (int)data;

    while (1)
    {
        ret = usb_host_ch341_read(id, ch341_read_buf, sizeof(ch341_read_buf), -1);
        if (ret < 0) {
            printf("[%d] ch341_read fail %d\n", id, ret);
            break;
        } else {
            printf("[%d] ch341_read %d: ", id, ret);
            for (i = 0; i < ret; i++)
                printf("%c", ch341_read_buf[i]);
            printf("\n");
        }
    }
}

static u32 ch341_open_bit;
static struct ch341_line_coding ch341_param = {
    .dwDTERate = 115200,
    .bCharFormat = USB_CH341_1_STOP_BITS,
    .bParityType = USB_CH341_NO_PARITY,
    .bDataBits = 8,
    .hardflow = 0,

};

thread_ptr_t write_thread[CH341_MINORS];
thread_ptr_t read_thread[CH341_MINORS];

void ch341_device_callback(u32 devices_bit)
{
    int i;
    int ret;

    for (i = 0; i < CH341_MINORS; i++) {
        /* open device */
        if ((devices_bit & BIT(i)) && !(ch341_open_bit & BIT(i))) {
            ret = usb_host_ch341_open(i, ch341_notify_callback);
            if (ret < 0) {
                printf("[%d] usb_host_ch341_open fail %d\n", i, ret);
                continue;
            }

            printf("[%d] open control lines: cts%c dsr%c ring%c dcd%c\n",
                i, ret & CH341_CTI_C ? '+' : '-',	ret & CH341_CTI_DS ? '+' : '-',
                ret & CH341_CTRL_RI ? '+' : '-',	ret & CH341_CTI_DC  ? '+' : '-');

            ret = usb_host_ch341_set_line_coding(i, &ch341_param);
            if (ret < 0) {
                usb_host_ch341_close(i);
                printf("[%d] usb_host_ch341_set_line_coding fail %d\n", i, ret);
                continue;
            }

            ret = usb_host_ch341_set_control(i, CH341_CTO_D | CH341_CTO_R);
            if (ret < 0) {
                usb_host_ch341_close(i);
                printf("[%d] usb_host_ch341_set_control fail %d\n", i, ret);
                continue;
            }

            write_thread[i] = thread_create("ch341_write_thread", 1024, ch341_write_thread, (void *)i);
            read_thread[i] = thread_create("ch341_read_thread", 1024, ch341_read_thread, (void *)i);
            ch341_open_bit |= BIT(i);
        }

        /* close device */
        if (!(devices_bit & BIT(i)) && (ch341_open_bit & BIT(i))) {
            thread_join(write_thread[i], NULL);
            thread_join(read_thread[i], NULL);
            usb_host_ch341_close(i);
            ch341_open_bit &= ~BIT(i);
        }
    }
}


void ch341_test(void)
{
    usb_host_ch341_register_callback(ch341_device_callback);
}
