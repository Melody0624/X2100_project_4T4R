#include "net_client.h"
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <os/freertos/include/FreeRTOS.h>
#include <os/freertos/include/task.h>
#include <os/freertos/include/semphr.h>

#include "heap_malloc.h"

#if defined(LWIP_POSIX_SOCKETS_API_NAMES) && LWIP_POSIX_SOCKETS_API_NAMES
    /* 启用 POSIX 名称：直接用 errno */
    #define NET_GET_ERRNO()     errno
    #define NET_EINPROGRESS     EINPROGRESS
    #define NET_EINTR           EINTR
#elif defined(sock_errno)
    /* 某些移植版本提供 sock_errno 变量 */
    #define NET_GET_ERRNO()     sock_errno
    #define NET_EINPROGRESS     SOCK_EINPROGRESS
    #define NET_EINTR           SOCK_EINTR
#elif defined(__ERRNO_H) || defined(_ERRNO_H)
    /* 标准 errno.h 已包含 */
    #define NET_GET_ERRNO()     errno
    #define NET_EINPROGRESS     EINPROGRESS
    #define NET_EINTR           EINTR
#else
    /* 假设 LwIP 使用 ERR_* 枚举，通过 SO_ERROR 获取 */
    /* 辅助函数：通过 getsockopt 获取套接字错误码 */
    static int net_get_socket_error(int sock) {
        int err = 0;
        socklen_t len = sizeof(err);
        if (sock >= 0) {
            lwip_getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
        }
        return err;
    }
    #define NET_GET_ERRNO()     net_get_socket_error(g_sock_fd)
    #define NET_EINPROGRESS     -1  /* 占位，实际通过连接流程判断 */
    #define NET_EINTR           -2
#endif

#define NET_CHECK_INIT(ret_val) \
    do { \
        if (!prv_is_initialized()) { \
            return (ret_val); \
        } \
    } while(0)


/*================ 可配置参数 ================*/
#define NET_SEND_THREAD_NAME      "net_send"
#define NET_SEND_STACK_DEFAULT    2048          // 默认栈大小(字)
#define NET_SEND_PRIO_DEFAULT     (tskIDLE_PRIORITY + 3)
#define NET_QUEUE_DEPTH_DEFAULT   3             // 队列深度
#define NET_CONN_TIMEOUT_DEFAULT  5000          // 连接默认超时
/*==========================================*/

// #define NET_DEBUG

/* 内部全局状态 */
static int g_sock_fd = -1;
static volatile net_state_t g_state = NET_STATE_DISCONNECTED;
static SemaphoreHandle_t g_state_mutex = NULL;

/* 发送线程相关 */
static TaskHandle_t g_send_task = NULL;
static QueueHandle_t g_send_queue = NULL;
static volatile bool g_send_task_running = false;

/* 连接线程相关 */
static TaskHandle_t g_conn_task = NULL;
static volatile bool g_conn_task_running = false;

/* 内部函数声明 */
static void prv_send_thread(void *pvParameters);
static int prv_do_send(int sock, const uint8_t *data, uint32_t len, uint32_t timeout_ms);
static inline bool prv_is_initialized(void);

/*================ 公共接口 ================*/

#ifdef NET_DEBUG
void net_client_dump_status(void)
{
    printf("[NET] g_state_mutex: %p\n", (void*)g_state_mutex);
    printf("[NET] g_send_queue:  %p\n", (void*)g_send_queue);
    printf("[NET] g_send_task:   %p\n", (void*)g_send_task);
    printf("[NET] g_state:       %d\n", (int)g_state);
    printf("[NET] g_sock_fd:     %d\n", g_sock_fd);
}
#endif

