#ifndef _GADGET_SERIAL_LOCAL_H_
#define _GADGET_SERIAL_LOCAL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief USB Serial 发送单条指令（线程安全）
 * @param data: 要发送的数据
 * @param len: 数据长度
 * @return 实际发送的字节数，-1 表示参数错误
 */
int usb_serial_send(const unsigned char *data, int len);

/**
 * @brief USB Serial 发送字符串
 * @param str: 要发送的字符串
 * @return 实际发送的字节数，-1 表示参数错误
 */
int usb_serial_send_string(const char *str);

/**
 * @brief 通过 USB Serial 发送 SD 卡数据（非阻塞，使用环形缓冲区）
 * @param data: 要发送的数据（save_data_to_file 需要存入 SD 卡的数据）
 * @param len: 数据长度
 * @return 实际写入环形缓冲区的字节数，-1 表示参数错误，-2 表示缓冲区满
 */
int usb_serial_send_sd_data(const unsigned char *data, int len);

/**
 * @brief 获取环形缓冲区当前状态
 * @param free_space: 输出参数，可用空间字节数
 * @param used_space: 输出参数，已使用空间字节数
 * @return 0 成功，-1 失败
 */
int usb_serial_get_ring_buf_status(uint32_t *free_space, uint32_t *used_space);

/**
 * @brief USB Serial 初始化测试
 * @return 0 成功，-1 失败
 */
int gadget_usb_serial_test(void);

/**
 * @brief 挂起 USB 串口接收任务（用于 OTA 独占）
 */
void gadget_serial_suspend_recv_task(void);

/**
 * @brief 恢复 USB 串口接收任务
 */
void gadget_serial_resume_recv_task(void);

/**
 * @brief 重置 USB 串口 read_wait 状态（用于 OTA 独占）
 *
 * 在挂起 USB 接收任务后调用，清理 gadget_serial_read 内部可能持有的
 * read_wait 状态（thread 指针），防止后续 OTA 升级操作因 assert(!waiter->thread)
 * 断言失败而崩溃。
 */
void gadget_serial_reset_read_wait_ab(void);

/**
 * @brief 重置 USB 串口 write_wait 状态（用于 OTA 独占）
 *
 * 在挂起 USB 发送任务后调用，清理 gadget_serial_write 内部可能持有的
 * write_wait 状态（thread 指针），防止后续 OTA 升级操作因 assert(!waiter->thread)
 * 断言失败而崩溃。
 */
void gadget_serial_reset_write_wait_ab(void);

#ifdef __cplusplus
}
#endif

#endif /* _GADGET_SERIAL_LOCAL_H_ */
