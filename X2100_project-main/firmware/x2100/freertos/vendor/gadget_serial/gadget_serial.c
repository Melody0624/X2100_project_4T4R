#include <common.h>
#include <os/freertos/include/FreeRTOS.h>
#include <os/freertos/include/task.h>
#include <os/freertos/include/semphr.h>
#include <sys/errno.h>
#include <usb/gadget_serial.h>
#include <os.h>
#include <driver/systick.h>
#include <string.h>
#include "cli.h"

#define RX_BUF_SIZE 8192
static unsigned char usb_rx_buf[RX_BUF_SIZE];

#define TEST_DURATION_MS 5000  // 测试持续时间 5 秒
static volatile int test_running_flag = 0;  // 测试运行标志
static volatile int test_abort_flag = 0;    // 测试中止标志
static int uart_rec_task_running = 0;
static SemaphoreHandle_t usb_serial_mutex = NULL;  // USB Serial 互斥信号量
static thread_ptr_t usb_gadget_serial_handle = NULL; // 接收任务句柄，用于挂起/恢复
static volatile int usb_gadget_serial_suspended = 0;  // 任务挂起状态标志

#define RING_BUF_SIZE (64 * 1024)  // 环形缓冲区大小 64KB
static unsigned char ring_buf[RING_BUF_SIZE];
static volatile uint32_t ring_buf_head = 0;  // 写入指针
static volatile uint32_t ring_buf_tail = 0;  // 读取指针
static SemaphoreHandle_t ring_buf_mutex = NULL;  // 环形缓冲区互斥信号量
static SemaphoreHandle_t ring_buf_not_empty = NULL;  // 缓冲区非空信号量
static int ring_buf_task_running = 0;  // 环形缓冲区处理任务运行标志

static const struct gadget_id serial_id = {
    .vendor_id = 0x0525,
    .product_id = 0xa4a7
};

static const struct usb_cdc_serial_param serial_parameters ={
    .dwDTERate = 115200,
    .bCharFormat = USB_CDC_1_STOP_BITS,
    .bParityType = USB_CDC_NO_PARITY,
    .bDataBits = 8
};

static inline uint32_t get_current_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void serial_param_callback(struct usb_cdc_serial_param *p)
{
    printf("usb serial: dwDTERate %d, bCharFormat %d, bParityType %d, bDataBits %d\n",
            p->dwDTERate, p->bCharFormat, p->bParityType, p->bDataBits);
}

static void serial_connect_callback(int connect)
{
    printf("serial_connect_callback %d\n", connect);
    if (connect) {
        // USB 连接建立，如果接收任务被挂起则恢复
        if (usb_gadget_serial_suspended && usb_gadget_serial_handle != NULL) {
            printf("usb serial: resuming suspended receive task\n");
            usb_gadget_serial_suspended = 0;
            thread_resume(usb_gadget_serial_handle);
        }
    }
}

/**
 * @brief 环形缓冲区获取当前可用空间
 * @return 可用字节数
 */
static uint32_t ring_buf_get_free_space(void)
{
    uint32_t head = ring_buf_head;
    uint32_t tail = ring_buf_tail;
    
    if (head >= tail) {
        return RING_BUF_SIZE - (head - tail) - 1;
    } else {
        return tail - head - 1;
    }
}

/**
 * @brief 环形缓冲区获取当前数据长度
 * @return 数据字节数
 */
static uint32_t ring_buf_get_data_len(void)
{
    uint32_t head = ring_buf_head;
    uint32_t tail = ring_buf_tail;
    
    if (head >= tail) {
        return head - tail;
    } else {
        return RING_BUF_SIZE - (tail - head);
    }
}

/**
 * @brief 环形缓冲区写入数据
 * @param data: 要写入的数据
 * @param len: 数据长度
 * @return 实际写入的字节数
 */