int net_client_init(uint32_t stack_depth, UBaseType_t priority, uint32_t queue_depth)
{
    /* 防止重复初始化 */
    if (prv_is_initialized()) {
        return NET_OK;  // 已初始化，直接成功
    }

    if (stack_depth == 0) stack_depth = NET_SEND_STACK_DEFAULT;
    if (queue_depth == 0) queue_depth = NET_QUEUE_DEPTH_DEFAULT;
    if (priority == 0) priority = NET_SEND_PRIO_DEFAULT;

    /* 先创建所有资源，再修改状态 */
    SemaphoreHandle_t new_mutex = xSemaphoreCreateMutex();
    if (new_mutex == NULL) {
        return NET_ERR_NO_MEM;  // 创建失败，直接返回，不修改全局状态
    }

    QueueHandle_t new_queue = xQueueCreate(queue_depth, sizeof(net_send_item_t));
    if (new_queue == NULL) {
        vSemaphoreDelete(new_mutex);  // 回滚已创建的资源
        return NET_ERR_NO_MEM;
    }

    /* 所有资源创建成功后，再赋值给全局变量 */
    g_state_mutex = new_mutex;
    g_send_queue = new_queue;
    
    /* 最后启动发送线程（避免线程访问未初始化资源） */
    g_send_task_running = true;
    BaseType_t ret = xTaskCreate(prv_send_thread, 
                                 NET_SEND_THREAD_NAME,
                                 stack_depth,
                                 NULL,
                                 priority,
                                 &g_send_task);
    if (ret != pdPASS) {
        // 线程创建失败，回滚
        vQueueDelete(g_send_queue);
        vSemaphoreDelete(g_state_mutex);
        g_send_queue = NULL;
        g_state_mutex = NULL;
        return NET_ERR_NO_MEM;
    }

    /* 最后更新状态 */
    g_state = NET_STATE_DISCONNECTED;
    
    /* 等待线程真正启动（避免竞态） */
    vTaskDelay(pdMS_TO_TICKS(10));

    return NET_OK;
}

void net_client_deinit(void)
{
    if (!prv_is_initialized()) {
        return;
    }

    // 强制切断底层 Socket
    net_client_disconnect();

    // 2. 停止连接任务 (Connect Task)
    g_conn_task_running = false;
    if (g_conn_task != NULL) {
        // 给它一点时间自然退出，或者粗暴点直接 vTaskDelete(g_conn_task)
        vTaskDelay(pdMS_TO_TICKS(200)); 
        if(g_conn_task != NULL) {
            vTaskDelete(g_conn_task);
            g_conn_task = NULL;
        }
    }
    
    g_send_task_running = false;
    if (g_send_task != NULL) {
        // 发送一个空项唤醒线程退出
        net_send_item_t exit_item = {0};
        xQueueSend(g_send_queue, &exit_item, pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(200)); // 等待线程退出
        g_send_task = NULL;
    }
    
    // 清理队列
    if (g_send_queue != NULL) {
        net_send_item_t item;
        while (xQueueReceive(g_send_queue, &item, 0) == pdTRUE) {
            // if (item.data) vPortFree(item.data);
            if (item.data) free(item.data);
        }
        vQueueDelete(g_send_queue);
        g_send_queue = NULL;
    }
    
    if (g_state_mutex) {
        vSemaphoreDelete(g_state_mutex);
        g_state_mutex = NULL;
    }
}

net_state_t net_client_get_state(void)
{
    return g_state;
}

uint32_t net_client_get_queue_space(void)
{
    if (g_send_queue == NULL) return 0;
    return uxQueueSpacesAvailable(g_send_queue);
}

