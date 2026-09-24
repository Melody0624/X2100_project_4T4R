src-$(CONFIG_USB_DRIVER) += core.c core_intr.c platform.c params.c common.c utf8.c workqueue.c

src-$(CONFIG_USB_IS_DEVICE) += gadget.c \
		gadget/usbstring.c gadget/config.c \
		gadget/epautoconf.c gadget/composite.c \
		gadget/core.c

src-$(CONFIG_USB_IS_HOST) += hcd.c hcd_intr.c hcd_queue.c \
		host/config.c host/hcd.c host/hub.c host/message.c host/urb.c host/usb.c

src-$(CONFIG_USB_HOST_HID_MOUSE) += host/hid/usbmouse.c

src-$(CONFIG_USB_HOST_HID_KEYBOARD) += host/hid/usbkbd.c

src-$(CONFIG_USB_HOST_HID_VENDOR) += host/hid/usb_hid_vendor.c

src-$(CONFIG_USB_HOST_CDC_ACM) += host/cdc-acm/cdc-acm.c

src-$(CONFIG_USB_HOST_CH341) += host/ch341/ch341.c

src-$(CONFIG_USB_HOST_WWAN) += host/wwan/wwan.c

src-$(CONFIG_USB_HOST_UVC) += host/uvc/uvc.c host/uvc/uvc_driver.c host/uvc/uvc_queue.c host/uvc/uvc_status.c host/uvc/uvc_video.c

src-$(CONFIG_USB_HOST_UAC) += host/uac/uac_driver.c host/uac/pcm.c host/uac/endpoint.c host/uac/helper.c host/uac/uac.c

src-$(CONFIG_USB_HOST_MASS_STORAGE) += host/storage/protocol.c host/storage/scsiglue.c host/storage/storage.c host/storage/transport.c
src-$(CONFIG_USB_HOST_MASS_STORAGE_AUTO_MOUNT) += host/storage/partition/partition.c host/storage/partition/efi.c host/storage/partition/msdos.c

src-$(CONFIG_USB_GADGET_SERIAL) += gadget/serial/serial.c gadget/serial/u_serial.c gadget/serial/f_acm.c

src-$(CONFIG_USB_GADGET_HID) += gadget/hid/hid.c gadget/hid/hid2.c gadget/hid/f_hid.c

src-$(CONFIG_USB_GADGET_SERIAL_HID) += gadget/cdc_hid/serial_hid.c gadget/cdc_hid/u_serial.c \
		gadget/cdc_hid/f_acm.c gadget/cdc_hid/f_hid.c

src-$(CONFIG_USB_GADGET_UVC) += gadget/uvc/webcam.c gadget/uvc/f_uvc.c gadget/uvc/uvc_video.c

src-$(CONFIG_USB_GADGET_UVC_N) += gadget/uvc_n/webcam.c gadget/uvc_n/f_uvc.c gadget/uvc_n/uvc_video.c

src-$(CONFIG_USB_GADGET_UVC_HID) += gadget/uvc_hid/webcam_hid.c gadget/uvc_hid/f_uvc.c gadget/uvc_hid/uvc_video.c gadget/uvc_hid/f_hid.c

src-$(CONFIG_USB_GADGET_BULK) += gadget/generic_bulk/generic_bulk.c gadget/generic_bulk/f_bulk.c

src-$(CONFIG_USB_GADGET_UAC1) += gadget/uac1/audio.c gadget/uac1/f_uac1.c gadget/uac1/u_audio.c

src-$(CONFIG_USB_GADGET_UVC_CDC) += gadget/uvc_cdc/webcam_cdc.c gadget/uvc_cdc/f_uvc.c gadget/uvc_cdc/uvc_video.c \
		gadget/uvc_cdc/u_serial.c gadget/uvc_cdc/f_acm.c

src-$(CONFIG_USB_GADGET_UVC_UAC1) += gadget/uvc_uac1/webcam_uac1.c gadget/uvc_uac1/f_uvc.c gadget/uvc_uac1/uvc_video.c \
		gadget/uvc_uac1/f_uac1.c gadget/uvc_uac1/u_audio.c

src-$(CONFIG_USB_GADGET_UVC_N_UAC1) += gadget/uvc_n_uac1/webcam_uac1.c gadget/uvc_n_uac1/f_uvc.c gadget/uvc_n_uac1/uvc_video.c \
		gadget/uvc_n_uac1/f_uac1.c gadget/uvc_n_uac1/u_audio.c

src-$(CONFIG_USB_GADGET_HID_WAKEUP) += gadget/hid_wakeup/hid.c gadget/hid_wakeup/f_hid.c

src-$(CONFIG_USB_GADGET_MASS_STORAGE) += gadget/mass_storage/mass_storage.c gadget/mass_storage/f_mass_storage.c

src-$(CONFIG_USB_GADGET_PRINTER) += gadget/printer/printer.c gadget/printer/f_printer.c

src-$(CONFIG_USB_GADGET_DISPLAY) += gadget/display/display.c gadget/display/f_display.c

src-$(CONFIG_USB_GADGET_DISPLAY_AUDIO_HID) += gadget/display_uac1_hid/display_uac1_hid.c gadget/display_uac1_hid/f_display.c \
		gadget/display_uac1_hid/f_hid.c gadget/display_uac1_hid/f_uac1.c gadget/display_uac1_hid/u_audio.c

src-$(CONFIG_USB_GADGET_RNDIS) +=  gadget/rndis/f_rndis.c gadget/rndis/rndis.c  gadget/rndis/u_ether.c gadget/rndis/ether.c

src-$(CONFIG_USB_GADGET_NCM) +=  gadget/ncm/f_ncm.c gadget/ncm/ncm.c gadget/ncm/u_ether.c

src-$(CONFIG_USB_GADGET_ECM) +=  gadget/ecm/f_ecm.c gadget/ecm/ecm.c gadget/ecm/u_ether.c

src-$(CONFIG_USB_GADGET_APPLE) += gadget/apple/f_nero.c gadget/apple/apple.c

src-$(CONFIG_USB_GADGET_WINUSB) += gadget/winusb/winusb.c gadget/winusb/f_winusb.c

