#include <stdio.h>
#include <driver/uart.h>
#include <os.h>
#include <string.h>
#include "uart_trans.h"
#include "memory_pool.h"
#include"general_functions.h"
#include <os/freertos/include/FreeRTOS.h>
#include <os/freertos/include/task.h>
#include <os/freertos/include/semphr.h>
#include <os/freertos/include/semphr.h>
#include "../gadget_serial/gadget_serial.h"
#include "../eol_cal/eol_calibration.h"
#include "board.h"
#include "ota_trigger_ab.h"

#define USE_UART_SEND_THREAD

#if (DOT_REPLAY == 1)
#include "frames_save.h"
extern int DetFrameNum;
#endif

//协议选择
#define OUTPUT_PROTOCOL_UART_ID_DEFAULT                     1       //默认协议
#define OUTPUT_PROTOCOL_UART_ID_BOSCH                       0       //Bosch协议

/* 串口接收任务相关定义 */
#define UART_RECV_BUFFER_SIZE     256
#define UART_LINE_BUFFER_SIZE     512
#define CLI_MAX_ARGS              10

static TaskHandle_t uart_recv_task_handle = NULL;
static volatile int uart_recv_task_running = 0;
static SemaphoreHandle_t uart_recv_sem = NULL;

static struct uart_config  uart3_cfg = {
    .uart_id            = 3,
    .data_bits          = 8,
    .stop_bits          = 1,
    .loop_mode          = 0,
    .tx_poll_mode       = 0,
    .rx_poll_mode       = 0,
    .parity             = UART_PARITY_NONE,
    .follow_contrl      = UART_FC_NONE,
    //.baud_rate          = 921600,
#if OUTPUT_PROTOCOL_UART_ID_DEFAULT
    .baud_rate          = 115200,
#endif
#if OUTPUT_PROTOCOL_UART_ID_BOSCH
    .baud_rate          = 115200,
#endif
};

static struct mutex frame_mutex;
static thread_cond_t uart_thread_cond;
static thread_ptr_t uart_thread = NULL;
static volatile int uart_thread_running = 0;
static volatile int data_ready = 0;

#if (USE_USB_OUTPUT == 1)
static size_t gFrameBufSize = 0;
static char *gFrameBuf = NULL;
#endif

static int gFrameID = 0;
static Mmw_pkt_info *g_pPktInfo = NULL;
static const char *g_pktBuf = NULL;
static int32_t g_pktSize = 0;

// 预警信息发送定时器（80ms发送一次）
static uint32_t gWarningTimer = 0;
#define WARNING_SEND_INTERVAL_MS 80

// 目标信息输出使能标志（默认关闭）
static volatile int gTargetInfoOutputEnabled = 0;

// 雷达版本号（3字节，格式：主版本号.次版本号.修订号）
static uint32_t gRadarVersion = 0x010203; // 默认版本号 1.2.3

// BSD启动车速阈值(单位：km/h)
static float gEgoSpeedThresholdKph = 10.0f;

static int uart_frame_pkt_send(void);
static int uart_alarm_pkt_send(int direction, float distance, float velocity);
static int uart_no_alarm_pkt_send(int direction);
static int uart_warning_pkt_send(void);
static int uart_recv_task_init(void);
static void uart_recv_task_deinit(void);
void update_and_send_warning_distance(uint8_t left_distance, uint8_t right_distance, uint8_t left_turn, uint8_t right_turn);
extern void write_dump_file_name(const char *name);
extern uint64_t systick_get_time_ms(void);

// 协议命令类型
typedef enum {
    RADAR_CMD_UNKNOWN = 0,
    RADAR_CMD_ENABLE_OUTPUT = 1,
    RADAR_CMD_DISABLE_OUTPUT = 2,
    RADAR_CMD_READ_VERSION = 3,
    RADAR_CMD_SET_START_SPEED = 4,
    RADAR_CMD_READ_START_SPEED = 5,
    RADAR_CMD_SET_ANGLE_OFFSET = 6,
    RADAR_CMD_READ_ANGLE_OFFSET = 7,
} RadarCommandType;

// 雷达命令结构体
typedef struct {
    RadarCommandType cmd_type;
    uint8_t radar_id;
    uint8_t data_length;
    uint8_t control_flag;
    uint8_t did;
    uint8_t start_speed;
    int8_t angle_offset;
} RadarCommand;

// 车身信息回复数据结构
typedef struct {
    uint8_t data_length;
    uint8_t response_type;
    uint8_t radar_id;
    uint8_t did;
    uint8_t reserved[3];
    uint8_t left_turn;
} CarInfoResponse;

static uint16_t calculate_checksum(const uint8_t *data, uint8_t len);
static int parse_radar_command(const uint8_t *buffer, uint8_t length, RadarCommand *cmd);
static int parse_car_info_response(const uint8_t *buffer, uint8_t length, CarInfoResponse *resp);
static int send_radar_response(RadarCommandType cmd_type, uint32_t data);

/*
 * Function: SetDumpFileName
 * Input: argc - int - 0 on success, -1 on failure
 * Output: Always successful (0)
 * Description: Sets the dump file name.
 */
static int32_t SetDumpFileName(int32_t argc, char *argv[])
{
    write_dump_file_name(argv[1]);
    return 0;
}

static int32_t uart3_uds_cmd(int32_t argc, char *argv[])
{
    uint8_t req_data[8] = {0};
    uint8_t resp_data[8] = {0};
    uint8_t resp_len = 0;

    if (argc < 2 || argc > 9) {
        char err_msg[] = "Error: UDS payload length must be 1-8 bytes.\r\n";
        uart_general_send(err_msg, strlen(err_msg));
        return -1;
    }

    for (int i = 1; i < argc; i++) {
        req_data[i - 1] = (uint8_t)strtol(argv[i], NULL, 16);
    }
    uint8_t req_len = argc - 1;

    eol_uds_process_request(req_data, req_len, resp_data, &resp_len);

    if (resp_len > 0) {
        char resp_str[128];
        int offset = snprintf(resp_str, sizeof(resp_str), "[UDS_RESP] ");
        for (int i = 0; i < resp_len; i++) {
            offset += snprintf(resp_str + offset, sizeof(resp_str) - offset, "%02X ", resp_data[i]);
        }
        snprintf(resp_str + offset, sizeof(resp_str) - offset, "\r\n");
        uart_general_send(resp_str, strlen(resp_str));
    }

    return 0;
}

/*
 * Function: otaUpgrade_cmd
 * Input: argc - int - 0 on success, -1 on failure
 * Output: Always successful (0)
 * Description: OTA upgrade command (UART3 transport path).
 */