int net_client_connect(const char *ip, uint16_t port, uint32_t timeout_ms)
{
    NET_CHECK_INIT(NET_ERR_STATE); 
    
    if (!ip || port == 0) return NET_ERR_PARAM;
    if (timeout_ms == 0) timeout_ms = NET_CONN_TIMEOUT_DEFAULT;

#ifdef NET_DEBUG
    net_client_dump_status();
#endif
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) 
        return NET_ERR_TIMEOUT;

    // 清理旧连接
    if (g_sock_fd >= 0) {
        lwip_close(g_sock_fd);
        g_sock_fd = -1;
    }
    g_state = NET_STATE_CONNECTING;
    xSemaphoreGive(g_state_mutex);

    // 创建 Socket
    int sock = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_state = NET_STATE_ERROR;
        xSemaphoreGive(g_state_mutex);
        return NET_ERR_CONN;
    }

    // 设置为非阻塞
    int flags = 1;
    lwip_ioctl(sock, FIONBIO, &flags);

    // 填充地址
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = lwip_htons(port);
    if (lwip_inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        lwip_close(sock);
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_state = NET_STATE_ERROR;
        xSemaphoreGive(g_state_mutex);
        return NET_ERR_PARAM;
    }

    // 发起连接
    int ret = lwip_connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (ret == 0) {
        // 立即连接成功
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_sock_fd = sock;
        g_state = NET_STATE_CONNECTED;
        xSemaphoreGive(g_state_mutex);
        
        // 关闭 Nagle 算法，降低小数据延迟
        int tcp_nodelay = 1;
        lwip_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay, sizeof(tcp_nodelay));
        return NET_OK;
    }

    int err = NET_GET_ERRNO();
    if (err != EINPROGRESS && err != EALREADY && err != 0) {
        lwip_close(sock);
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_state = NET_STATE_ERROR;
        xSemaphoreGive(g_state_mutex);
        return NET_ERR_CONN;
    }

    // select 等待连接完成
    fd_set writeset;
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    FD_ZERO(&writeset);
    FD_SET(sock, &writeset);

    ret = lwip_select(sock + 1, NULL, &writeset, NULL, &tv);
    if (ret <= 0) {
        lwip_close(sock);
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_state = NET_STATE_ERROR;
        xSemaphoreGive(g_state_mutex);
        return (ret == 0) ? NET_ERR_TIMEOUT : NET_ERR_CONN;
    }

    // 检查连接结果
    int so_err = 0;
    socklen_t len = sizeof(so_err);
    lwip_getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_err, &len);
    if (so_err != 0) {
        lwip_close(sock);
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_state = NET_STATE_ERROR;
        xSemaphoreGive(g_state_mutex);
        return NET_ERR_CONN;
    }

    // 连接成功：关闭 Nagle 算法
    int tcp_nodelay = 1;
    lwip_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay, sizeof(tcp_nodelay));

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_sock_fd = sock;

    g_state = NET_STATE_CONNECTED;
    xSemaphoreGive(g_state_mutex);
    
    return NET_OK;
}