static int ring_buf_write(const unsigned char *data, uint32_t len)
{
    if (!data || len == 0) {
        return 0;
    }
    
    if (xSemaphoreTake(ring_buf_mutex, portMAX_DELAY) != pdTRUE) {
        return -1;
    }
    
    uint32_t free_space = ring_buf_get_free_space();
    uint32_t write_len = (len < free_space) ? len : free_space;
    
    if (write_len == 0) {
        xSemaphoreGive(ring_buf_mutex);
        return -2;  // 缓冲区满
    }
    
    uint32_t head = ring_buf_head;
    
    if (head + write_len <= RING_BUF_SIZE) {
        memcpy((void *)&ring_buf[head], data, write_len);
        head += write_len;
    } else {
        uint32_t first_part = RING_BUF_SIZE - head;
        memcpy((void *)&ring_buf[head], data, first_part);
        memcpy((void *)ring_buf, data + first_part, write_len - first_part);
        head = write_len - first_part;
    }
    
    ring_buf_head = head;
    
    xSemaphoreGive(ring_buf_mutex);
    xSemaphoreGive(ring_buf_not_empty);
    
    return write_len;
}

/**
 * @brief 环形缓冲区读取数据
 * @param buf: 接收缓冲区
 * @param len: 要读取的长度
 * @return 实际读取的字节数
 */
static int ring_buf_read(unsigned char *buf, uint32_t len)
{
    if (!buf || len == 0) {
        return 0;
    }
    
    if (xSemaphoreTake(ring_buf_mutex, portMAX_DELAY) != pdTRUE) {
        return -1;
    }
    
    uint32_t data_len = ring_buf_get_data_len();
    uint32_t read_len = (len < data_len) ? len : data_len;
    
    if (read_len == 0) {
        xSemaphoreGive(ring_buf_mutex);
        return 0;
    }
    
    uint32_t tail = ring_buf_tail;
    
    if (tail + read_len <= RING_BUF_SIZE) {
        memcpy(buf, (const void *)&ring_buf[tail], read_len);
        tail += read_len;
    } else {
        uint32_t first_part = RING_BUF_SIZE - tail;
        memcpy(buf, (const void *)&ring_buf[tail], first_part);
        memcpy(buf + first_part, (const void *)ring_buf, read_len - first_part);
        tail = read_len - first_part;
    }
    
    ring_buf_tail = tail;
    
    xSemaphoreGive(ring_buf_mutex);
    
    return read_len;
}

/**
 * @brief 环形缓冲区清空
 */
static void ring_buf_clear(void)
{
    if (xSemaphoreTake(ring_buf_mutex, portMAX_DELAY) == pdTRUE) {
        ring_buf_head = 0;
        ring_buf_tail = 0;
        xSemaphoreGive(ring_buf_mutex);
    }
}

/**
 * @brief 环形缓冲区处理任务
 * 持续从环形缓冲区读取数据并通过 USB Serial 发送
 */