static int32_t otaUpgrade_cmd(int32_t argc, char *argv[])
{
    // printf("otaUpgrade_cmd\n");

    /* OTA 固件升级命令
     * 此命令来自 UART3 传输路径，传入 OTA_TRANSPORT_UART */
    if (ota_cmd_handler(argc, argv, OTA_TRANSPORT_UART) != 0)
    {
        return -1;
    }

    // 不再在此处返回 OK 响应 ，由 OTA 线程准备启动前发送
    // 因为主线程可能也在发送数据，导致两者同时占用，发送失败

    return 0;
}

/**
 * @brief UART发送线程函数
 * 循环发送数据，每次发送完成后等待条件变量信号
 */
static void uart_send_thread_func(void *data)
{
    // uint64_t start_time, end_time;
	
    while (uart_thread_running)
    {
        mutex_lock(&frame_mutex);

        while (!data_ready && uart_thread_running)
        {
            thread_cond_wait(&uart_thread_cond, &frame_mutex);
        }

        if (!uart_thread_running)
        {
            mutex_unlock(&frame_mutex);
            break;
        }

        data_ready = 0;

        // start_time = get_time_ns();

        // printf("[*** frame %d ***] 开始发送数据(UART)\n", gFrameID);
        if (OUTPUT_PROTOCOL_UART_ID_DEFAULT)
        {
            uart_frame_pkt_send();
        }

        if (OUTPUT_PROTOCOL_UART_ID_BOSCH)
        {
            // 每帧都发送预警信息
            uart_warning_pkt_send();
        }

        // end_time = get_time_ns();
        // double send_time_ms = (end_time - start_time) / 1000000.0;

		// printf("[*** frame %d ***] uart send thread 总耗时: %.2f 毫秒\n", gFrameID, send_time_ms);

        mutex_unlock(&frame_mutex);

        // 添加短暂延时，避免忙等待
        thread_wait_timeout(1);
    }
}

static int uart_send_thread_init(void)
{
    mutex_init(&frame_mutex);
    thread_cond_init(&uart_thread_cond);

    uart_thread = thread_create("uart_send_test", 32768, uart_send_thread_func, NULL);
    if (uart_thread == NULL)
    {
        printf("Failed to create uart send thread\n");
        return -1;
    }

    uart_thread_running = 1;
    thread_set_priority(uart_thread, OS_priority_high);
    
    return 0;
}

int uart_open(void)
{
    if (!uart3_cfg.uart_id)
    {
        return -1;
    }
    
    uart_start(&uart3_cfg);

#ifdef USE_UART_SEND_THREAD
    uart_send_thread_init();
#endif

    /* 初始化串口接收任务 */
    uart_recv_task_init();

    return 0;
}

int uart_close(void)
{
    if (!uart3_cfg.uart_id)
    {
        return -1;
    }

#ifdef USE_UART_SEND_THREAD
    mutex_lock(&frame_mutex);
    uart_thread_running = 0;
    data_ready = 1;
    thread_cond_broadcast(&uart_thread_cond);
    mutex_unlock(&frame_mutex);
    thread_join(uart_thread, NULL);
#endif

    /* 停止串口接收任务 */
    uart_recv_task_deinit();

    uart_stop(&uart3_cfg);
    data_ready = 0;

    return 0;
}

static int send_data(char *buf, int size)
{
	if (!buf || size <= 0)
    {
		printf("ERROR: Invalid buffer or size in send_data\n");
		return -1;
	}

    if (ota_is_busy()) return 0;

    uart_send(&uart3_cfg, buf, size);
    // printf("UART: Successfully sent %d bytes\n", size);

	return size;
}

// 仅用于物理串口返回命令响应以及EOL结果，不用于发送数据帧
int uart_general_send(char *buf, int size)
{
    if (!buf || size <= 0)
    {
        return -1;
    }

    if (ota_is_busy()) return 0;

    send_data(buf, size);

    return 0;
}

int uart_frame_pkt_update(Mmw_pkt_info *pktInfo, const char *pktBuf, int32_t pktSize, uint32_t frameID)
{
    if (!pktBuf || pktSize <= 0)
    {
        printf("ERROR: Invalid packet buffer or size in uart_frame_pkt_update\n");
        return -1;
    }

    mutex_lock(&frame_mutex);

    g_pPktInfo = pktInfo;
    gFrameID = frameID;
    g_pktBuf = pktBuf;
    g_pktSize = pktSize;

    data_ready = 1;

    mutex_unlock(&frame_mutex);

    if (ota_is_busy()) return 0;

#ifdef USE_UART_SEND_THREAD
    thread_cond_broadcast(&uart_thread_cond);
#else
    uart_frame_pkt_send();
#endif

    return 0;
}

/**
 * @brief 发送带帧头数据
 * @param ctx 全局上下文指针
 * @param frameNumber 帧号
 * @return 成功返回0，失败返回错误码，-2表示USB发送失败
 */
static int uart_frame_pkt_send(void)
{
	if (!g_pktBuf || g_pktSize <= 0) {
		printf("ERROR: Invalid packet buffer or size\n");
		return -1;
	}

    if (ota_is_busy()) return 0;

#if USE_USB_UART
#if (USE_USB_OUTPUT == 0)
    send_data((char *)g_pktBuf, g_pktSize);
#else
    int ret = usb_serial_send((char *)g_pktBuf, g_pktSize);
    if (ret < 0) {
        printf("ERROR: USB serial send failed, ret=%d\n", ret);
        return -2;
    }
    if (ret < g_pktSize) {
        printf("WARNING: USB serial partial send: sent %d/%d bytes\n", ret, g_pktSize);
    }
#endif
#else
    send_data((char *)g_pktBuf, g_pktSize);
#endif

	return 0;
}

