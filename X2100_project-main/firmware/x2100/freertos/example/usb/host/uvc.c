#include <stdio.h>
#include <common.h>
#include <os.h>
#include <little_things.h>

#include <usb/host_uvc.h>

static void uvc_event_streaming(u8 id, struct uvc_status *status, int len)
{
	if (len <= offsetof(struct uvc_status, bEvent))
		return;

	if (status->bEvent == 0) {
		if (len <= offsetof(struct uvc_status, streaming))
			return;

		printf("UVC%d: Button (intf %u) %s len %d\n",
			id, status->bOriginator,
			status->streaming.button ? "pressed" : "released", len);
	}
}

static void uvc_notify_callback(u8 id, struct uvc_status *status, int len)
{
	switch (status->bStatusType & 0x0f) {
	case UVC_STATUS_TYPE_STREAMING: {
		uvc_event_streaming(id, status, len);
		break;
	}

	case UVC_STATUS_TYPE_CONTROL:
		/* todo */
	default:
		break;
	}
}

static void uvc_device_callback(u32 devices_bit)
{
	printf("%s: %#x\n", __func__, devices_bit);
}


void usb_uvc_test(void)
{
	int i;
	int ret;
	u32 uvc_dev;
	u32 frame_size;
	u8 disconnect;
	u8 uvc_dev_index;
	struct uvc_buffer *buf;
	struct uvc_host_video video;
	struct uvc_video_format video_format;
	usb_host_uvc_register_callback(uvc_device_callback);

	while (1) {
		sleep(1);

		uvc_dev = usb_host_uvc_get_devices_bit();
		for (i = 0; i < 32; i++) {
			if (uvc_dev & (1 << i)) {
				break;
			}
		}

		if (i == 32)
			continue;

		uvc_dev_index = i;
		ret = usb_host_uvc_open(uvc_dev_index, uvc_notify_callback);
		if (ret)
			continue;

		printf("uvc%d video num %d\n", uvc_dev_index, usb_host_uvc_get_video_num(uvc_dev_index));

		ret = usb_host_uvc_get_video(uvc_dev_index, 0, &video);
		if (ret) {
			printf("uvc get video fail %d\n", ret);
			goto err_close_uvc;
		}

		sleep(2);

		ret = usb_host_uvc_set_format(uvc_dev_index, &video, 0, 0, 0);
		if (ret) {
			printf("uvc set format fail %d\n", ret);
			goto err_close_uvc;
		}

		ret = usb_host_uvc_get_format(uvc_dev_index, &video, &video_format);
		if (ret) {
			printf("uvc get format fail %d\n", ret);
			goto err_close_uvc;
		}

		if (!video_format.bpp)
			video_format.bpp = 8;

		frame_size = video_format.width * video_format.height * video_format.bpp / 8;

		ret = usb_host_uvc_request_buffer(uvc_dev_index, &video, 3, frame_size);
		if (ret) {
			printf("uvc request buffer fail %d\n", ret);
			goto err_close_uvc;
		}

		ret = usb_host_uvc_stream_on(uvc_dev_index, &video);
		if (ret) {
			printf("uvc stream on fail %d\n", ret);
			goto err_free_buffer;
		}

		disconnect = 0;
		while (1) {
			ret = usb_host_uvc_get_buffer(uvc_dev_index, &video, &buf, -1);
			if (ret) {
				printf("uvc get buffer fail %d\n", ret);
				break;
			}

			printf("uvc buf state %d, size %d\n", buf->state, buf->bytesused);

			if (buf->state == UVC_BUF_STATE_ERROR && buf->bytesused == 0)
				disconnect = 1;

			ret = usb_host_uvc_put_buffer(uvc_dev_index, &video, buf);
			if (ret) {
				printf("uvc put buffer fail %d\n", ret);
				break;
			}
			
			if (disconnect)
				break;
		}

		usb_host_uvc_stream_off(uvc_dev_index, &video);
err_free_buffer:
		usb_host_uvc_free_buffer(uvc_dev_index, &video);
err_close_uvc:
		usb_host_uvc_close(uvc_dev_index);
		printf("uvc%d close\n", uvc_dev_index);
	}

}