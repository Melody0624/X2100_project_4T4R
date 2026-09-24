#include <stdio.h>
#include <common.h>
#include <os.h>
#include <usb/host_cdc_acm.h>
#include <usb/cdc.h>

void cdc_acm_notify_callback(u8 id, void *buf, int length)
{
    struct usb_cdc_notification *dr = buf;
	unsigned char *data;
	u32 newctrl;

    data = (unsigned char *)(dr + 1);
	switch (dr->bNotificationType) {
		case USB_CDC_NOTIFY_NETWORK_CONNECTION:
			printf("[%d] %s network\n", id, dr->wValue ? "connected to" : "disconnected from");
			break;

		case USB_CDC_NOTIFY_RINGDETECT:
			printf("[%d] ring\n", id);
			break;

		case USB_CDC_NOTIFY_SERIAL_STATE:
			newctrl = *(u16 *)data;

			printf("[%d] control lines: dcd%c dsr%c break%c ring%c framing%c parity%c overrun%c\n",
				id, newctrl & ACM_CTRL_DCD ? '+' : '-',	newctrl & ACM_CTRL_DSR ? '+' : '-',
				newctrl & ACM_CTRL_BRK ? '+' : '-',	newctrl & ACM_CTRL_RI  ? '+' : '-',
				newctrl & ACM_CTRL_FRAMING ? '+' : '-',	newctrl & ACM_CTRL_PARITY ? '+' : '-',
				newctrl & ACM_CTRL_OVERRUN ? '+' : '-');
			break;

		default:
			printf("[%d] unknown notification %d received: index %d len %d data0 %d data1 %d\n",
				id, dr->bNotificationType, dr->wIndex,
				dr->wLength, data[0], data[1]);
			break;
	}
}

static char *cdc_acm_write_buf = "cdc acm tx\n";
static void cdc_acm_write_thread(void *data)
{
    int ret;
    int id = (int)data;
    int len = strlen(cdc_acm_write_buf);

    while (1)
    {
        ret = usb_host_cdc_acm_write(id, cdc_acm_write_buf, len, -1);
        if (ret < 0) {
            printf("[%d] cdc_acm_write fail %d\n", id, ret);
            break;
        }

        if (ret != len)
            printf("[%d] cdc_acm_write ret(%d) != len\n", id, ret);

        msleep(1000);
    }
    
}

static unsigned char cdc_acm_read_buf[1024];
static void cdc_acm_read_thread(void *data)
{
    int i;
    int ret;
    int id = (int)data;

    while (1)
    {
        ret = usb_host_cdc_acm_read(id, cdc_acm_read_buf, sizeof(cdc_acm_read_buf), -1);
        if (ret < 0) {
            printf("[%d] cdc_acm_read fail %d\n", id, ret);
            break;
        } else {
            printf("[%d] cdc_acm_read %d: ", id, ret);
            for (i = 0; i < ret; i++)
                printf("%c", cdc_acm_read_buf[i]);
            printf("\n");
        }
    }
}

static u32 cdc_acm_open_bit;
static struct cdc_acm_line_coding cdc_acm_param = {
    .dwDTERate = 115200,
    .bCharFormat = USB_CDC_1_STOP_BITS,
    .bParityType = USB_CDC_NO_PARITY,
    .bDataBits = 8,
};

static thread_ptr_t cdc_acm_write_threads[ACM_MINORS];
static thread_ptr_t cdc_acm_read_threads[ACM_MINORS];

void cdc_acm_device_callback(u32 devices_bit)
{
    int i;
    int ret;

    for (i = 0; i < ACM_MINORS; i++) {
        /* open device */
        if ((devices_bit & BIT(i)) && !(cdc_acm_open_bit & BIT(i))) {
            ret = usb_host_cdc_acm_open(i, cdc_acm_notify_callback);
            if (ret) {
                printf("[%d] usb_host_cdc_acm_open fail %d\n", i, ret);
                continue;
            }

            ret = usb_host_cdc_acm_set_line_coding(i, &cdc_acm_param, -1);
            if (ret) {
                usb_host_cdc_acm_close(i);
                printf("[%d] usb_host_cdc_acm_set_line_coding fail %d\n", i, ret);
                continue;
            }

            ret = usb_host_cdc_acm_set_control(i, ACM_CTRL_DTR | ACM_CTRL_RTS, -1);
            if (ret) {
                usb_host_cdc_acm_close(i);
                printf("[%d] usb_host_cdc_acm_set_control fail %d\n", i, ret);
                continue;
            }

            cdc_acm_write_threads[i] = thread_create("cdc_acm_write_thread", 1024, cdc_acm_write_thread, (void *)i);
            cdc_acm_read_threads[i] = thread_create("cdc_acm_read_thread", 1024, cdc_acm_read_thread, (void *)i);
            cdc_acm_open_bit |= BIT(i);
        }

        /* close device */
        if (!(devices_bit & BIT(i)) && (cdc_acm_open_bit & BIT(i))) {
            thread_join(cdc_acm_write_threads[i], NULL);
            thread_join(cdc_acm_read_threads[i], NULL);
            usb_host_cdc_acm_close(i);
            cdc_acm_open_bit &= ~BIT(i);
        }
    }
}


void cdc_acm_test(void)
{
    usb_host_cdc_acm_register_callback(cdc_acm_device_callback);
}