/* 串口接收任务实现 */
static void uart_recv_task(void *pvParameters)
{
    uint8_t recv_buffer[UART_RECV_BUFFER_SIZE];
    uint8_t line_buffer[UART_LINE_BUFFER_SIZE];
    uint32_t line_index = 0;
    int32_t ret;
    
    /* VT协议解析状态机 */
    uint8_t vt_frame_buffer[16] = {0};
    uint8_t vt_frame_index = 0;
    uint8_t vt_frame_expected = 0;
    int vt_parsing = 0;

    printf("UART receive task started on UART3\n");

    while (uart_recv_task_running)
    {
        /* 获取信号量保护UART接收操作 */
        if (xSemaphoreTake(uart_recv_sem, portMAX_DELAY) == pdTRUE)
        {
            /* 接收数据 */
            ret = uart_receive_timeout(&uart3_cfg, (char *) recv_buffer, sizeof(recv_buffer) - 1, 100);

            /* 释放信号量 */
            xSemaphoreGive(uart_recv_sem);

            if (ret > 0)
            {
                recv_buffer[ret] = '\0'; /* 确保字符串终止 */

                /* 调试信息：显示接收到的原始数据 */
                printf("Raw UART3 data received (%d bytes): ", ret);
                for (uint32_t j = 0; j < (ret < 32 ? ret : 32); j++)
                {
                    if (recv_buffer[j] >= 32 && recv_buffer[j] <= 126)
                    {
                        printf("%c", recv_buffer[j]);
                    }
                    else
                    {
                        printf("[%02X]", recv_buffer[j]);
                    }
                }
                printf("\n");

                /* 处理接收到的数据 */

                /* 车身信息回复解析 - 检测帧头 0x60 0x07 */
                if (ret >= 12 && recv_buffer[0] == 0x60 && recv_buffer[1] == 0x07) {
                    CarInfoResponse resp;
                    int resp_result = parse_car_info_response(recv_buffer, ret, &resp);
                    if (resp_result == 0) {
                        printf("Car info response parsed successfully\n");
                        // TODO: 处理车身信息回复数据
                    } else {
                        printf("Failed to parse car info response (error: %d)\n", resp_result);
                    }
                    continue; /* 跳过后续文本解析 */
                }

                /* BOSCH协议帧解析 - 检测帧头 0xDF 0x07 */
                if (ret >= 14 && recv_buffer[0] == 0xDF && recv_buffer[1] == 0x07) {
                    RadarCommand cmd;
                    int parse_result = parse_radar_command(recv_buffer, ret, &cmd);
                    if (parse_result == 0) {
                        /* 根据命令类型执行相应操作 */
                        switch (cmd.cmd_type) {
                            case RADAR_CMD_ENABLE_OUTPUT:
                                printf("Executing: Enable target info output\n");
                                gTargetInfoOutputEnabled = 1;
                                break;
                            case RADAR_CMD_DISABLE_OUTPUT:
                                printf("Executing: Disable target info output\n");
                                gTargetInfoOutputEnabled = 0;
                                break;
                            case RADAR_CMD_READ_VERSION:
                                printf("Executing: Read version\n");
                                // 发送版本号回复
                                send_radar_response(RADAR_CMD_READ_VERSION, gRadarVersion);
                                break;
                            case RADAR_CMD_SET_START_SPEED:
                                printf("Executing: Set start speed = %d km/h\n", cmd.start_speed);
                                gEgoSpeedThresholdKph = (float)cmd.start_speed;
                                send_radar_response(RADAR_CMD_SET_START_SPEED, (uint32_t)gEgoSpeedThresholdKph);
                                break;
                            case RADAR_CMD_READ_START_SPEED:
                                printf("Executing: Read start speed = %.1f km/h\n", gEgoSpeedThresholdKph);
                                send_radar_response(RADAR_CMD_READ_START_SPEED, (uint32_t)gEgoSpeedThresholdKph);
                                break;
                            case RADAR_CMD_SET_ANGLE_OFFSET:
                                printf("Executing: Set angle offset = %.1f degrees\n", cmd.angle_offset * 0.1f);
                                // TODO: 添加设置角度校正的逻辑，并发送回复
                                // 回复格式: 60 07 03 52 10 0A 55 55 55 55 83 01
                                break;
                            case RADAR_CMD_READ_ANGLE_OFFSET:
                                printf("Executing: Read angle offset\n");
                                // TODO: 添加读取角度校正的逻辑，并发送回复
                                // 回复格式: 60 07 04 51 10 0A 00 00 55 55 6E 01
                                break;
                            default:
                                printf("Unknown command type\n");
                                break;
                        }
                    } else {
                        printf("Failed to parse radar command (error: %d)\n", parse_result);
                    }
                    continue; /* 跳过后续文本解析 */
                }
                
                /*
                 * 重置文本命令缓冲区，防止之前接收的二进制协议数据中的
                 * 可打印字符残留污染本次命令解析。
                 * 注意：不支持跨多次接收的长命令拼接。
                 */
                line_index = 0;
                memset(line_buffer, 0, UART_LINE_BUFFER_SIZE);

                for (uint32_t i = 0; i < ret; i++)
                {
                    uint8_t current_byte = recv_buffer[i];

  					if ((current_byte == '\r') || (current_byte == '\n'))
                    {
                        /* 收到完整命令 */
                        if (line_index > 0)
                        {
                            line_buffer[line_index] = '\0';
                            printf("UART3 Received command: %s\n", (char *)line_buffer);

                            /* 解析并执行命令 */
                            char *tokenizedArgs[CLI_MAX_ARGS] = {0};
                            char *ptrCommand = NULL;
                            char delimiter[] = " \t\r\n";
                            uint32_t argIndex = 0;

                            /* 重置参数数组 */
                            memset((void *)&tokenizedArgs, 0, sizeof(tokenizedArgs));
                            ptrCommand = (char *)&line_buffer[0];

                            /* 解析命令参数 */
                            while (1)
                            {
                                tokenizedArgs[argIndex] = strtok(ptrCommand, delimiter);
                                if (tokenizedArgs[argIndex] == NULL)
                                    break;

                                argIndex++;
                                if (argIndex >= CLI_MAX_ARGS)
                                    break;

                                ptrCommand = NULL;
                            }

                            /* 根据命令类型调用相应的函数 */
                            if (argIndex > 0)
                            {
                                if (strcmp(tokenizedArgs[0], "SetDumpFileName") == 0 ||
                                    strcmp(tokenizedArgs[0], "setdumpfilename") == 0)
                                {
                                    if (argIndex >= 2)
                                    {
                                        printf("Setting dump file name to: %s\n", tokenizedArgs[1]);
                                        SetDumpFileName(argIndex, tokenizedArgs);

                                        /* 发送确认消息 */
                                        char ack_msg[256];
                                        snprintf(ack_msg, sizeof(ack_msg),
                                                 "Dump file name set to: %s\r\nOK\r\n", tokenizedArgs[1]);
                                        uart_general_send((char *)ack_msg, strlen(ack_msg));
                                    }
                                    else
                                    {
                                        printf("Error: Missing filename parameter for SetDumpFileName\n");
                                        char err_msg[] = "Error: Missing filename parameter\r\nUsage: SetDumpFileName <filename>\r\nOK\r\n";
                                        uart_general_send((char *)err_msg, strlen(err_msg));
                                    }
                                }
                                else if (strcmp(tokenizedArgs[0], "uds") == 0)
                                {
                                    printf("UART3 Executing UDS command\n");
                                    uart3_uds_cmd(argIndex, tokenizedArgs);
                                }
                                else if (strcmp(tokenizedArgs[0], "otaUpgrade") == 0 ||
                                         strcmp(tokenizedArgs[0], "ota") == 0)
                                {
                                    printf("UART3 OTA upgrade command received\n");
                                    /*
                                     * 此命令来自 UART3 物理串口，传入 OTA_TRANSPORT_UART。
                                     * ota_cmd_handler 内部会挂起当前 UART3 接收任务，
                                     * 因此调用后立即返回，不阻塞此循环。
                                     */
                                    if (otaUpgrade_cmd(argIndex, tokenizedArgs) != 0) {
                                        printf("UART3: otaUpgrade_cmd failed\n");
                                    }
                                }
                                else if (strcmp(tokenizedArgs[0], "WARN") == 0 ||
                                         strcmp(tokenizedArgs[0], "warn") == 0)
                                {
                                    if (argIndex >= 2)
                                    {
                                        printf("UART3 Executing WARN test with parameter: %s\n", tokenizedArgs[1]);
                                        warn_test_from_binary_string(tokenizedArgs[1]);
                                        // char ack_msg[256];
                                        // snprintf(ack_msg, sizeof(ack_msg),
                                        //          "WARN test executed: %s\r\nOK\r\n", tokenizedArgs[1]);
                                        // uart_general_send((char *)ack_msg, strlen(ack_msg));
                                    }
                                    else
                                    {
                                        printf("Error: Missing parameter for WARN command\n");
                                        // char err_msg[] = "Error: Missing parameter for WARN command\r\n"
                                        //                  "Usage: WARN <8-bit binary string>\r\n"
                                        //                  "Example: WARN 10110111\r\n"
                                        //                  "OK\r\n";
                                        // uart_general_send((char *)err_msg, strlen(err_msg));
                                    }
                                }
                                else
                                {
                                    printf("Unknown command: %s\n", tokenizedArgs[0]);
                                    char unknown_msg[] = "Error: Unknown command\r\nOK\r\n";
                                    uart_general_send((char *)unknown_msg, strlen(unknown_msg));
                                }
                            }
                        }

                        line_index = 0; /* 重置行索引 */
                    }
                    else if (current_byte >= 32 && current_byte <= 126)
                    {
                        /* 只存储可打印字符 */
                        if (line_index < (UART_LINE_BUFFER_SIZE - 1))
                        {
                            line_buffer[line_index++] = current_byte;
                        }
                    }
                }
            }
        }

        /* 短暂延时，避免忙等待 */
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    printf("UART3 receive task ended\n");
    vTaskDelete(NULL);
}

static int uart_recv_task_init(void)
{
    if (uart_recv_task_running)
    {
        printf("UART3 receive task already running\n");
        return -1;
    }

    /* 创建信号量 */
    uart_recv_sem = xSemaphoreCreateMutex();
    if (uart_recv_sem == NULL)
    {
        printf("Failed to create UART3 receive semaphore\n");
        return -1;
    }

    uart_recv_task_running = 1;

    if (xTaskCreate(uart_recv_task, "UART3_RECV", 4096, NULL, 2, &uart_recv_task_handle) != pdPASS)
    {
        printf("Failed to create UART3 receive task\n");
        uart_recv_task_running = 0;
        vSemaphoreDelete(uart_recv_sem);
        uart_recv_sem = NULL;
        return -1;
    }

    printf("UART3 receive task initialized successfully\n");
    return 0;
}

static void uart_recv_task_deinit(void)
{
    if (uart_recv_task_running)
    {
        uart_recv_task_running = 0;

        if (uart_recv_task_handle)
        {
            vTaskDelete(uart_recv_task_handle);
            uart_recv_task_handle = NULL;
        }

        if (uart_recv_sem)
        {
            vSemaphoreDelete(uart_recv_sem);
            uart_recv_sem = NULL;
        }
    }
}

#if (USE_USB_OUTPUT == 1)
void save_frame_pkt_update_proc(void *rawdata, uint32_t len, uint32_t frameID, int pool_index)
{
    gFrameBuf = g_memoryPool[pool_index].saver_pkt_buf_raw;
    // memcpy(gFrameBuf, rawdata, len);
    gFrameBufSize = len;
    gFrameID = frameID;

    data_ready = 1;

    if (ota_is_busy()) return;

    int ret = usb_serial_send((char *)gFrameBuf, gFrameBufSize);
    if (ret < 0) {
        printf("ERROR: USB serial send failed, ret=%d\n", ret);
        return;
    }
    if (ret < gFrameBufSize) {
        printf("WARNING: USB serial partial send: sent %d/%d bytes\n", ret, gFrameBufSize);
    }

}
#endif

/* ======================== OTA 升级传输接口 ======================== */

/**
 * @brief 原始UART发送（绕过USB/CLI路由，直接操作UART3硬件）
 * @param buf 数据缓冲区
 * @param size 数据大小
 * @return 成功返回发送字节数，失败返回-1
 */
int uart_raw_send(const char *buf, int size, int timeout_ms)
{
    if (!buf || size <= 0) {
        printf("ERROR: Invalid buffer or size in uart_raw_send\n");
        return -1;
    }

    return uart_send_timeout(&uart3_cfg, buf, size, timeout_ms);
}

/**
 * @brief 原始UART接收（绕过CLI解析，直接读取UART3硬件）
 * @param buf 接收缓冲区
 * @param size 期望接收字节数
 * @param timeout_ms 超时时间（毫秒）
 * @return 成功返回接收字节数，超时返回0，失败返回-1
 */
int uart_raw_receive(uint8_t *buf, int size, int timeout_ms)
{
    if (!buf || size <= 0) {
        printf("ERROR: Invalid buffer or size in uart_raw_receive\n");
        return -1;
    }

    return uart_receive_timeout(&uart3_cfg, (char *)buf, size, timeout_ms);
}

/**
 * @brief 挂起UART3接收任务（OTA升级前调用，防止与OTA协议冲突）
 *
 * 使用 FreeRTOS vTaskSuspend 挂起接收任务，任务不会释放信号量，
 * 因此 OTA 线程可以直接使用 uart_receive_timeout 读取 UART3 硬件，
 * 不存在竞争条件。
 */
void uart_suspend_recv_task(void)
{
    if (uart_recv_task_handle != NULL) {
        vTaskSuspend(uart_recv_task_handle);
        // printf("UART3 receive task suspended\n");
    }
}

/**
 * @brief 恢复UART3接收任务（OTA升级完成后调用）
 */
void uart_resume_recv_task(void)
{
    if (uart_recv_task_handle != NULL) {
        vTaskResume(uart_recv_task_handle);
        // printf("UART3 receive task resumed\n");
    }
}

/**
 * @brief 重置UART3接收等待状态（OTA升级挂起任务前调用）
 *
 * 在挂起 UART3 接收任务后调用，清理 uart_receive_timeout 内部可能持有的
 * rx_mutex、rx_wait、rx_busy 状态，防止后续 OTA 升级操作因 mutex 被
 * 挂起任务持有而阻塞。
 *
 * 此函数是 uart_reset_read_wait(&uart3_cfg) 的无参包装，
 * 用于与 ota_transport_iface_t.reset_read_wait 回调签名匹配。
 */
void uart_reset_read_wait_ab(void)
{
    uart_reset_read_wait(&uart3_cfg);
}

/**
 * @brief 重置UART3发送等待状态（OTA升级挂起发送线程后调用）
 *
 * 在挂起 UART3 发送线程后调用，清理 uart_send 内部可能持有的
 * tx_mutex、tx_wait、tx_busy 状态，防止后续 OTA 升级操作因 mutex 被
 * 挂起线程持有而阻塞。
 *
 * 此函数是 uart_reset_write_wait(&uart3_cfg) 的无参包装，
 * 用于与 ota_transport_iface_t 的回调签名匹配。
 */
void uart_reset_write_wait_ab(void)
{
    uart_reset_write_wait(&uart3_cfg);
}

/***************************** BOSCH协议 ******************************/

/**
 * @brief 发送雷达命令回复
 * @param cmd_type 命令类型
 * @param data 回复数据（版本号、速度值或角度偏移）
 * @return 成功返回0，失败返回错误码
 * 
 * 回复格式: 60 07 Len Ctrl 10 Cmd 00 xx xx xx SumCheckL SumCheckH
 *           |    |    |    |   |   |    |        |
 *           |    |    |    |   |   |    |        +-- 校验和(低字节,高字节)
 *           |    |    |    |   |   |    +-- 数据内容
 *           |    |    |    |   |   +-- 保留位
 *           |    |    |    |   +-- DID
 *           |    |    |    +-- 控制标志(51=读响应, 52=写响应)
 *           |    |    +-- 数据长度
 *           |    +-- 帧头
 *           +-- 帧头
 */
static int send_radar_response(RadarCommandType cmd_type, uint32_t data)
{
    uint8_t frame[12];
    
    // 帧头
    frame[0] = 0x60;
    frame[1] = 0x07;
    
    // DID
    frame[4] = 0x10;
    
    // 根据命令类型设置数据长度、控制标志、命令码和数据内容
    switch (cmd_type) {
        case RADAR_CMD_READ_VERSION:
            frame[2] = 0x03;      // 数据长度
            frame[3] = 0x51;      // 控制标志(读响应)
            frame[5] = 0x05;      // 命令码
            frame[6] = 0x00;      // 保留位
            frame[7] = (data >> 16) & 0xFF;  // 版本号高字节
            frame[8] = (data >> 8) & 0xFF;   // 版本号中字节
            frame[9] = data & 0xFF;          // 版本号低字节
            break;
            
        case RADAR_CMD_SET_START_SPEED:
            frame[2] = 0x03;      // 数据长度
            frame[3] = 0x52;      // 控制标志(写响应)
            frame[5] = 0x09;      // 命令码
            frame[6] = 0x55;      // 固定值
            frame[7] = 0x55;      // 固定值
            frame[8] = 0x55;      // 固定值
            frame[9] = 0x55;      // 固定值
            break;
            
        case RADAR_CMD_READ_START_SPEED:
            frame[2] = 0x04;      // 数据长度
            frame[3] = 0x51;      // 控制标志(读响应)
            frame[5] = 0x09;      // 命令码
            frame[6] = 0x00;      // 速度高字节
            frame[7] = 0x00;      // 速度低字节
            frame[8] = 0x55;      // 固定值
            frame[9] = 0x55;      // 固定值
            break;
            
        case RADAR_CMD_SET_ANGLE_OFFSET:
            frame[2] = 0x03;      // 数据长度
            frame[3] = 0x52;      // 控制标志(写响应)
            frame[5] = 0x0A;      // 命令码
            frame[6] = 0x55;      // 固定值
            frame[7] = 0x55;      // 固定值
            frame[8] = 0x55;      // 固定值
            frame[9] = 0x55;      // 固定值
            break;
            
        case RADAR_CMD_READ_ANGLE_OFFSET:
            frame[2] = 0x04;      // 数据长度
            frame[3] = 0x51;      // 控制标志(读响应)
            frame[5] = 0x0A;      // 命令码
            frame[6] = 0x00;      // 角度高字节
            frame[7] = 0x00;      // 角度低字节
            frame[8] = 0x55;      // 固定值
            frame[9] = 0x55;      // 固定值
            break;
            
        default:
            frame[2] = 0x03;
            frame[3] = 0x51;
            frame[5] = 0x00;
            frame[6] = 0x00;
            frame[7] = 0x00;
            frame[8] = 0x00;
            frame[9] = 0x00;
            break;
    }
    
    // 校验和 (Data[2] ~ Data[9] 共8个字节)
    uint16_t checksum = calculate_checksum(&frame[2], 8);
    frame[10] = checksum & 0xFF;     // 低字节
    frame[11] = (checksum >> 8) & 0xFF;  // 高字节
    
    // 发送数据
    return send_data((char *)frame, sizeof(frame));
}

/**
 * @brief 计算校验和
 * @param data 数据指针
 * @param len 数据长度
 * @return 校验和值
 */
static uint16_t calculate_checksum(const uint8_t *data, uint8_t len)
{
    uint16_t sum = 0;
    for (uint8_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

/**
 * @brief 解析车机发送给雷达的命令
 * @param buffer 接收到的数据缓冲区
 * @param length 数据长度
 * @param cmd 解析结果存储结构体
 * @return 成功返回0，失败返回错误码
 */
static int parse_radar_command(const uint8_t *buffer, uint8_t length, RadarCommand *cmd)
{
    if (!buffer || !cmd || length < 14) {
        return -1;
    }

    // 检查帧头
    if (buffer[0] != 0xDF || buffer[1] != 0x07) {
        return -2; // 帧头错误
    }

    // 验证校验和 (data[2] ~ data[9] 共8个字节)
    uint16_t expected_checksum = (buffer[11] << 8) | buffer[10];
    uint16_t actual_checksum = calculate_checksum(&buffer[2], 8);
    if (expected_checksum != actual_checksum) {
        printf("Checksum error: expected 0x%04X, got 0x%04X\n", expected_checksum, actual_checksum);
        return -3; // 校验和错误
    }

    // 解析命令
    cmd->data_length = buffer[2];
    cmd->radar_id = buffer[4];
    cmd->did = buffer[5];

    uint8_t request_type = buffer[3]; // 0x12=写, 0x11=读

    if (request_type == 0x12) {
        // 写入数据请求
        if (cmd->did == 0x09) {
            // 设置启动速度
            cmd->start_speed = buffer[6];
            cmd->cmd_type = RADAR_CMD_SET_START_SPEED;
            printf("Radar command: Set start speed = %d km/h (DID: 0x%02X)\n", cmd->start_speed, cmd->did);
        } else if (cmd->did == 0x0A) {
            // 设置角度校正 (角度范围±5°, 精度0.1°, int8类型)
            cmd->angle_offset = (int8_t)buffer[6];
            cmd->cmd_type = RADAR_CMD_SET_ANGLE_OFFSET;
            printf("Radar command: Set angle offset = %.1f degrees (DID: 0x%02X)\n", cmd->angle_offset * 0.1f, cmd->did);
        } else {
            // 目标信息输出控制
            cmd->control_flag = buffer[6]; // 0x01=开启, 0x00=关闭
            if (cmd->control_flag == 0x01) {
                cmd->cmd_type = RADAR_CMD_ENABLE_OUTPUT;
                printf("Radar command: Enable target output (DID: 0x%02X)\n", cmd->did);
            } else if (cmd->control_flag == 0x00) {
                cmd->cmd_type = RADAR_CMD_DISABLE_OUTPUT;
                printf("Radar command: Disable target output (DID: 0x%02X)\n", cmd->did);
            } else {
                cmd->cmd_type = RADAR_CMD_UNKNOWN;
                printf("Radar command: Unknown control flag (0x%02X)\n", cmd->control_flag);
            }
        }
    } else if (request_type == 0x11) {
        // 读取数据请求
        if (cmd->did == 0x05) {
            cmd->cmd_type = RADAR_CMD_READ_VERSION;
            printf("Radar command: Read version (DID: 0x%02X)\n", cmd->did);
        } else if (cmd->did == 0x09) {
            cmd->cmd_type = RADAR_CMD_READ_START_SPEED;
            printf("Radar command: Read start speed (DID: 0x%02X)\n", cmd->did);
        } else if (cmd->did == 0x0A) {
            cmd->cmd_type = RADAR_CMD_READ_ANGLE_OFFSET;
            printf("Radar command: Read angle offset (DID: 0x%02X)\n", cmd->did);
        } else {
            cmd->cmd_type = RADAR_CMD_UNKNOWN;
            printf("Radar command: Unknown read DID (0x%02X)\n", cmd->did);
        }
    } else {
        cmd->cmd_type = RADAR_CMD_UNKNOWN;
        printf("Radar command: Unknown request type (0x%02X)\n", request_type);
    }

    return 0;
}


//发送协议封包

// 水平角度校正值
static int8_t gAngleOffset = 0;

/**
 * @brief 根据航迹ID获取目标距离
 * @param trkID 航迹ID
 * @return 距离（米），无效返回-1
 */
static float get_track_distance(uint8_t trkID)
{
    if (!g_pPktInfo) {
        return -1.0f;
    }
    
    for (int i = 0; i < g_pPktInfo->trkInfo.header.numTrks; i++) {
        if (g_pPktInfo->trkInfo.trkObj[i].isvalid && 
            g_pPktInfo->trkInfo.trkObj[i].trkID == trkID) {
            // 返回纵向距离（取绝对值）
            return fabs(g_pPktInfo->trkInfo.trkObj[i].x_output);
        }
    }
    return -1.0f;
}

/**
 * @brief 根据距离确定预警级别（与board.c一致，支持正负坐标）
 * @param distance 目标距离（米）
 * @return 0=无预警, 1=二级预警(10-70米,常亮), 2=一级预警(0-10米,闪烁)
 */
static uint8_t get_warning_level(float distance)
{
    // -1 表示无效值，直接返回无预警
    if (distance == -1.0f) {
        return 0;
    }
    // 取绝对值判断（与board.c一致）
    float abs_dist = (distance < 0) ? -distance : distance;
    if (abs_dist > 10.0f && abs_dist <= 70.0f) {
        return 1;  // 二级预警（常亮）- 10~70米
    } else if (abs_dist > 0.0f && abs_dist <= 10.0f) {
        return 2;  // 一级预警（闪烁）- 0~10米
    }
    return 0;  // 无预警
}

/**
 * @brief 封包预警信息并发送
 * @return 成功返回0，失败返回错误码
 */
static int uart_warning_pkt_send(void)
{
    if (!g_pPktInfo) {
        printf("[uart_warning] g_pPktInfo is NULL\n");
        return -1;
    }

    if (ota_is_busy()) return 0;

    uint8_t frame[12] = {0};

    // 帧头
    frame[0] = 0x80;
    frame[1] = 0x05;

    // 数据域
    frame[2] = 0x07;  // 有效数据位为7位
    frame[3] = 0x11;  // 表示读取数据响应
    frame[4] = 0x10;  // 响应对象为BSD雷达

    // Data5: 一级预警 (0-10米), Data6: 二级预警 (10-70米)
    // Bit0:BSD右, Bit1:BSD左, Bit2:LCA右, Bit3:LCA左, Bit4:RCW
    uint8_t level1_bits = 0;  // 一级预警（0-10米）
    uint8_t level2_bits = 0;  // 二级预警（10-70米）
    uint8_t left_distance = 0;   // 左侧预警目标距离
    uint8_t right_distance = 0;  // 右侧预警目标距离
    const WarnResult *warn = &g_pPktInfo->warnInfo.warnResult;
    
    float ego_speed_kph = ((float)g_pPktInfo->egoVlcInfo.egoVelocity_mps / 100.0f) * 3.6f;
    // float ego_speed_kph = ((float)g_pPktInfo->egoVlcInfo.egoVelocity_mps) * 3.6f;    // 读取文件测试时打开


    // BSD右（需要车速 >= 启动阈值）
    if (warn->BSD_right && warn->BSD_right_cnt > 0 && ego_speed_kph >= gEgoSpeedThresholdKph) {
        float distance = get_track_distance(warn->BSD_right_IDs[0]);
        uint8_t level = get_warning_level(distance);
        printf("[uart_warning] BSD_R: ID=%d, dist=%.2f, level=%d\n", 
               warn->BSD_right_IDs[0], distance, level);
        if (level == 2) {
            level1_bits |= 0x01;  // Bit0: BSD右（一级预警）
            right_distance = (uint8_t)distance;  // 记录右侧目标距离
        } else if (level == 1) {
            level2_bits |= 0x01;  // Bit0: BSD右（二级预警）
            right_distance = (uint8_t)distance;  // 记录右侧目标距离
        }
    }

    // BSD左（需要车速 >= 启动阈值）
    if (warn->BSD_left && warn->BSD_left_cnt > 0 && ego_speed_kph >= gEgoSpeedThresholdKph) {
        float distance = get_track_distance(warn->BSD_left_IDs[0]);
        uint8_t level = get_warning_level(distance);
        printf("[uart_warning] BSD_L: ID=%d, dist=%.2f, level=%d\n", 
               warn->BSD_left_IDs[0], distance, level);
        if (level == 2) {
            level1_bits |= 0x02;  // Bit1: BSD左（一级预警）
            left_distance = (uint8_t)distance;  // 记录左侧目标距离
        } else if (level == 1) {
            level2_bits |= 0x02;  // Bit1: BSD左（二级预警）
            left_distance = (uint8_t)distance;  // 记录左侧目标距离
        }
    }

    // LCA右（车道变更辅助，不受车速限制）
    if (warn->LCA_right && warn->LCA_right_cnt > 0) {
        float distance = get_track_distance(warn->LCA_right_IDs[0]);
        uint8_t level = get_warning_level(distance);
        printf("[uart_warning] LCA_R: ID=%d, dist=%.2f, level=%d\n", 
               warn->LCA_right_IDs[0], distance, level);
        if (level == 2) {
            level1_bits |= 0x04;  // Bit2: LCA右（一级预警）
            right_distance = (uint8_t)distance;  // 记录右侧目标距离
        } else if (level == 1) {
            level2_bits |= 0x04;  // Bit2: LCA右（二级预警）
            right_distance = (uint8_t)distance;  // 记录右侧目标距离
        }
    }

    // LCA左（车道变更辅助，不受车速限制）
    if (warn->LCA_left && warn->LCA_left_cnt > 0) {
        float distance = get_track_distance(warn->LCA_left_IDs[0]);
        uint8_t level = get_warning_level(distance);
        printf("[uart_warning] LCA_L: ID=%d, dist=%.2f, level=%d\n", 
               warn->LCA_left_IDs[0], distance, level);
        if (level == 2) {
            level1_bits |= 0x08;  // Bit3: LCA左（一级预警）
            left_distance = (uint8_t)distance;  // 记录左侧目标距离
        } else if (level == 1) {
            level2_bits |= 0x08;  // Bit3: LCA左（二级预警）
            left_distance = (uint8_t)distance;  // 记录左侧目标距离
        }
    }

    // RCW（不受车速限制，0~70米范围预警）
    if (warn->RCW && warn->RCW_cnt > 0) {
        float distance = get_track_distance(warn->RCW_IDs[0]);
        printf("[uart_warning] RCW: ID=%d, dist=%.2f\n", 
               warn->RCW_IDs[0], distance);
        // RCW: 0~70米范围预警（与board.c一致）
        if (distance != -1.0f) {
            float abs_dist = (distance < 0) ? -distance : distance;
            if (abs_dist > 0.0f && abs_dist <= 10.0f) {
                level1_bits |= 0x10;  // Bit4: RCW（一级预警，0-10米）
            } else if (abs_dist > 10.0f && abs_dist <= 70.0f) {
                level2_bits |= 0x10;  // Bit4: RCW（二级预警，10-70米）
            }
        }
    }

    if (0) 
    {
        // 调试信息：打印预警状态
        printf("[uart_warning] ego_speed=%.1f km/h, numTrks=%d\n", 
            ego_speed_kph, g_pPktInfo->trkInfo.header.numTrks);
        printf("[uart_warning] BSD_L=%d(cnt=%d), BSD_R=%d(cnt=%d)\n",
            warn->BSD_left, warn->BSD_left_cnt, 
            warn->BSD_right, warn->BSD_right_cnt);
        printf("[uart_warning] AOA_L=%d(cnt=%d), AOA_R=%d(cnt=%d)\n",
            warn->AOA_left, warn->AOA_left_cnt,
            warn->AOA_right, warn->AOA_right_cnt);
        printf("[uart_warning] LCA_L=%d(cnt=%d), LCA_R=%d(cnt=%d), RCW=%d(cnt=%d)\n",
            warn->LCA_left, warn->LCA_left_cnt,
            warn->LCA_right, warn->LCA_right_cnt,
            warn->RCW, warn->RCW_cnt);

        printf("[uart_warning] level1_bits=0x%02X, level2_bits=0x%02X\n", level1_bits, level2_bits);
    }

    
    frame[5] = level1_bits;  // Data5: 一级预警（0-10米）
    frame[6] = level2_bits;  // Data6: 二级预警（10-70米）

    // Data7: BSD功能启动车速 (单位：km/h)
    frame[7] = (uint8_t)gEgoSpeedThresholdKph;

    // Data8: 当前车速 (单位：km/h，取整)
    frame[8] = (uint8_t)(ego_speed_kph + 0.5f);

    // Data9: 水平角度校正值 (范围±5°, 精度0.1°, int8类型)
    frame[9] = (uint8_t)gAngleOffset;

    // 校验和 (Data[2] ~ Data[9] 共8个字节)
    uint16_t checksum = 0;
    for (uint8_t i = 2; i <= 9; i++) {
        checksum += frame[i];
    }
    frame[10] = checksum & 0xFF;
    frame[11] = (checksum >> 8) & 0xFF;

    // 打印发送的预警包
    printf("[uart_warning_send] frame: ");
    for (uint8_t i = 0; i < sizeof(frame); i++) {
        printf("%02X ", frame[i]);
    }
    printf("\n");

    // 发送预警信息
#if USE_USB_UART
#if (USE_USB_OUTPUT == 0)
    send_data((char *)frame, sizeof(frame));
#else
    int ret = usb_serial_send((char *)frame, sizeof(frame));
    if (ret < 0) {
        printf("ERROR: USB serial send failed, ret=%d\n", ret);
        return -2;
    }
    if (ret < sizeof(frame)) {
        printf("WARNING: USB serial partial send: sent %d/%d bytes\n", ret, sizeof(frame));
    }
#endif
#else
    send_data((char *)frame, sizeof(frame));
#endif

    // 发送预警目标距离信息（根据使能标志决定是否发送）
    if (gTargetInfoOutputEnabled) {
        update_and_send_warning_distance(left_distance, right_distance, 0, 0);
    }

    return 0;
}

/**
 * @brief 设置BSD启动车速阈值
 * @param speed 启动车速（km/h）
 */
void set_ego_speed_threshold(float speed)
{
    gEgoSpeedThresholdKph = speed;
}

/**
 * @brief 设置水平角度校正值
 * @param offset 角度校正值（精度0.1°，范围±5°，即int8范围-50~50对应-5.0°~5.0°）
 */
void set_angle_offset(int8_t offset)
{
    gAngleOffset = offset;
}

/**
 * @brief 更新预警信息并发送
 */
void update_and_send_warning(void)
{
    uart_warning_pkt_send();
}

//预警目标距离数据结构
typedef struct {
    uint8_t left_target_distance;   // Data[2]: 左侧预警目标距离 (精度1m)
    uint8_t right_target_distance;  // Data[3]: 右侧预警目标距离 (精度1m)
    uint8_t left_turn_signal;       // Data[7]: 左侧转向信号
    uint8_t right_turn_signal;      // Data[8]: 右侧转向信号
} WarningTargetDistance;

/**
 * @brief 封包预警目标距离并发送
 * @param info 预警目标距离数据
 * @return 成功返回0，失败返回错误码
 */
static int uart_warning_distance_pkt_send(const WarningTargetDistance *info)
{
    if (!info) {
        return -1;
    }

    uint8_t frame[12] = {0};

    // 帧头
    frame[0] = 0xA0;
    frame[1] = 0x04;

    // 数据域
    frame[2] = info->left_target_distance;
    frame[3] = info->right_target_distance;
    frame[4] = 0;
    frame[5] = 0;
    frame[6] = 0;
    frame[7] = info->left_turn_signal;
    frame[8] = info->right_turn_signal;
    frame[9] = 0;

    // 校验和 (Data[2] ~ Data[9] 共8个字节)
    uint16_t checksum = 0;
    for (uint8_t i = 2; i <= 9; i++) {
        checksum += frame[i];
    }
    frame[10] = checksum & 0xFF;
    frame[11] = (checksum >> 8) & 0xFF;

    // 发送数据
    send_data((char *)frame, sizeof(frame));

    return 0;
}

// 全局预警目标距离变量
static WarningTargetDistance g_warning_distance = {
    .left_target_distance = 0,
    .right_target_distance = 0,
    .left_turn_signal = 0,
    .right_turn_signal = 0
};

/**
 * @brief 更新预警目标距离并发送
 * @param left_distance 左侧预警目标距离
 * @param right_distance 右侧预警目标距离
 * @param left_turn 左侧转向信号
 * @param right_turn 右侧转向信号
 */
void update_and_send_warning_distance(uint8_t left_distance, uint8_t right_distance,
                                       uint8_t left_turn, uint8_t right_turn)
{
    g_warning_distance.left_target_distance = left_distance;
    g_warning_distance.right_target_distance = right_distance;
    g_warning_distance.left_turn_signal = left_turn;
    g_warning_distance.right_turn_signal = right_turn;

    uart_warning_distance_pkt_send(&g_warning_distance);
}

//车身信息请求数据结构
typedef struct {
    uint8_t data_length;    // Data[2]: 有效数据位为7位
    uint8_t request_type;  // Data[3]: 0x12=参数设置请求
    uint8_t radar_id;      // Data[4]: 雷达ID
    uint8_t did;           // Data[5]: 控制DID
    uint8_t reserved[3];   // Data[6-8]: 填充位
    uint8_t left_turn;     // Data[9]: 配置左侧转向信号 (0x00=关闭, 0x01=开启)
    uint8_t right_turn;    // Data[10]: 配置右转向信号
} CarInfoRequest;

/**
 * @brief 封包车身信息请求并发送
 * @param req 车身信息请求数据
 * @return 成功返回0，失败返回错误码
 */
static int uart_car_info_request_pkt_send(const CarInfoRequest *req)
{
    if (!req) {
        return -1;
    }

    uint8_t frame[12] = {0};

    // 帧头
    frame[0] = 0xDF;
    frame[1] = 0x07;

    // 数据域
    frame[2] = req->data_length;
    frame[3] = req->request_type;
    frame[4] = req->radar_id;
    frame[5] = req->did;
    frame[6] = req->reserved[0];
    frame[7] = req->reserved[1];
    frame[8] = req->reserved[2];
    frame[9] = req->left_turn;

    // 校验和 (Data[2] ~ Data[9] 共8个字节)
    uint16_t checksum = 0;
    for (uint8_t i = 2; i <= 9; i++) {
        checksum += frame[i];
    }
    frame[10] = checksum & 0xFF;
    frame[11] = (checksum >> 8) & 0xFF;

    // 发送数据
    send_data((char *)frame, sizeof(frame));

    return 0;
}

/**
 * @brief 发送车身信息请求
 * @param left_turn 左侧转向信号 (0=关闭, 1=开启)
 * @param right_turn 右侧转向信号 (0=关闭, 1=开启)
 */
void send_car_info_request(uint8_t left_turn, uint8_t right_turn)
{
    CarInfoRequest req = {
        .data_length = 0x07,
        .request_type = 0x12,
        .radar_id = 0x10,
        .did = 0x28,
        .reserved = {0x00, 0x00, 0x00},
        .left_turn = left_turn,
        .right_turn = right_turn
    };

    uart_car_info_request_pkt_send(&req);
}

/**
 * @brief 解析车身信息回复
 * @param buffer 接收到的数据缓冲区
 * @param length 数据长度
 * @param resp 解析结果
 * @return 成功返回0，失败返回错误码
 */
static int parse_car_info_response(const uint8_t *buffer, uint8_t length, CarInfoResponse *resp)
{
    if (!buffer || !resp || length < 12) {
        return -1;
    }

    // 检查帧头
    if (buffer[0] != 0x60 || buffer[1] != 0x07) {
        return -2; // 帧头错误
    }

    // 验证校验和
    uint16_t expected_checksum = (buffer[11] << 8) | buffer[10];
    uint16_t actual_checksum = 0;
    for (uint8_t i = 2; i <= 9; i++) {
        actual_checksum += buffer[i];
    }
    if (expected_checksum != actual_checksum) {
        printf("Car info response checksum error: expected 0x%04X, got 0x%04X\n",
               expected_checksum, actual_checksum);
        return -3; // 校验和错误
    }

    resp->data_length = buffer[2];
    resp->response_type = buffer[3];
    resp->radar_id = buffer[4];
    resp->did = buffer[5];
    resp->reserved[0] = buffer[6];
    resp->reserved[1] = buffer[7];
    resp->reserved[2] = buffer[8];
    resp->left_turn = buffer[9];

    printf("Car info response: left_turn=%d, right_turn=%d, DID=0x%02X\n",
           resp->left_turn, resp->reserved[2], resp->did);

    return 0;
}