void net_client_connect_task(void *pv)
{
    const char *srv_ip = "192.168.1.100";  
    uint16_t srv_port = 5000;
    struct netif *netif = netif_default;
    
    printf("[NET_CONN] Task started. Waiting for network stack ready...\n");
    
    int timeout = 100; // 10秒
    while (timeout-- > 0) {
        bool link_up = netif_is_link_up(netif);
        const ip4_addr_t *ip = netif_ip4_addr(netif);
        bool ip_valid = (ip != NULL && ip->addr != IPADDR_ANY); 
        
        if (link_up && ip_valid) {
            break; 
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    const ip4_addr_t *final_ip = netif_ip4_addr(netif);
    const char *ip_str = final_ip ? ip4addr_ntoa(final_ip) : "NULL";
    
    if (!netif_is_link_up(netif) || !final_ip || final_ip->addr == IPADDR_ANY) {
        printf("[NET_CONN] FATAL: Network not ready (Link=%d, IP=%s)! Abort.\n", 
               netif_is_link_up(netif), ip_str);
        vTaskDelete(NULL);
        return;
    }

    // PHY 芯片在 Link Up 后可能需要重新协商，导致短暂 Link Down。
    // 等待 1.5 秒，确保链路真正稳定，避开闪断期。
    printf("[NET_CONN] Link UP & IP Ready (%s). Waiting 1.5s for PHY stabilization...\n", ip_str);
    vTaskDelay(pdMS_TO_TICKS(1500));
    
    // 再次确认链路没有在防抖期间断开
    if (!netif_is_link_up(netif)) {
        printf("[NET_CONN] WARNING: Link dropped during stabilization! Waiting for re-link...\n");
        // 如果断了，就等它再次连上
        while (!netif_is_link_up(netif)) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        printf("[NET_CONN] Link recovered. Stabilizing again...\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    printf("[NET_CONN] Network stable. Target: %s:%d. Starting connect loop...\n", srv_ip, srv_port);
    
    while (1) {
        // 如果网络模块已经被去初始化，结束任务
        if (net_client_get_state() == NET_STATE_DISCONNECTED && prv_is_initialized() == false) {
            printf("[NET_CONN] Network deinitialized, exiting task.\n");
            break; 
        }

        net_state_t state = net_client_get_state();
        
        /* 已连接则休眠等待 */
        if (state == NET_STATE_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        
        /* 如果是 ERROR 状态，先调用 disconnect 复位到 DISCONNECTED */
        if (state == NET_STATE_ERROR) {
            printf("[NET_CONN] Recovering from ERROR state...\n");
            net_client_disconnect();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        printf("[NET_CONN] State=%d, attempting connect...\n", net_client_get_state());
        int ret = net_client_connect(srv_ip, srv_port, 3000);
        
        if (ret == NET_OK) {
            printf("[NET_CONN] Connected successfully!\n");
        } else {
            printf("[NET_CONN] Connect failed! ret=%d\n", ret);
            /* 根据错误码打印提示 */
            if (ret == NET_ERR_TIMEOUT) printf("  -> Timeout\n");
            else if (ret == NET_ERR_CONN) printf("  -> Connection refused/reset\n");
            else printf("  -> Other error (check state/mutex)\n");
            
            vTaskDelay(pdMS_TO_TICKS(2000)); // 失败后等待重连
        }
    }

    vTaskDelete(NULL);
}

int net_client_send_async(const uint8_t *data, uint32_t len, uint32_t frameID, uint32_t timeout_ms)
{
    NET_CHECK_INIT(NET_ERR_STATE); 

    if (!data || len == 0) return NET_ERR_PARAM;
    if (g_send_queue == NULL) return NET_ERR_STATE;

    // 分配内存并拷贝数据（确保线程安全，调用者无需等待）
    // uint8_t *buf = (uint8_t *)pvPortMalloc(len);
    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf) return NET_ERR_NO_MEM;
    memcpy(buf, data, len);
    
    net_send_item_t item = {
        .data = buf,
        .len = len,
        .result = NULL,
        .timeout_ms = timeout_ms,
        .need_free = true
    };
    
    TickType_t wait_ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    if (xQueueSend(g_send_queue, &item, wait_ticks) != pdTRUE) {
        // vPortFree(buf);  // 队列满，释放内存
        free(buf);
        return NET_ERR_QUEUE_FULL;
    }
    
    return NET_OK;  // 成功入队
}

int net_client_send_async_nocopy(const uint8_t *data, uint32_t len, uint32_t timeout_ms)
{
    NET_CHECK_INIT(NET_ERR_STATE); 

    if (!data || len == 0) return NET_ERR_PARAM;
    if (g_send_queue == NULL) return NET_ERR_STATE;
    
    // 不使用 pvPortMalloc，直接传递指针
    net_send_item_t item = {
        .data = (uint8_t *)data,
        .len = len,
        .result = NULL,
        .timeout_ms = timeout_ms,
        .need_free = false
    };
    
    TickType_t wait_ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    if (xQueueSend(g_send_queue, &item, wait_ticks) != pdTRUE) {
        return NET_ERR_QUEUE_FULL;
    }
    
    return NET_OK;
}

int net_client_send_sync(const uint8_t *data, uint32_t len, uint32_t frameID, uint32_t timeout_ms)
{
    NET_CHECK_INIT(NET_ERR_STATE); 

    if (!data || len == 0) return NET_ERR_PARAM;
    if (g_send_queue == NULL) return NET_ERR_STATE;
    
    // 创建同步信号量
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    if (!sem) return NET_ERR_NO_MEM;
    
    int result = NET_ERR_SEND;
    
    // 分配内存并拷贝数据
    // uint8_t *buf = (uint8_t *)pvPortMalloc(len);
    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf) {
        vSemaphoreDelete(sem);
        return NET_ERR_NO_MEM;
    }
    memcpy(buf, data, len);
    
    net_send_item_t item = {
        .data = buf,
        .len = len,
        .result = &result,
        .timeout_ms = timeout_ms,
        .need_free = true
    };
    
    // 入队（等待队列空间）
    TickType_t queue_wait = pdMS_TO_TICKS(100);  // 等待入队超时
    if (xQueueSend(g_send_queue, &item, queue_wait) != pdTRUE) {
        // vPortFree(buf);
        free(buf);
        vSemaphoreDelete(sem);
        return NET_ERR_QUEUE_FULL;
    }
    
    // 等待发送完成
    TickType_t send_wait = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(sem, send_wait) != pdTRUE) {
        vSemaphoreDelete(sem);
        return NET_ERR_TIMEOUT;  // 发送超时
    }
    
    vSemaphoreDelete(sem);
    return result;
}

int net_client_disconnect(void)
{
    NET_CHECK_INIT(NET_ERR_STATE); 

    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(200)) != pdTRUE) 
        return NET_ERR_TIMEOUT;

    if (g_sock_fd >= 0) {
        lwip_shutdown(g_sock_fd, SHUT_RDWR);  // 完全关闭双向
        lwip_close(g_sock_fd);
        g_sock_fd = -1;
    }
    g_state = NET_STATE_DISCONNECTED;
    xSemaphoreGive(g_state_mutex);
    return NET_OK;
}

/*================ 内部实现 ================*/

static inline bool prv_is_initialized(void)
{
    return (g_state_mutex != NULL);
}

/**
 * @brief 实际执行阻塞发送（在发送线程中调用）
 */
static int prv_do_send(int sock, const uint8_t *data, uint32_t len, uint32_t timeout_ms)
{
    if (sock < 0) return NET_ERR_CONN;
    
    uint32_t sent = 0;
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (sent < len) {
        // 计算剩余超时
        TickType_t elapsed = xTaskGetTickCount() - start_tick;
        if (elapsed >= timeout_ticks) {
            return NET_ERR_TIMEOUT;
        }
        uint32_t rem_ms = (timeout_ticks - elapsed) * portTICK_PERIOD_MS;
        
        // select 等待可写
        struct timeval tv = { rem_ms / 1000, (rem_ms % 1000) * 1000 };
        fd_set writeset;
        FD_ZERO(&writeset);
        FD_SET(sock, &writeset);
        
        int ret = lwip_select(sock + 1, NULL, &writeset, NULL, &tv);
        if (ret <= 0) {
            return (ret == 0) ? NET_ERR_TIMEOUT : NET_ERR_SEND;
        }
        
        // 执行 send
        int n = lwip_send(sock, data + sent, len - sent, 0);
        if (n < 0) {
            int err = errno;
            if (err == EINTR) continue;  // 信号中断，重试
            return NET_ERR_SEND;
        }
        if (n == 0) {
            return NET_ERR_CONN;  // 对端关闭
        }
        sent += n;
    }
    return (int)sent;
}

/**
 * @brief 发送线程主函数
 */
static void prv_send_thread(void *pvParameters)
{
    (void)pvParameters;
    net_send_item_t item;
    
    while (g_send_task_running) {
        // 阻塞等待发送请求
        if (xQueueReceive(g_send_queue, &item, pdMS_TO_TICKS(500)) != pdTRUE) {
            continue;  // 超时，检查是否退出
        }
        
        // 空项表示退出信号
        if (item.data == NULL && item.len == 0) {
            break;
        }
        
        // 检查连接状态
        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            bool connected = (g_state == NET_STATE_CONNECTED && g_sock_fd >= 0);
            int sock = g_sock_fd;
            xSemaphoreGive(g_state_mutex);
            
            if (!connected) {
                // 未连接，直接返回错误
                if (item.result) *item.result = NET_ERR_STATE;

                // if (item.data) vPortFree(item.data);
                if (item.data) free(item.data);
                continue;
            }
            
            // 执行实际发送
            int ret = prv_do_send(sock, item.data, item.len, 
                                (item.timeout_ms > 0) ? item.timeout_ms : 3000);
            
            // 通知结果
            if (item.result) *item.result = ret;
            
            // 发送失败且连接异常，更新状态
            if (ret < 0 && ret != NET_ERR_TIMEOUT) {
                xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                if (g_state == NET_STATE_CONNECTED) {
                    g_state = NET_STATE_ERROR;
                }
                xSemaphoreGive(g_state_mutex);
            }
        }
        
        // 释放资源
        if (item.need_free && item.data) {
            // vPortFree(item.data);
            free(item.data);
        }
    }
    
    // 线程退出前清理队列剩余项
    while (xQueueReceive(g_send_queue, &item, 0) == pdTRUE) {
        if (item.need_free && item.data) {
            // vPortFree(item.data);
            free(item.data); //
        }
    }
    
    g_send_task = NULL;
    vTaskDelete(NULL);
}