static void ring_buf_process_task(void *arg)
{
    unsigned char send_buf[512];
    int len;
    
    printf("usb serial: ring buffer process thread started\n");
    
    while (ring_buf_task_running) {
        if (xSemaphoreTake(ring_buf_not_empty, portMAX_DELAY) == pdTRUE) {
            len = ring_buf_read(send_buf, sizeof(send_buf));
            if (len > 0) {
                if (gadget_serial_get_connect_status()) {
                    int ret = gadget_serial_write(send_buf, len, 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
                    if (ret < 0) {
                        printf("usb serial: ring buffer send failed, ret=%d\n", ret);
                    }
                } else {
                    printf("usb serial: not connected, dropping %d bytes\n", len);
                }
            }
        }
    }
    
    printf("usb serial: ring buffer process thread exiting\n");
}

/**
 * @brief USB Serial 发送单条指令（线程安全）
 * @param data: 要发送的数据
 * @param len: 数据长度
 * @return 实际发送的字节数
 */
int usb_serial_send(const unsigned char *data, int len)
{
    if (!data || len <= 0) {
        printf("usb serial: invalid send parameters\n");
        return -1;
    }
    
    int ret = gadget_serial_get_connect_status();
    if (ret != 1) {
        return ret;
    }

    const int max_retries = 3;
    int total_sent = 0;
    int remaining = len;
    const unsigned char *current_ptr = data;

    while (remaining > 0) {
        int ret = -1;
        int sent_this_round = 0;

        for (int retry = 0; retry < max_retries; retry++) {
            ret = gadget_serial_write(current_ptr, remaining, 1, 5);
            if (ret > 0) {
                sent_this_round = ret;
                break;
            }

            if (ret == -ETIMEDOUT || ret == -EAGAIN)
            {
                if (retry < max_retries - 1) {
                    vTaskDelay(pdMS_TO_TICKS(2));
                }
            } else {
                printf("usb serial: send failed after %d retries, ret=%d, abort retry\n", retry + 1, ret);
                if (total_sent > 0) {
                    printf("usb serial: partial send completed, total sent=%d/%d bytes\n", total_sent, len);
                }
                return (total_sent > 0) ? total_sent : ret;
            }
        }

        if (sent_this_round <= 0) {
            printf("usb serial: send failed after %d retries, ret=%d\n", max_retries, ret);
            if (total_sent > 0) {
                printf("usb serial: partial send completed, total sent=%d/%d bytes\n", total_sent, len);
            }
            return (total_sent > 0) ? total_sent : ret;
        }

        total_sent += sent_this_round;
        remaining -= sent_this_round;
        current_ptr += sent_this_round;

        if (remaining > 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    return total_sent;
}

/**
 * @brief USB Serial 发送字符串
 * @param str: 要发送的字符串
 * @return 实际发送的字节数
 */
int usb_serial_send_string(const char *str)
{
    if (!str) {
        printf("usb serial: invalid string\n");
        return -1;
    }
    return usb_serial_send((const unsigned char *)str, strlen(str));
}

/**
 * @brief USB Serial 接收数据任务
 * 专注于接收数据，不处理发送
 * 注意：CliSiganlProcess 内部会调用 usb_serial_send，
 * 因此不能将 CliSiganlProcess 放在信号量保护范围内，否则会导致死锁
 */
static void usb_gadget_serial_thread(void *data)
{
    int len = 0;
    static int last_dtr = -1;
    static int discard_active = 0;
    static uint32_t discard_start_time = 0;     // DTR上升沿时刻（ms）
    static int discard_pkt_cnt = 0;             // 窗口内已丢弃的数据包数
    const uint32_t DISCARD_WINDOW_MS = 300;     // 窗口时间300ms
    const int DISCARD_MAX_PKTS = 10;            // 最多丢弃10个数据包

    printf("usb serial: receive thread started\n");

    while (uart_rec_task_running)
    {
        // 1. 先读取数据（阻塞，超时100ms）
        memset(usb_rx_buf, 0, sizeof(usb_rx_buf));

        // gadget_serial_read 内部会使用内部锁，不需要额外信号量保护
        len = gadget_serial_read(usb_rx_buf, sizeof(usb_rx_buf), 1, 100);

        /* 
        * Linux 主机：连接时会将 DTR 设置为1，断开时会将为0
        *             如果收发一起进行，刚连接时会出现脏数据
        * Windows 主机：无论连接与否，DTR 都会保持为0（部分CDC驱动）
        *
        */
        // 2. 获取当前DTR状态并检测上升沿
        int cur_dtr = gadget_serial_is_dtr_set();
        if (last_dtr == 0 && cur_dtr == 1) {
            // DTR上升沿 → 进入丢弃模式
            discard_active = 1;
            discard_pkt_cnt = 0;
            discard_start_time = get_current_ms(); // 获取系统毫秒时间
            printf("usb serial: DTR set, entering discard mode (window %dms)\n", DISCARD_WINDOW_MS);

            // 立即清空已有队列（非阻塞，超时10ms）
            unsigned char tmp[64];
            int ret;
            int empty_cnt = 0;
            while (empty_cnt < 3) {  // 连续3次无数据才认为队列空
                ret = gadget_serial_read(tmp, sizeof(tmp), 0, 10); // 非阻塞，超时10ms
                if (ret == -EAGAIN) {
                    empty_cnt++;
                } else if (ret > 0) {
                    printf("usb serial: flushed pending %d bytes\n", ret);
                    empty_cnt = 0;   // 有数据，重置空计数
                } else if (ret < 0) {
                    // 其他错误，跳出
                    printf("usb serial: flush error %d\n", ret);
                    break;
                }
            }
            // 丢弃本次已读的数据（如果len>0）
            if (len > 0) {
                printf("usb serial: discarding current read %d bytes due to DTR rise\n", len);
                len = 0;  // 标记为丢弃
            }
        }
        last_dtr = cur_dtr;

        // 3. 判断是否处于丢弃模式（时间窗口 or 包数限制）
        int in_window = 0;
        if (discard_active) {
            uint32_t now = get_current_ms();
            uint32_t elapsed = now - discard_start_time;
            if (elapsed < DISCARD_WINDOW_MS && discard_pkt_cnt < DISCARD_MAX_PKTS) {
                in_window = 1;  // 仍在丢弃窗口内
            } else {
                // 窗口结束或达到最大丢弃包数，退出丢弃模式
                discard_active = 0;
                printf("usb serial: discard mode ended (elapsed %dms, pkts %d)\n", 
                        elapsed, discard_pkt_cnt);
            }
        }

        // 4. 处理本次读取的数据
        if (len > 0) {
            if (in_window) {
                // 在丢弃窗口内，丢弃数据并计数
                discard_pkt_cnt++;
                printf("usb serial: discarding %d bytes (pkt %d)\n", len, discard_pkt_cnt);
                // 注意：len>0但丢弃，不调用 CliSiganlProcess
            } else {
                // 正常模式，交由命令处理
                printf("usb serial: read %d bytes\n", len);
                // CliSiganlProcess 不能放在信号量保护范围内，
                // 因为它内部会调用 usb_serial_send，导致死锁
                CliSiganlProcess(usb_rx_buf, len);
            }
        }
        else if (len < 0)
        {
            // 区分"普通超时/没数据"和"USB真正断开"
            if (len == -ETIMEDOUT || len == -EAGAIN) {
                // 只是没有数据，继续循环等待
                continue;
            }
            // USB 断开或其他异常，短暂延时避免空转占用CPU，
            // 等待 serial_connect_callback 或后续读取自动恢复
            printf("usb serial: read error (%d), wait and retry\n", len);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/**
 * @brief 通过 USB Serial 发送 SD 卡数据（非阻塞，使用环形缓冲区）
 * @param data: 要发送的数据（save_data_to_file 需要存入 SD 卡的数据）
 * @param len: 数据长度
 * @return 实际写入环形缓冲区的字节数，-1 表示参数错误，-2 表示缓冲区满
 */
int usb_serial_send_sd_data(const unsigned char *data, int len)
{
    if (!data || len <= 0) {
        printf("usb serial: invalid sd data parameters\n");
        return -1;
    }
    
    return ring_buf_write(data, len);
}

/**
 * @brief 获取环形缓冲区当前状态
 * @param free_space: 输出参数，可用空间字节数
 * @param used_space: 输出参数，已使用空间字节数
 * @return 0 成功
 */
int usb_serial_get_ring_buf_status(uint32_t *free_space, uint32_t *used_space)
{
    if (!free_space || !used_space) {
        return -1;
    }
    
    if (xSemaphoreTake(ring_buf_mutex, portMAX_DELAY) != pdTRUE) {
        return -1;
    }
    
    *used_space = ring_buf_get_data_len();
    *free_space = ring_buf_get_free_space();
    
    xSemaphoreGive(ring_buf_mutex);
    
    return 0;
}

int gadget_usb_serial_test(void)
{
    if (uart_rec_task_running) {
        printf("usb serial: test is already running\n");
        return -1;
    }

    // 创建互斥信号量
    usb_serial_mutex = xSemaphoreCreateMutex();
    if (usb_serial_mutex == NULL) {
        printf("usb serial: failed to create mutex\n");
        return -1;
    }
// #if((OUTPUT_RAW_DATA == 1)||(USB_UART_UART == 1))
//     // 创建环形缓冲区互斥信号量
//     ring_buf_mutex = xSemaphoreCreateMutex();
//     if (ring_buf_mutex == NULL) {
//         printf("usb serial: failed to create ring buf mutex\n");
//         vSemaphoreDelete(usb_serial_mutex);
//         usb_serial_mutex = NULL;
//         return -1;
//     }
    
//     // 创建环形缓冲区非空信号量
//     ring_buf_not_empty = xSemaphoreCreateBinary();
//     if (ring_buf_not_empty == NULL) {
//         printf("usb serial: failed to create ring buf semaphore\n");
//         vSemaphoreDelete(ring_buf_mutex);
//         ring_buf_mutex = NULL;
//         vSemaphoreDelete(usb_serial_mutex);
//         usb_serial_mutex = NULL;
//         return -1;
//     }
// #endif
    xSemaphoreGive(usb_serial_mutex);
    uart_rec_task_running = 1;
    ring_buf_task_running = 1;

    int ret = gadget_serial_init(&serial_id, &serial_parameters, serial_connect_callback, serial_param_callback);
    if (ret != 0) {
        printf("usb serial: init failed, ret=%d\n", ret);
        vSemaphoreDelete(ring_buf_not_empty);
        ring_buf_not_empty = NULL;
        vSemaphoreDelete(ring_buf_mutex);
        ring_buf_mutex = NULL;
        vSemaphoreDelete(usb_serial_mutex);
        usb_serial_mutex = NULL;
        return ret;
    }
    
    usb_gadget_serial_suspended = 0;
    usb_gadget_serial_handle = thread_create("usb gadget serial thread", 32768, usb_gadget_serial_thread, NULL);
// #if((OUTPUT_RAW_DATA == 1)||(USB_UART_UART == 1))
//     thread_create("usb ring buf process", 8192, ring_buf_process_task, NULL);
//     printf("usb serial: initialized successfully with ring buffer\n");
// #endif
    return 0;
}

void gadget_serial_suspend_recv_task(void)
{
    if (usb_gadget_serial_handle != NULL) {
        if (!usb_gadget_serial_suspended) {
            usb_gadget_serial_suspended = 1;
            // printf("gadget_serial: receive task suspended\n");
            thread_suspend(usb_gadget_serial_handle);
        }
    } else {
        printf("gadget_serial: no receive task to suspend\n");
    }
}

void gadget_serial_resume_recv_task(void)
{
    if (usb_gadget_serial_handle != NULL) {
        if (usb_gadget_serial_suspended) {
            usb_gadget_serial_suspended = 0;
            // printf("gadget_serial: receive task resumed\n");
            thread_resume(usb_gadget_serial_handle);
        }
    } else {
        printf("gadget_serial: no receive task to resume\n");
    }
}

/*
 * 重置 USB 串口内部的 read_wait 状态。
 * 因为 USB 接收线程被挂起时可能正处于 thread_waiter_wait_timeout 内部
 * （等待 USB 数据），导致 read_wait.thread 仍指向被挂起的线程。
 * 若不重置，后续 pkt_read_thread 调用 gadget_serial_read 时会触发
 * assert(!waiter->thread) 断言失败。
 */
void gadget_serial_reset_read_wait_ab(void)
{
    gadget_serial_reset_read_wait();
}

/*
 * 重置 USB 串口内部的 write_wait 状态。
 */
void gadget_serial_reset_write_wait_ab(void)
{
    gadget_serial_reset_write_wait();
}
