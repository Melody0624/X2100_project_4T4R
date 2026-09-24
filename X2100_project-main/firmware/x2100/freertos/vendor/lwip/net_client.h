#ifndef NET_CLIENT_H
#define NET_CLIENT_H

#include <stdint.h>
#include <os/freertos/include/FreeRTOS.h>
#include <os/freertos/include/queue.h>

/* 返回值定义 */
#define NET_OK              0
#define NET_ERR_PARAM      -1  // 参数错误
#define NET_ERR_TIMEOUT    -2  // 操作超时
#define NET_ERR_CONN       -3  // 连接失败/异常断开
#define NET_ERR_SEND       -4  // 发送失败
#define NET_ERR_STATE      -5  // 状态机不允许当前操作
#define NET_ERR_QUEUE_FULL -6  // 发送队列满
#define NET_ERR_NO_MEM     -7  // 内存分配失败

/* 连接状态 */
typedef enum {
    NET_STATE_DISCONNECTED = 0,
    NET_STATE_CONNECTING,
    NET_STATE_CONNECTED,
    NET_STATE_ERROR
} net_state_t;

/* 发送项结构（内部使用） */
typedef struct {
    uint8_t *data;          // 数据指针，根据need_free决定是否内部拷贝
    uint32_t len;           // 数据长度
    int *result;            // 发送结果指针
    uint32_t timeout_ms;    // 本次发送超时
    uint8_t need_free;      // 是否需要释放资源
} net_send_item_t;

/**
 * @brief 初始化网络模块（创建内部资源+启动发送线程）
 * @param stack_depth  发送线程栈大小（字），建议 2048 ~ 4096
 * @param priority     发送线程优先级
 * @param queue_depth  发送队列深度（建议 8 ~ 32，根据雷达数据频率调整）
 * @return NET_OK 成功
 */
int net_client_init(uint32_t stack_depth, UBaseType_t priority, uint32_t queue_depth);

/**
 * @brief 连接到服务器（阻塞调用，在独立任务中执行）
 * @param ip    服务器IP字符串 (如 "192.168.1.100")
 * @param port  服务器端口
 * @param timeout_ms 连接超时时间(毫秒)
 * @return NET_OK 成功, 其他为错误码
 */
int net_client_connect(const char *ip, uint16_t port, uint32_t timeout_ms);

void net_client_connect_task(void *pv);

/**
 * @brief 异步发送数据（非阻塞，数据会被内部拷贝）
 * @param data        数据指针
 * @param len         数据长度
 * @param frameID     帧ID
 * @param timeout_ms  等待队列可用的超时时间(毫秒)，0表示立即返回
 * @return >=0 表示成功入队(不等于已发送), <0 错误码
 * @note 调用者无需等待发送完成，如需确认结果请使用 net_client_send_sync
 */
int net_client_send_async(const uint8_t *data, uint32_t len, uint32_t frameID, uint32_t timeout_ms);

/**
 * @brief 异步发送数据（非阻塞，数据不拷贝）
 * @param data        数据指针
 * @param len         数据长度
 * @param timeout_ms  等待队列可用的超时时间(毫秒)，0表示立即返回
 * @return >=0 表示成功入队(不等于已发送), <0 错误码
 * @note 调用者无需等待发送完成，如需确认结果请使用 net_client_send_sync
 */
int net_client_send_async_nocopy(const uint8_t *data, uint32_t len, uint32_t timeout_ms);

/**
 * @brief 同步发送数据（阻塞直到发送完成或超时）
 * @param data        数据指针
 * @param len         数据长度
 * @param frameID     帧ID
 * @param timeout_ms  发送总超时时间(毫秒)
 * @return >=0 实际发送字节数(等于len表示成功), <0 错误码
 */
int net_client_send_sync(const uint8_t *data, uint32_t len, uint32_t frameID, uint32_t timeout_ms);

/**
 * @brief 主动断开连接
 * @return NET_OK
 */
int net_client_disconnect(void);

/**
 * @brief 获取当前网络状态
 */
net_state_t net_client_get_state(void);

/**
 * @brief 反初始化，释放资源（确保先断开连接）
 */
void net_client_deinit(void);

/**
 * @brief 获取发送队列剩余空间（可用于流量控制）
 */
uint32_t net_client_get_queue_space(void);

#endif /* NET_CLIENT_H */