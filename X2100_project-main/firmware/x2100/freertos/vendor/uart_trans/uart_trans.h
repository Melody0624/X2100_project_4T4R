#ifndef __UART_TRANS_H__
#define __UART_TRANS_H__

#include "mmw_msg_pkt.h"

/**
 * @brief 启动UART
 * @param config UART配置结构体指针
 * @return 成功返回0，失败返回错误码
 */
int uart_open(void);

/**
 * @brief 停止UART
 * @param config UART配置结构体指针
 * @return 成功返回0，失败返回错误码
 */
int uart_close(void);

/**
 * @brief 发送原始数据
 * @param buf 数据指针
 * @param size 数据大小
 * @return 成功返回发送字节数，失败返回-1
 */
int uart_general_send(char *buf, int size);

/**
 * @brief 更新已封包好的共享数据 (由主任务统一封包后调用)
 * @param pktBuf 预封包好的数据缓冲区指针
 * @param pktSize 封包数据字节数
 * @param frameID 帧ID
 * @return 成功返回0，失败返回错误码
 */
int uart_frame_pkt_update(Mmw_pkt_info *pktInfo, const char *pktBuf, int32_t pktSize, uint32_t frameID);

/**
 * @brief 按照串口协议发送报警数据
 * @return 成功返回0，失败返回错误码
 */
int uart_protocol_alarm_send(void);

/**
 * @brief 按照32板串口协议VT08.7.3发送预警信息
 * @return 成功返回0，失败返回错误码
 */
int vt_protocol_warn_send(void);

#if (USE_USB_OUTPUT == 1)
void save_frame_pkt_update_proc(void *rawdata, uint32_t len, uint32_t frameID, int pool_index);
#endif

/**
 * @brief 更新预警信息并发送
 */
void update_and_send_warning(void);

/**
 * @brief 设置BSD启动车速阈值
 * @param speed 启动车速（km/h）
 */
void set_ego_speed_threshold(float speed);

/**
 * @brief 设置水平角度校正值
 * @param offset 角度校正值（精度0.1°，范围±5°）
 */
void set_angle_offset(int8_t offset);

/* ======================== OTA 升级传输接口 ======================== */

/**
 * @brief 原始UART发送（绕过USB/CLI路由，直接操作UART3硬件）
 * @param buf 数据缓冲区
 * @param size 数据大小
 * @param timeout_ms 超时时间（毫秒）
 * @return 成功返回发送字节数，失败返回-1
 */
int uart_raw_send(const char *buf, int size, int timeout_ms);

/**
 * @brief 原始UART接收（绕过CLI解析，直接读取UART3硬件）
 * @param buf 接收缓冲区
 * @param size 期望接收字节数
 * @param timeout_ms 超时时间（毫秒）
 * @return 成功返回接收字节数，失败返回-1
 */
int uart_raw_receive(uint8_t *buf, int size, int timeout_ms);

/**
 * @brief 挂起UART3接收任务（OTA升级前调用，防止与OTA协议冲突）
 */
void uart_suspend_recv_task(void);

/**
 * @brief 恢复UART3接收任务（OTA升级完成后调用）
 */
void uart_resume_recv_task(void);

/**
 * @brief 重置UART3接收等待状态（OTA升级挂起任务后调用）
 *
 * 清理 uart_receive_timeout 内部可能持有的 rx_mutex、rx_wait、rx_busy 状态，
 * 防止后续 OTA 操作因 mutex 被挂起任务持有而阻塞。
 */
void uart_reset_read_wait_ab(void);

/**
 * @brief 重置UART3发送等待状态（OTA升级挂起发送线程后调用）
 *
 * 清理 uart_send 内部可能持有的 tx_mutex、tx_wait、tx_busy 状态，
 * 防止后续 OTA 操作因 mutex 被挂起线程持有而阻塞。
 */
void uart_reset_write_wait_ab(void);

#endif /* __UART_TRANS_H__ */
