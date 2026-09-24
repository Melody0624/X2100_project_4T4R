#ifndef _HOST_MASS_STORAGE_H_
#define _HOST_MASS_STORAGE_H_

#include <common.h>
#include <errno.h>
#include <os.h>

#ifdef CONFIG_USB_HOST_MASS_STORAGE

struct usb_storage_device {
    void *user_data;
    unsigned int block_size;
    unsigned long long block_count;

#define USB_STORAGE_INQUIRY_VENDOR_LEN 8
    char vendor[USB_STORAGE_INQUIRY_VENDOR_LEN + 1];
#define USB_STORAGE_INQUIRY_PRODUCT_LEN 16
    char product[USB_STORAGE_INQUIRY_PRODUCT_LEN + 1];
#define USB_STORAGE_INQUIRY_VERSION_LEN 4
    char version[USB_STORAGE_INQUIRY_VERSION_LEN + 1];

    /* only internal use */
    u8 index;
    void *private_data;
    
#define USB_STORAGE_BIND_FLAG 0x5AA5A55A
    u32 bind_flag;
#define USB_STORAGE_UNBIND_WAIT_TIMEOUT (10 * 1000)
    thread_waiter_t unbind_wait;
};

/* 
 * 解除u盘绑定 
 * 
 * 注意:
 *  解除绑定后,dev设备指针不能使用.
 * 
 * unlink和unbind分开实现的目的:
 *  二次确定不再引用设备指针,解除绑定后设备指针不能使用
 *  unbind 可以在unlink函数外调用.
 *  引入超时机制,尽量防止u盘卸载卡死驱动
 */
extern void usb_host_mass_storage_unbind(struct usb_storage_device *dev);

extern int usb_host_mass_storage_write(struct usb_storage_device *dev, u8 *buffer, u32 sector, u16 count);
extern int usb_host_mass_storage_read(struct usb_storage_device *dev, u8 *buffer, u32 sector, u16 count);

#if 0
/* 
 * need user to implement
 * int usb_host_mass_storage_link_bind(struct usb_storage_device *dev)
 * void usb_host_mass_storage_unlink(struct usb_storage_device *dev)
 *
 * example:
 */
static inline int usb_host_mass_storage_link_bind(struct usb_storage_device *dev)
{
    int i;
    int result;
    u8 *buf = malloc(dev->block_size);
    if (!buf)
        return -ENOMEM;

    result = usb_host_mass_storage_read(dev, buf, 0, 1);
    if (result)
        return result;

    printf("lun %d: sector0 data:\n", dev->index);
    for (i = 0; i < dev->block_size; i++) {
        if (!(i % 16))
            printf("%08x", i);

        printf("  %02x", buf[i]);

        if (!((i + 1) % 16))
            printf("\n");
    }
    printf("\n");

    free(buf);

    return 0;
}
static inline void usb_host_mass_storage_unlink(struct usb_storage_device *dev)
{
    usb_host_mass_storage_unbind(dev);
}
#else
/* user to implement the function */

/* 
 * 需要用户实现的函数
 * 如果u盘插入,会调用 usb_host_mass_storage_link_bind
 * 返回值: 0链接并绑定成功 -1失败
 * 
 * 注意: 如果绑定成功,当断开链接时需要手动解除绑定
 */
int usb_host_mass_storage_link_bind(struct usb_storage_device *dev);

/* 
 * 需要用户实现的函数
 * 如果u盘拔出,会调用 usb_host_mass_storage_unlink
 * 
 * 通知应用程序u盘已经拔出,需要停止通讯并手动解除绑定
 * 
 * 注意: 如果在限制时间USB_STORAGE_UNBIND_WAIT_TIMEOUT内没有解除绑定,
 * 驱动将认为用户忘记解除绑定,强制解除绑定.
 * 强制解除绑定后,如果用户继续操作dev,驱动将异常处理
 * 
 * 如果默认的限制时间不满足使用场景,自行修改USB_STORAGE_UNBIND_WAIT_TIMEOUT
 */
void usb_host_mass_storage_unlink(struct usb_storage_device *dev);
#endif

#endif

#endif /* _HOST_MASS_STORAGE_H_ */
