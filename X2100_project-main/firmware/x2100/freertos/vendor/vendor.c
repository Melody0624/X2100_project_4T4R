/**********************************************************************************
 * Copyright: Copyright © 2025 SenardMicro All Rights Reserved. // 
 * @Author: : wangziqian ziqian.wang@senardmicro.com
 * @Date: : 2025-12-24 13:42:15
 * @LastEditors: : wangziqian ziqian.wang@senardmicro.com
 * @LastEditTime: : 2026-02-06 09:32:13
 * @Description: : ${file_description}
 * Version: V1.0.0
 * Changelog: - v1.0.0 ${create_time} 初始版本
 * 系统名称-V1.0
**********************************************************************************/
#include <stdio.h>
#include <limits.h> 
#include <common.h>
#include <driver/spi.h>
#include <driver/gpio.h>
#include <driver/camera.h>
#include <soc/camera_sensor.h>
#include <os.h>
#include <sys/time.h>
#include <delay.h>
#include <os.h>
// #include <os/freertos/include/FreeRTOS.h>
#include <dfs_posix.h>
#include <os/freertos/include/FreeRTOS.h>
#include <os/freertos/include/semphr.h>
#include <os/freertos/include/task.h>
#include <soc/ccu.h>
#include <cpu/irq.h>
#include <driver/irq.h>

#include "cheetah.h"
#include "heap_malloc.h"
#include "general_functions.h"
#include "perf_logger.h"
#include "radar_functions.h"
#include "memory_pool.h"
#include "algorithm_functions.h"
#include "rcs_estimation.h"
#if WARNING_ENABLE
#include <float.h>
#include "warning_detection.h"
#endif

#include "uart_trans.h"
#include "cli.h"
#include "frames_save.h"
#include "mcp2515.h"
#include "net_client.h"
#include "stdarg.h"
#include  <driver/uart_console.h>
#include <cpu/cpu.h>
#include <driver/uart.h>
#include "ota_trigger_ab.h"

#include <driver/cache.h>
#include <soc/cpm.h>
#include "config_manager.h"
#include "eol_calibration.h"

#include "board.h"
#include "board_calib_config.h"
#include "InitPhaseCalib.h"
#include "cheetah_calib.h"

extern struct sensor_attr *cheetah_get_sensor_attr(void);
extern void cheetah_sensor_dynamic_reconfig(int new_width, int new_height);
float cheetah_get_temperature(int devidx);

// 虚拟数据输入测试功能
#define WARN_REPLAY 				0  // 启用警告测试数据重放

#if (DOT_REPLAY == 1)
#include "frames_save.h"
#include "parse_dat_detInfo.h"
extern int DetFrameNum;
#endif

#if WARN_REPLAY
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dfs_posix.h>

static int g_warn_replay_fd = -1;
static uint32_t g_warn_replay_frame = 0;

// 打开警告测试数据文件
static int warn_replay_open(const char *filename)
{
    g_warn_replay_fd = open(filename, O_RDONLY, 0);
    if (g_warn_replay_fd < 0) {
        printf("警告: 无法打开警告测试数据文件 %s\n", filename);
        return -1;
    }
    printf("成功打开警告测试数据文件: %s\n", filename);
    g_warn_replay_frame = 0;
    return 0;
}

// 从文件读取一帧数据（与上位机extractPktData逻辑一致）
static int warn_replay_read_frame(Mmw_pkt_info *pktInfo)
{
    if (g_warn_replay_fd < 0) {
        return -1;
    }

    // 清零数据
    memset(&(pktInfo->detInfo), 0, sizeof(Mmw_output_message_DetInfo));
    memset(&(pktInfo->trkInfo), 0, sizeof(Mmw_output_message_TrkInfo));
    memset(&(pktInfo->egoVlcInfo), 0, sizeof(Mmw_output_message_EgoVlcInfo));
    memset(&(pktInfo->warnInfo), 0, sizeof(Mmw_output_message_WarnInfo));

    Mmw_output_message_header header;
    int ret = read(g_warn_replay_fd, &header, sizeof(Mmw_output_message_header));
    if (ret != sizeof(Mmw_output_message_header)) {
        printf("警告测试数据读取完毕或出错\n");
        close(g_warn_replay_fd);
        g_warn_replay_fd = -1;
        return -1;
    }

    // 验证magic word
    if (header.magicWord[0] != 0x0102 || header.magicWord[1] != 0x0304 ||
        header.magicWord[2] != 0x0506 || header.magicWord[3] != 0x0708) {
        printf("警告: 无效的magic word\n");
        return -1;
    }

    // 读取TLV数据 - 动态分配缓冲区
    size_t tlv_data_len = header.totalPacketLen - sizeof(Mmw_output_message_header);
    char *buffer = malloc(tlv_data_len);
    if (!buffer) {
        printf("[warn_replay] 内存分配失败，需要 %zu 字节\n", tlv_data_len);
        return -1;
    }

    ret = read(g_warn_replay_fd, buffer, tlv_data_len);
    if (ret < 0) {
        printf("[warn_replay] TLV数据读取失败\n");
        free(buffer);
        return -1;
    }

    char *currentPtr = buffer;
    char *buffer_end = buffer + tlv_data_len;  // 边界检查
    int parse_error = 0;

    for (uint32_t i = 0; i < header.numTLVs; i++) {
        // 边界检查：确保有足够空间读取TLV头部
        if (currentPtr + sizeof(Mmw_output_message_tl) > buffer_end) {
            printf("[warn_replay] TLV头部越界\n");
            parse_error = 1;
            break;
        }

        Mmw_output_message_tl tl;
        memcpy(&tl, currentPtr, sizeof(Mmw_output_message_tl));
        currentPtr += sizeof(Mmw_output_message_tl);

        // 边界检查：确保TLV数据不越界
        if (currentPtr + tl.length > buffer_end) {
            printf("[warn_replay] TLV数据越界，length=%u\n", tl.length);
            parse_error = 1;
            break;
        }

        switch (tl.type) {
            case MMW_OUTPUT_MSG_MOTORCYCLE_DETINFO:
            {
                if (tl.length < sizeof(DetInfo_Header)) {
                    printf("[warn_replay] DETINFO TLV too short\n");
                    parse_error = 1;
                    break;
                }
                memcpy(&pktInfo->detInfo.header, currentPtr, sizeof(DetInfo_Header));
                uint16_t numDets = pktInfo->detInfo.header.numDets;
                if (numDets > MAX_DETECTIONS) numDets = MAX_DETECTIONS;
                size_t detDataSize = sizeof(MotorCycle_DetObj) * numDets;
                memcpy(pktInfo->detInfo.detObj, currentPtr + sizeof(DetInfo_Header), detDataSize);
                break;
            }
            case MMW_OUTPUT_MSG_MOTORCYCLE_TRKINFO:
            {
                if (tl.length < sizeof(TrkInfo_Header)) {
                    printf("[warn_replay] TRKINFO TLV too short\n");
                    parse_error = 1;
                    break;
                }
                memcpy(&pktInfo->trkInfo.header, currentPtr, sizeof(TrkInfo_Header));
                uint8_t numTrks = pktInfo->trkInfo.header.numTrks;
                if (numTrks > MAX_TRACKS) numTrks = MAX_TRACKS;
                size_t trkDataSize = sizeof(MotorCycle_TrkObj) * numTrks;
                memcpy(pktInfo->trkInfo.trkObj, currentPtr + sizeof(TrkInfo_Header), trkDataSize);
                break;
            }
            case MMW_OUTPUT_MSG_MOTORCYCLE_EGOVLCINFO:
                memcpy(&pktInfo->egoVlcInfo, currentPtr, tl.length);
                break;
            case MMW_OUTPUT_MSG_MOTORCYCLE_WARNINFO:
                memcpy(&pktInfo->warnInfo, currentPtr, tl.length);
                break;
            default:
                break;
        }
        currentPtr += tl.length;
    }

    free(buffer);

    if (parse_error) {
        return -1;
    }

    g_warn_replay_frame++;
    printf("读取警告测试帧: %d, 帧数: %d, 检测点: %d, 航迹: %d\n", 
           g_warn_replay_frame, header.frameNumber,
           pktInfo->detInfo.header.numDets, pktInfo->trkInfo.header.numTrks);

    // 调试：打印航迹信息
    for (int i = 0; i < pktInfo->trkInfo.header.numTrks; i++) {
        MotorCycle_TrkObj *trk = &pktInfo->trkInfo.trkObj[i];
        printf("[warn_replay] Trk[%d]: isvalid=%d, trkID=%d, x=%.2f, y=%.2f\n",
               i, trk->isvalid, trk->trkID, trk->x_output, trk->y_output);
    }

    // 调试：打印自身速度
    float ego_speed_mps = (float)pktInfo->egoVlcInfo.egoVelocity_mps;
    float ego_speed_kph = ego_speed_mps * 3.6f;
    printf("[warn_replay] 自身速度: %.2f m/s (%.1f km/h), pktInfo->egoVlcInfo.egoVelocity_mps = %.2f\n", ego_speed_mps, ego_speed_kph, pktInfo->egoVlcInfo.egoVelocity_mps);

    // 调试：打印预警信息
    WarnResult *warn = &pktInfo->warnInfo.warnResult;
    printf("[warn_replay] 预警信息: BSD_L=%d, BSD_R=%d, AOA_L=%d, AOA_R=%d, LCA_L=%d, LCA_R=%d, RCW=%d\n",
           warn->BSD_left, warn->BSD_right, warn->AOA_left, warn->AOA_right,
           warn->LCA_left, warn->LCA_right, warn->RCW);
    printf("[warn_replay] TTC_min=%.2fs, BSD_L_cnt=%d, BSD_R_cnt=%d, AOA_L_cnt=%d, AOA_R_cnt=%d, LCA_L_cnt=%d, LCA_R_cnt=%d, RCW_cnt=%d\n",
           warn->TTC_min, warn->BSD_left_cnt, warn->BSD_right_cnt,
           warn->AOA_left_cnt, warn->AOA_right_cnt, warn->LCA_left_cnt,
           warn->LCA_right_cnt, warn->RCW_cnt);
    printf("[warn_replay] LCA_L_level=%d, LCA_R_level=%d\n",
           warn->LCA_left_level, warn->LCA_right_level);
    if (warn->RCW_cnt > 0) {
        printf("[warn_replay] RCW_IDs: ");
        for (int i = 0; i < warn->RCW_cnt; i++) {
            printf("%d ", warn->RCW_IDs[i]);
        }
        printf("\n");
    }

    return 0;
}

// 初始化警告测试数据重放
static void warn_replay_init(void)
{
    warn_replay_open("/mmcblk2p0/Record_20260615_115532.dat");
}
#endif

extern int gadget_usb_serial_test(void);
extern void release_irq(int irq);
extern void arch_enable_fpu(void);
/**************************************CPU 1 veriables********************************************/
// int watch[10] = {0};
#define CPU1_ENABLE 0
static int first_frame_flg = 1;
static int current_pool_index = 0;
static int last_pool_index = 0;
struct mutex cpu1_mutex;
static int frame_count = 0;
// static int cpu1_should_run = 1;
static volatile int cpu1_has_finished = 0;
// static int cpu1_uart_initialized = 0;
// static int cpu1_running = 0;
static int cpu1_detection_result = 0;
static SemaphoreHandle_t cpu0_mailbox_sem = NULL;

static int frmCnt = 1;
static int maxFrames = -1;				// 减少帧数以加速测试
static volatile int isNewTest = 0;
static int cheetah_running_flg = 0;
static uint8_t g_outRawDataFlg = 0;		// 输出Raw Data标志，上电时从Flash读取一次，之后不再切换
static volatile int g_host_config_ready = 0;
static volatile uint8_t g_reconfig_stop_pending = 0;

/* 网络发送完成标志，每 pool 一个，用于 nocopy 模式下确保 buffer 不被提前复用 */
static volatile int g_net_send_done[MAXPOOLNUM] = {1};

#define SOFTWARE_VERSION "V1.4.1"
#define PRODUCT_PLATFORM "X2100"
#define PRODUCT_VERSION "01_04_01"
#define PRODUCT_NAME "MotorCycle_Ingenic"
#define RELEASE_DATE "2026-09-28"
/* ---------------- CPU0 (FreeRTOS) Mailbox Logic ---------------- */
static void cpu0_mailbox_isr(int irq, void *data)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    ccu_write_reg(CCU_MBR(0), 0); // Clear interrupt flag
    if (cpu0_mailbox_sem) {
        xSemaphoreGiveFromISR(cpu0_mailbox_sem, &xHigherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void cpu0_mailbox_init(void)
{
    if (cpu0_mailbox_sem == NULL) {
        cpu0_mailbox_sem = xSemaphoreCreateBinary();
    }
    release_irq(IRQ_V_IP3);
    request_irq_disabled(IRQ_V_IP3, 0, cpu0_mailbox_isr, "cpu0_mbox", NULL);
    arch_enable_irq_nolock(IRQ_V_IP3); 
}
static int cpu1_init(void);
static void Cpu1Thread(void *data);
static void cpu1_delay_us(unsigned int us);
// static void cpu1_delay_ms(unsigned int ms);
static void cpu1_main_loop(void);
static void update_pool_index(void);
void update_frmCnt(int input_cnt);

// CPU1专用UART配置（与参考文件一致，避免冲突）
// #define CPU1_UART_TX_ONCE_SIZE       (256)
// #define CPU1_UART_TX_BUF_SIZE        (8 * 1024)
// #define CPU1_UART_TIMEOUT_MS         1
// #define CPU1_UART_CFG_ID             2
// #define CPU1_UART_CFG_BAUD_RATE      3000000

// static struct uart_config cpu1_uart_config = {
//     .uart_id = CPU1_UART_CFG_ID,
//     .data_bits = 8,
//     .stop_bits = 1,
//     .loop_mode = 0,
//     .tx_poll_mode = 1,
// #ifdef CONFIG_UART_CONSOLE_RX_MODE_POLL
//     .rx_poll_mode = 1,
// #else
//     .rx_poll_mode = 0,
// #endif
//     .parity = UART_PARITY_NONE,
//     .follow_contrl = UART_FC_NONE,
//     .baud_rate = CPU1_UART_CFG_BAUD_RATE,
// };

/* OTA semaphore for main loop blocking during OTA upgrade */
SemaphoreHandle_t ota_sem;

/* Phase calibration ADC data ready semaphore */
SemaphoreHandle_t calib_adc_sem = NULL;

// ADC完成通知机制
struct mutex adc_mutex;
volatile int adc_finished = 0;

struct mutex can_mutex;
volatile int can_finished = 0;

volatile int g_is_eol_mode = 0; // 0: Normal operation, 1: EOL calibration mode

struct mutex curFrame_mutex;
volatile int curFrame_is_processing = 0;
GlbCtx* glbCtx[MAXPOOLNUM];

#define CHEETAH_SAVE 			0
#define USE_WARN_LIGHT			1
#define USE_UART_TRANS			1
// #define USE_UART_CLI			1
#define USE_CAN_TRANS			0

#define USE_NET					0
#ifdef CONFIG_X2000_MAC0
#define USE_NET					1
#define NET_TEST				0	// 启用测试函数
#define USE_WARN_LIGHT			0	// 报警灯与网口在PC1、PC2、PC3冲突，不能同时启用
/* 保留 USE_UART_TRANS=1 (Line 368)，UART3 作为命令通道不能关闭 */
#define USE_UART_CLI			0
#endif

/* 临时关闭逐帧运行日志；最终发布前改为 1 恢复。 */
#define FRAME_RUNTIME_LOG_ENABLE 1
/* 调试移相器校准期间临时关闭点云输出；调试完成后改为 1 恢复。 */
#define POINT_CLOUD_OUTPUT_ENABLE 1

volatile uint8_t g_ps_cal_mode = 0;
volatile uint8_t g_ps_cal_local_mode = 0;
volatile int g_ps_cal_ready_pool_index = -1;
volatile int g_ps_cal_capture_start_error = 0;
volatile uint8_t g_wait_for_sensor_start = 0;
static volatile uint8_t g_ps_cal_mem_ready = 0;
static uint32_t g_ps_cal_saved_chirps;
static uint32_t g_ps_cal_saved_adc_len;
size_t g_total_adc_size = 0;
struct camera_device *g_camera = NULL;

int radar_capture_host_stop(void);
int radar_capture_reconfig_stop(void);
int radar_capture_host_start(void);


static void radar_config_start(void);

int enter_ps_cal_mode(void)
{
	mutex_lock(&curFrame_mutex);
	g_ps_cal_mode = 1;
	g_ps_cal_mem_ready = 0;
	isNewTest = 0;
	mutex_unlock(&curFrame_mutex);

	return 0;
}

int enter_local_ps_cal_mode(void)
{
	/*
	 * Every local calibration stage carries its own verified wake-up and
	 * power-on sequence.  Enter calibration from the deterministic low-power
	 * state produced by cheetah_stop(), rather than leaving Cheetah active
	 * after the reconfiguration-only reset.
	 */
	int ret = radar_capture_host_stop();

	if (ret != 0)
		return ret;

	mutex_lock(&curFrame_mutex);
	g_ps_cal_saved_chirps = glbCtx[0]->waveParams.numChirps[0];
	g_ps_cal_saved_adc_len = glbCtx[0]->waveParams.adcLen[0];
	g_ps_cal_local_mode = 1;
	g_ps_cal_mode = 1;
	g_ps_cal_mem_ready = 0;
	g_ps_cal_ready_pool_index = -1;
	g_ps_cal_capture_start_error = 0;
	mutex_unlock(&curFrame_mutex);
	return 0;
}

int prepare_local_ps_cal_memory(uint32_t num_chirps, uint32_t adc_len)
{
	if (!g_ps_cal_local_mode || num_chirps == 0 || num_chirps > 512 ||
	    adc_len == 0 || adc_len > 506)
		return -EINVAL;

	mutex_lock(&curFrame_mutex);
	g_ps_cal_mem_ready = 0;
	for (int i = 0; i < MAXPOOLNUM; i++) {
		glbCtx[i]->waveParams.numChirps[0] = num_chirps;
		glbCtx[i]->waveParams.adcLen[0] = adc_len;
		free_memory_pool(i);
	}
	mutex_unlock(&curFrame_mutex);

	return 0;
}

int allocate_local_ps_cal_memory(uint32_t adc_frame_size)
{
	int pool_ret = 0;

	if (!g_ps_cal_local_mode || adc_frame_size == 0)
		return -EINVAL;

	mutex_lock(&curFrame_mutex);
	for (int i = 0; i < MAXPOOLNUM; i++) {
		pool_ret = init_memory_pool(glbCtx[i], i, 0, adc_frame_size);
		if (pool_ret != 0)
			break;
	}
	if (pool_ret == 0) {
		g_ps_cal_mem_ready = 1;
	} else {
		for (int i = 0; i < MAXPOOLNUM; i++)
			free_memory_pool(i);
	}
	mutex_unlock(&curFrame_mutex);

	return pool_ret;
}

int configure_ps_cal_memory(uint32_t num_chirps, uint32_t adc_len,
			    uint32_t adc_frame_size)
{
	int pool_ret = 0;

	if (!g_ps_cal_mode || num_chirps == 0 || num_chirps > 512 ||
	    adc_len == 0 || adc_len > 506 || adc_frame_size == 0)
		return -EINVAL;

	mutex_lock(&curFrame_mutex);
	g_ps_cal_mem_ready = 0;
	mutex_unlock(&curFrame_mutex);

	mutex_lock(&curFrame_mutex);
	for (int i = 0; i < MAXPOOLNUM; i++) {
		glbCtx[i]->waveParams.numChirps[0] = num_chirps;
		glbCtx[i]->waveParams.adcLen[0] = adc_len;
	}

	for (int i = 0; i < MAXPOOLNUM; i++) {
		free_memory_pool(i);
		pool_ret = init_memory_pool(glbCtx[i], i, 0, adc_frame_size);
		if (pool_ret != 0)
			break;
	}
	if (pool_ret == 0) {
		g_ps_cal_mem_ready = 1;
	} else {
		for (int i = 0; i < MAXPOOLNUM; i++)
			free_memory_pool(i);
	}
	mutex_unlock(&curFrame_mutex);

	return pool_ret;
}


#if BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_4T4R
#if CHEETAH_CONFIG != cheetah_128_512_config || WAVERANGE_CONFIG != 120
#error "Source 4T4R RF table supports 128x506 at 16.699 MHz/us only"
#endif
#include "cheetah/source4t4r_register_config.h"
#elif CHEETAH_CONFIG == cheetah_128_256_config
static struct reg_line cheetah_default_config[] = {

	{0, 0x2003, 1, {0x000FF000}},
	{0, CMD_DELAY, 1, {0x00000005}},
	{0, 0x1053, 1, {0x04000000}},
	{0, CMD_DELAY, 1, {0x0000000A}},
	{0, 0x1053, 1, {0x00000000}},
	{0, CMD_DELAY, 1, {0x00000064}},

	{0, 0x2007, 1, {0x53B1D4E0}},
	{0, CMD_RESET_RAMS, 1, {0x00000000}},
	{0, 0x108D, 1, {0x000001AB}},
	{0, 0x108E, 1, {0xFC488F8F}},
	{0, 0x108F, 1, {0xFAAAAAAA}},
	{0, 0x1093, 1, {0xAAAAAAAA}},
	{0, 0x1073, 1, {0x881AFFFF}},
	{0, 0x1005, 1, {0xE300FEA1}},
	{0, 0x1006, 1, {0x7D42A688}},
	{0, 0x1007, 1, {0x11A4A044}},
	{0, 0x100A, 1, {0xA6AFAAAB}},
	{0, 0x108C, 1, {0xC3C3FA00}},
	{0, 0x100D, 1, {0xFFAFAFFF}},
	{0, 0x100C, 1, {0x2C000001}},
	{0, 0x109B, 1, {0x10B2A400}},
	{0, 0x1051, 1, {0x8E949270}},
	{0, 0x107A, 1, {0x0ED80000}},
	{0, 0x107B, 1, {0x0ED80000}},
	{0, 0x1079, 1, {0x0ED80000}},
	{0, 0x1059, 1, {0x76FFFFFF}},
	{0, 0x105A, 1, {0xFFFFFFFF}},
	{0, 0x10A1, 1, {0x83941C80}},
	{0, 0x1054, 1, {0x00000000}},
	{0, 0x1040, 1, {0x1DD11DD1}},
	{0, 0x1041, 1, {0x1DD11DD1}},
	{0, 0x1014, 1, {0x07006666}},
	{0, 0x10A0, 1, {0x00120000}},
    //SendConfigInfo 0 1 128 256
	{0, CMD_RESET_RAMS, 1, {0x00000000}},
	{0, CMD_DELAY, 1, {0x0000000A}},
	// BurstProfileRam0
	{0, 0x0000, 24, {0x007C0003, 0x00000000, 0x00000000, 0x82450AD1, 0xA91DD272, 0x94C851F8, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x41EF8045, 0x062E6348, 0x92444596, 0x11E3B501, 0x78EA0D34, 0x665A2248, 0x0C783D15, 0x6585F493, 0xF1E79971, 0x00000000, 0x00000000, 0x00000000}},
	// ChripProfileRam0
	{0, 0x0018, 24, {0x0EE05A4D, 0x402060C8, 0x000FF06C, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	// BurstProfileRam1
	{0, 0x0030, 24, {0x00FC0003, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	// ChripProfileRam1
	{0, 0x0048, 24, {0x8F000000, 0x4061E099, 0x001FF0CC, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	// BeamProfileRam
	{0, 0x0060, 64, {0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693}},
	// DDMAProfileRam
	{0, 0x00A0, 32, {0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0xFA5A80FF, 0x039F7300, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x730080FF, 0x730080FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x039F80FF, 0xFA5A7300, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF}},
	// RfParamRam
	{0, 0x00C0, 4, {0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	// ChirpRam
	{0, 0x00C4, 64, {0xE000D800, 0x18001000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0104, 64, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0144, 64, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0184, 64, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	// TDMRam
	{0, 0x01C4, 64, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0204, 64, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	// BPMRam
	{0, 0x0244, 64, {0x0F0F0F0F, 0x00000000, 0x0F000000, 0x0F0F0F0F, 0x000F0000, 0x000F0F0F, 0x0F0F0F00, 0x000F0000, 0x00000F00, 0x000F000F, 0x00000F0F, 0x00000000, 0x000F000F, 0x0F0F000F, 0x0F0F0000, 0x0F000F00, 0x0F0F000F, 0x000F0F0F, 0x0F0F0000, 0x000F000F, 0x000F0000, 0x00000000, 0x0F0F0F0F, 0x0F0F0F0F, 0x0F0F0F00, 0x000F000F, 0x0F0F0F00, 0x0F000F0F, 0x0F0F0F00, 0x0F0F0F0F, 0x00000F0F, 0x000F000F, 0x0F000F0F, 0x00000000, 0x00000000, 0x0F000F00, 0x000F000F, 0x0F0F0000, 0x0F0F0000, 0x000F0000, 0x0F000000, 0x0F0F000F, 0x0F0F000F, 0x0F0F0F0F, 0x00000000, 0x0000000F, 0x0F000F00, 0x00000F0F, 0x000F0000, 0x0F0F000F, 0x00000F0F, 0x000F0F00, 0x0F000000, 0x0F000000, 0x000F000F, 0x000F0F0F, 0x0000000F, 0x0000000F, 0x000F0000, 0x00000000, 0x0000000F, 0x0F0F0000, 0x0F00000F, 0x0F000F0F}},
	{0, 0x0284, 64, {0x000F0000, 0x0F0F0F00, 0x000F0F0F, 0x00000F0F, 0x0F0F000F, 0x000F0000, 0x000F0F0F, 0x0F0F0000, 0x000F000F, 0x0F000F00, 0x000F0F0F, 0x000F0F0F, 0x0F0F0000, 0x0000000F, 0x0F00000F, 0x00000F00, 0x0000000F, 0x0000000F, 0x0F000F00, 0x0F0F000F, 0x0F000F00, 0x000F0000, 0x0000000F, 0x0F00000F, 0x0F0F0000, 0x00000000, 0x0F0F0F00, 0x00000F00, 0x00000000, 0x00000000, 0x000F0F0F, 0x0F0F0000, 0x0F00000F, 0x0000000F, 0x0F000F00, 0x00000F00, 0x000F0F00, 0x0F0F000F, 0x0000000F, 0x0F0F0000, 0x00000000, 0x0F0F0000, 0x0F00000F, 0x000F0F0F, 0x0000000F, 0x0F00000F, 0x000F0F0F, 0x000F0F0F, 0x0F000F0F, 0x00000F00, 0x0F000000, 0x0F000F0F, 0x00000F0F, 0x0F00000F, 0x000F0F0F, 0x0F000F00, 0x0F0F0F0F, 0x0F00000F, 0x000F000F, 0x00000F0F, 0x0F000000, 0x0F0F0F00, 0x0F000F00, 0x0F000000}},
	// BeamSteeringRam
	{0, 0x02C4, 64, {0x00000000, 0x00000000, 0x757BEFE8, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	// DDMARam
	{0, 0x0304, 48, {0x00C20400, 0x14410000, 0x000000C6, 0x00CA2482, 0x34C30000, 0x000000CE, 0x00D24504, 0x55450000, 0x000000D6, 0x00DA6586, 0x75C70000, 0x000000DE, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0334, 48, {0x00000020, 0x00210000, 0x00000000, 0x00000022, 0x00230000, 0x00000000, 0x00000024, 0x00250000, 0x00000000, 0x00000026, 0x00270000, 0x00000000, 0x00000028, 0x00290000, 0x00000000, 0x0000002A, 0x002B0000, 0x00000000, 0x0000002C, 0x002D0000, 0x00000000, 0x0000002E, 0x002F0000, 0x00000000, 0x00000030, 0x00310000, 0x00000000, 0x00000032, 0x00330000, 0x00000000, 0x00000034, 0x00350000, 0x00000000, 0x00000036, 0x00370000, 0x00000000, 0x00000038, 0x00390000, 0x00000000, 0x0000003A, 0x003B0000, 0x00000000, 0x0000003C, 0x003D0000, 0x00000000, 0x0000003E, 0x003F0000, 0x00000000}},
	{0, 0x0364, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0394, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x03C4, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x03F4, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0424, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0454, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0484, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x04B4, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x04E4, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0514, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0544, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0574, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x05A4, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x05D4, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
#if ((CHEETAH_SAVE && SAVE_RAW_DATA))
	// 4 FPS
	{0, 0x1023, 1, {0x000000F5}},
#else
	// 不存ADC, 帧率提高 15FPS
	{0, 0x1023, 1, {0x0000003E}},
#endif
	{0, 0x1027, 1, {0x04000000}},
#if USE_BPM
	{0, 0x1029, 1, {0x04050000}},
#else
	{0, 0x1029, 1, {0x04010000}},
#endif
	{0, 0x103B, 1, {0x00000000}},
	{0, 0x103C, 1, {0x0000C0E0}},
	{0, 0x103D, 1, {0x0381C000}},
	{0, 0x102A, 1, {0xF3FFFFFF}},
	{0, 0x10B5, 1, {0x03020003}},
	{0, 0x10B6, 1, {0x02020001}},
	{0, 0x10BB, 1, {0x20000002}},
	{0, 0x1051, 1, {0x8E9492B0}},
	{0, 0x1053, 1, {0x04000000}},
	{0, CMD_DELAY, 1, {0x00000064}},
	{0, 0x1053, 1, {0x00000000}},
	{0, CMD_DELAY, 1, {0x00000064}},
	// {0, 0x1053, 1, {0x10000000}},
};
#elif (CHEETAH_CONFIG == cheetah_128_512_config)

static struct reg_line cheetah_default_config[] = {

	{0, 0x2003, 1, {0x000FF000}},
	{0, CMD_DELAY, 1, {0x00000005}},
	{0, 0x1053, 1, {0x04000000}},
	{0, CMD_DELAY, 1, {0x0000000A}},
	{0, 0x1053, 1, {0x00000000}},
	{0, CMD_DELAY, 1, {0x00000064}},

	{0, 0x2007, 1, {0x53B1D4E0}},
	{0, CMD_RESET_RAMS, 1, {0x00000000}},
	{0, 0x108D, 1, {0x000001AB}},
	{0, 0x108E, 1, {0xFC488F8F}},
	{0, 0x108F, 1, {0xFAAAAAAA}},
	{0, 0x1093, 1, {0xAAAAAAAA}},
	{0, 0x1073, 1, {0x881AFFFF}},
	{0, 0x1005, 1, {0xE300FEA1}},
	{0, 0x1006, 1, {0x7D42A688}},
	{0, 0x1007, 1, {0x11A4A044}},
	{0, 0x100A, 1, {0xA6AFAAAB}},
	{0, 0x108C, 1, {0xC3C3FA00}},
	{0, 0x100D, 1, {0xFFAFAFFF}},
	{0, 0x100C, 1, {0x2C000001}},
	{0, 0x109B, 1, {0x10B2A400}},
	{0, 0x1051, 1, {0x8E949270}},
	{0, 0x107A, 1, {0x0ED80000}},
	{0, 0x107B, 1, {0x0ED80000}},
	{0, 0x1079, 1, {0x0ED80000}},
	{0, 0x1059, 1, {0x76FFFFFF}},
	{0, 0x105A, 1, {0xFFFFFFFF}},
	{0, 0x10A1, 1, {0x83941C80}},
	{0, 0x1054, 1, {0x00000000}},
	{0, 0x1040, 1, {0x1DD11DD1}},
	{0, 0x1041, 1, {0x1DD11DD1}},
	{0, 0x1014, 1, {0x07006666}},
	{0, 0x10A0, 1, {0x00120000}},
    //SendConfigInfo 0 1 128 512
	{0, CMD_RESET_RAMS, 1, {0x00000000}},
	{0, CMD_DELAY, 1, {0x0000000A}},
	// BurstProfileRam0
	{0, 0x0000, 24, {0x007C0003, 0x00000000, 0x00000000, 0x007C0803, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x41EF8045, 0x062E6348, 0x92444596, 0x11E3B501, 0x78EA0D34, 0x665A2248, 0x0C783D15, 0x6585F493, 0xF1E79971, 0x00000000, 0x00000000, 0x00000000}},
	// ChripProfileRam0
#if(WAVERANGE_CONFIG == 38)
    {0, 0x0018, 24, {0x8EE05A4D, 0x3021410A, 0x001F9080, 0x8EE05A4D, 0x3022010A, 0x001F9080, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
#elif (WAVERANGE_CONFIG == 60)
    {0, 0x0018, 24, {0x8EE05A4D, 0x302140AA, 0x001F9080, 0x8EE05A4D, 0x302200AA, 0x001F9080, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
#else
	{0, 0x0018, 24, {0x8EE05A4D, 0x30214055, 0x001F9080, 0x8EE05A4D, 0x30220055, 0x001F9080, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
#endif
	// BurstProfileRam1
	{0, 0x0030, 24, {0x00FC0003, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	// ChripProfileRam1
	{0, 0x0048, 24, {0x8F000000, 0x4061E099, 0x001FF0CC, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	// BeamProfileRam
	{0, 0x0060, 64, {0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693, 0x80FF80FF, 0x80FF80FF, 0x6027E337, 0xA9C84693}},
	// DDMAProfileRam
	{0, 0x00A0, 32, {0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0xFC6080FF, 0x02987900, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x730080FF, 0x730080FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF, 0x80FF80FF}},
	// RfParamRam
	{0, 0x00C0, 4, {0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	// ChirpRam
	{0, 0x00C4, 64, {0xE000D800, 0x18001000, 0x00010001, 0x00010001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0104, 64, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0144, 64, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0184, 64, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	// TDMRam
	{0, 0x01C4, 64, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0204, 64, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	// BPMRam
	{0, 0x0244, 64, {0x0F0F0F0F, 0x00000000, 0x0F000000, 0x0F0F0F0F, 0x000F0000, 0x000F0F0F, 0x0F0F0F00, 0x000F0000, 0x00000F00, 0x000F000F, 0x00000F0F, 0x00000000, 0x000F000F, 0x0F0F000F, 0x0F0F0000, 0x0F000F00, 0x0F0F000F, 0x000F0F0F, 0x0F0F0000, 0x000F000F, 0x000F0000, 0x00000000, 0x0F0F0F0F, 0x0F0F0F0F, 0x0F0F0F00, 0x000F000F, 0x0F0F0F00, 0x0F000F0F, 0x0F0F0F00, 0x0F0F0F0F, 0x00000F0F, 0x000F000F, 0x0F000F0F, 0x00000000, 0x00000000, 0x0F000F00, 0x000F000F, 0x0F0F0000, 0x0F0F0000, 0x000F0000, 0x0F000000, 0x0F0F000F, 0x0F0F000F, 0x0F0F0F0F, 0x00000000, 0x0000000F, 0x0F000F00, 0x00000F0F, 0x000F0000, 0x0F0F000F, 0x00000F0F, 0x000F0F00, 0x0F000000, 0x0F000000, 0x000F000F, 0x000F0F0F, 0x0000000F, 0x0000000F, 0x000F0000, 0x00000000, 0x0000000F, 0x0F0F0000, 0x0F00000F, 0x0F000F0F}},
	{0, 0x0284, 64, {0x000F0000, 0x0F0F0F00, 0x000F0F0F, 0x00000F0F, 0x0F0F000F, 0x000F0000, 0x000F0F0F, 0x0F0F0000, 0x000F000F, 0x0F000F00, 0x000F0F0F, 0x000F0F0F, 0x0F0F0000, 0x0000000F, 0x0F00000F, 0x00000F00, 0x0000000F, 0x0000000F, 0x0F000F00, 0x0F0F000F, 0x0F000F00, 0x000F0000, 0x0000000F, 0x0F00000F, 0x0F0F0000, 0x00000000, 0x0F0F0F00, 0x00000F00, 0x00000000, 0x00000000, 0x000F0F0F, 0x0F0F0000, 0x0F00000F, 0x0000000F, 0x0F000F00, 0x00000F00, 0x000F0F00, 0x0F0F000F, 0x0000000F, 0x0F0F0000, 0x00000000, 0x0F0F0000, 0x0F00000F, 0x000F0F0F, 0x0000000F, 0x0F00000F, 0x000F0F0F, 0x000F0F0F, 0x0F000F0F, 0x00000F00, 0x0F000000, 0x0F000F0F, 0x00000F0F, 0x0F00000F, 0x000F0F0F, 0x0F000F00, 0x0F0F0F0F, 0x0F00000F, 0x000F000F, 0x00000F0F, 0x0F000000, 0x0F0F0F00, 0x0F000F00, 0x0F000000}},
	// BeamSteeringRam
	{0, 0x02C4, 64, {0x00000000, 0x00000000, 0x757BEFE8, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	// DDMARam
	{0, 0x0304, 48, {0x00C20400, 0x14410000, 0x000000C6, 0x00CA2482, 0x34C30000, 0x000000CE, 0x00D24504, 0x55450000, 0x000000D6, 0x00DA6586, 0x75C70000, 0x000000DE, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0334, 48, {0x00000020, 0x00210000, 0x00000000, 0x00000022, 0x00230000, 0x00000000, 0x00000024, 0x00250000, 0x00000000, 0x00000026, 0x00270000, 0x00000000, 0x00000028, 0x00290000, 0x00000000, 0x0000002A, 0x002B0000, 0x00000000, 0x0000002C, 0x002D0000, 0x00000000, 0x0000002E, 0x002F0000, 0x00000000, 0x00000030, 0x00310000, 0x00000000, 0x00000032, 0x00330000, 0x00000000, 0x00000034, 0x00350000, 0x00000000, 0x00000036, 0x00370000, 0x00000000, 0x00000038, 0x00390000, 0x00000000, 0x0000003A, 0x003B0000, 0x00000000, 0x0000003C, 0x003D0000, 0x00000000, 0x0000003E, 0x003F0000, 0x00000000}},
	{0, 0x0364, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0394, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x03C4, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x03F4, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0424, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0454, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0484, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x04B4, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x04E4, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0514, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0544, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x0574, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x05A4, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
	{0, 0x05D4, 48, {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000}},
#if ((CHEETAH_SAVE && SAVE_RAW_DATA))
	// 4 FPS
	{0, 0x1023, 1, {0x000000F5}},
#else
	// 不存ADC, 帧率提高 15FPS
#if ADC_REPLAY
	{0, 0x1023, 1, {0x000000F5}},
#else

#if((USE_USB_OUTPUT == 1)&&(UART_ADC_SEND == 1))
	{0, 0x1023, 1, {0x00000050}}, // chirp 发射时间 3.328ms; 0x1023 值： ctx->wave_params.frame_period (50.328ms) - 3.328ms -1ms
    {0, 0x1028, 1, {0xF9F80080}}, // Intersubframe: 
#else
	{0, 0x1023, 1, {0x0000002E}}, // chirp 发射时间 3.328ms; 0x1023 值： ctx->wave_params.frame_period (50.328ms) - 3.328ms -1ms
    {0, 0x1028, 1, {0x8B680080}}, // Intersubframe: 
#endif
#endif
#endif
	{0, 0x1027, 1, {0x04000001}},
	{0, 0x1024, 1, {0x04000000}},
	{0, 0x1025, 1, {0x20000000}},
	// BPM enabled
#if USE_BPM
	{0, 0x1029, 1, {0x04052800}},
#else
	{0, 0x1029, 1, {0x04052800}},
#endif
	// BPM Number = 128
	{0, 0x1033, 1, {0x001FCFE0}},
	// BPM chirp = 1
	{0, 0x1034, 1, {0x0381C000}},
	{0, 0x1035, 1, {0x00000000}},
	{0, 0x103B, 1, {0x00000000}},
	{0, 0x103C, 1, {0x0000C060}},
	{0, 0x103D, 1, {0x0381C000}},
	{0, 0x102A, 1, {0xF3FFF3FF}},
	{0, 0x100E, 1, {0xFFFF080C}},
	{0, 0x100F, 1, {0x50A50500}},
	{0, 0x1014, 1, {0x07006666}},
	{0, 0x10A0, 1, {0x00120000}},
	{0, 0x10B5, 1, {0x03020003}},
	{0, 0x10B6, 1, {0x02020001}},
	{0, 0x10BB, 1, {0x20000012}},
	{0, 0x1051, 1, {0x8E9492B0}},
	{0, 0x1053, 1, {0x04000000}},
	{0, CMD_DELAY, 1, {0x00000064}},
	{0, 0x1053, 1, {0x00000000}},
	{0, CMD_DELAY, 1, {0x00000064}},
	// {0, 0x1053, 1, {0x10000000}},
};

#elif (CHEETAH_CONFIG == cheetah_256_512_config)

static struct reg_line cheetah_default_config[] = {

};

#endif

const struct reg_line *x2100_get_normal_radar_config(size_t *count)
{
#if CHEETAH_CONFIG == cheetah_128_512_config
    if (count != NULL)
        *count = sizeof(cheetah_default_config) /
                 sizeof(cheetah_default_config[0]);
    return cheetah_default_config;
#else
    if (count != NULL)
        *count = 0;
    return NULL;
#endif
}

/* 变量声明 */
volatile static GlbCtx glbCtx2;
/* 函数声明 */
GlbCtx* set_parameters(bool isInhighAltitude, bool isFlipInstall_azm, float radarInstallAng, int pool_index);
/* angCalibMat / angCalibMat_back / angFFT_interp 已迁移到 board_calib_config.c，通过 board_calib_config.h 统一管理 */

// 使用编译时间戳命名文件名
static char dump_file_name[256];

void write_dump_file_name(const char* name)
{
	snprintf(dump_file_name, sizeof(dump_file_name), "/mmcblk2p0/%s.bin", name);

	printf("[DUMP_FILE] Dump file name set to: %s\n", dump_file_name);
}

static void init_filename(void)
{
    // 解析编译日期为数字格式
    int year, month, day;
    char month_str[4] = {0};
    
    // 解析格式："Mar 24 2024"
    sscanf(__DATE__, "%3s %d %d", month_str, &day, &year);
    
    // 月份名称转数字
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (month = 0; month < 12; month++)
	{
        if (strncmp(month_str, months[month], 3) == 0)
		{
            month++; // 月份从1开始
            break;
        }
    }
    
    // 生成数字格式日期：YYYYMMDD
    char compile_date[16] = {0};
    snprintf(compile_date, sizeof(compile_date), "%04d%02d%02d", year, month, day);
    
    // 处理编译时间，只保留到分钟
    char compile_time[8] = {0};
    const char *src_time = __TIME__;
    char *dst_time = compile_time;

    int time_chars = 0;
    while (*src_time && time_chars < 5)
	{	// HH:MM 共5个字符
		if (*src_time == ':')
		{
			src_time++;
			time_chars++;
			continue;
		}
		*dst_time++ = *src_time++;
        time_chars++;
    }

	// 格式：adc_YYYYMMDD_HHMM_times.bin
	snprintf(dump_file_name, sizeof(dump_file_name), 
             "/mmcblk2p0/adc_%s_%s_0.bin", 
             compile_date, compile_time);
}

void spi_test(void *data)
{
    cheetah_spi_init();
    // reset_ram(0);
    set_regs_to_target(cheetah_default_config, sizeof(cheetah_default_config) / sizeof(struct reg_line));
    
}

#if NET_TEST

#define TEST_BUF_SIZE 1024
#define TEST_BUF_COUNT 4  // 缓冲区数量应大于队列深度(默认3)，留有余量
static uint8_t g_nocopy_bufs[TEST_BUF_COUNT][TEST_BUF_SIZE];

void net_test(void)
{
	int ret;
	uint32_t send_timeout_ms = 2000;

	/* 1. 状态预检 */
    int timeout = 100;  // 100 * 100ms = 10s
    while (net_client_get_state() != NET_STATE_CONNECTED && timeout-- > 0) {	// 等待网络线程链接完成
        msleep(100);
    }
    if (net_client_get_state() != NET_STATE_CONNECTED) {
        printf("[NET_TEST] Not connected (state=%d), aborting test.\n", net_client_get_state());
        return;
    }

    printf("========== [NET_TEST] TCP Send Test Start ==========\n");

    /* 2. 单次精确发送测试 */
    uint8_t *buf0 = g_nocopy_bufs[0];
    int len0 = snprintf((char*)buf0, TEST_BUF_SIZE, "NET_TEST_FRAME_001_NO_COPY\n");
    
    ret = net_client_send_async_nocopy(buf0, len0, send_timeout_ms);
    if (ret >= 0) {
        printf("[NET_TEST] Single nocopy send SUCCESS: enqueued.\n");
    } else {
        printf("[NET_TEST] Single nocopy send FAILED: ret=%d State=%d\n", ret, net_client_get_state());
    }


    /* 3. 连续脉冲测试（验证稳定性、TCP 窗口与粘包处理） */
    int packetsNum = 50;
    int packetsInterval = 10; // 10ms 间隔
    printf("[NET_TEST] Starting nocopy burst test (%d packets, %dms interval)...\n", packetsNum, packetsInterval);
    
    for (int i = 0; i < packetsNum; i++) {
        // 轮询使用全局 buffer，防止发送线程还没发完就被覆盖
        uint8_t *current_buf = g_nocopy_bufs[i % TEST_BUF_COUNT];
        
        // 模拟填充雷达 ADC 数据 (这里用 snprintf 模拟，实际中是 DMA 填充)
        int len = snprintf((char*)current_buf, TEST_BUF_SIZE, "RADAR_ADC_PKT_%03d_NO_COPY\n", i);
        
        ret = net_client_send_async_nocopy(current_buf, len, send_timeout_ms);
        
        printf("[NET_TEST] Pkt %d: %s (ret=%d)\n", i, (ret >= 0) ? "OK" : "FAIL", ret);
        if (ret < 0 && ret != NET_ERR_TIMEOUT) {
            printf("[NET_TEST] Connection lost or queue error during burst test.\n");
            break;
        }
        msleep(packetsInterval);
    }

    printf("========== [NET_TEST] Test Complete ==========\n\n");

	while(1) {
        msleep(1000);	// 临时测试用的死循环挂起保活
    }
}

#endif

static void radar_config_start(void)
{
    printf("\n========== [RADAR_CONFIG] Loading Configuration ==========\n");

    /* 1. 设置安全上限：最多允许读取的字节数（避免无限制占用内存）*/
    uint32_t max_read_size = MAX_CHEETAH_CFG_SIZE; 
    
    printf("[RADAR_CONFIG] Step 1: Allocating Heap buffer (Max: %u bytes)...\n", max_read_size);
    struct reg_line *read_cfg_buffer = (struct reg_line *)malloc(max_read_size);
    if (!read_cfg_buffer) {
        printf("[RADAR_CONFIG] Error: malloc failed! Fallback to Factory Default.\n");
        cheetah_reset();
        mdelay(500);
        phase_calib_apply_saved_regs(cheetah_default_config,
                                      sizeof(cheetah_default_config) / sizeof(struct reg_line));
        set_regs_to_target(cheetah_default_config, sizeof(cheetah_default_config) / sizeof(struct reg_line));
        return;
    }

    /* 2. 从 Flash 宽泛读取数据 */
    if (param_get(PARAM_CHEETAH_DEFAULT_CFG, read_cfg_buffer, max_read_size) != 0) {
        printf("[RADAR_CONFIG] Error: Flash read failed! Fallback to Factory Default.\n");
        cheetah_reset();
        mdelay(500);
        phase_calib_apply_saved_regs(cheetah_default_config,
                                      sizeof(cheetah_default_config) / sizeof(struct reg_line));
        set_regs_to_target(cheetah_default_config, sizeof(cheetah_default_config) / sizeof(struct reg_line));
        free(read_cfg_buffer);
        return;
    }

    /* 3. 动态解析真实的指令条数 */
    uint32_t cfg_count = 0;
    uint32_t max_possible_count = max_read_size / sizeof(struct reg_line);
    
    for (uint32_t i = 0; i < max_possible_count; i++) {
        // 遇到 0xFF 填充区，说明有效配置指令已结束
        // 注意 chipIdx 是 uint16_t，addr 是 int16_t
        if (read_cfg_buffer[i].chipIdx == 0xFFFF && read_cfg_buffer[i].addr == (int16_t)0xFFFF) {
            break;
        }
        cfg_count++;
    }

    /* 4. 灾难防呆校验：如果解析出 0 条指令，强制回退 */
    if (cfg_count == 0) {
        printf("[RADAR_CONFIG] Warning: Flash config is EMPTY or Corrupted! Fallback to Factory Default.\n");
        cheetah_reset();
        mdelay(500);
        phase_calib_apply_saved_regs(cheetah_default_config,
                                      sizeof(cheetah_default_config) / sizeof(struct reg_line));
        set_regs_to_target(cheetah_default_config, sizeof(cheetah_default_config) / sizeof(struct reg_line));
        free(read_cfg_buffer);
        return;
    }

    /* 5. 一切正常，输出比对日志，并下发给雷达硬件 */
    printf("[RADAR_CONFIG] Valid Config Lines Parsed: %u\n", cfg_count);
    
    // （可选）仅作信息提示，不影响下发逻辑
    uint32_t factory_size = sizeof(cheetah_default_config);
    if (cfg_count * sizeof(struct reg_line) == factory_size && 
        memcmp(cheetah_default_config, read_cfg_buffer, factory_size) == 0) {
        printf("[RADAR_CONFIG] Info: Using Factory Default Configuration.\n");
    } else {
        printf("[RADAR_CONFIG] Info: Using Custom User Configuration from Flash.\n");
    }

    /* Keep both the immediate Flash configuration and the static fallback
     * synchronized.  The main loop and restart paths may use the latter. */
    phase_calib_apply_saved_regs(cheetah_default_config,
                                  sizeof(cheetah_default_config) / sizeof(struct reg_line));
    phase_calib_apply_saved_regs(read_cfg_buffer, cfg_count);
    printf("[RADAR_CONFIG] Step 2: Applying config to Radar hardware...\n");
    cheetah_reset();
    mdelay(500);
    
    // 正式下发从 Flash 解析出的指令
    set_regs_to_target(read_cfg_buffer, cfg_count);
    
    printf("[RADAR_CONFIG] >>> SUCCESS: Config applied! <<<\n");

    /* 6. 释放内存 */
    free(read_cfg_buffer);
    printf("========== [RADAR_CONFIG] Process Complete ==========\n\n");
}

__attribute__((optimize("O3"))) void vendor_init(void *arg)
{
#if BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_4T4R
    /* Keep the complete processing path linked for diagnostic verification. */
    static volatile uint8_t rf_ready = SOURCE_4T4R_RF_READY;
    if (!rf_ready) {
        printf("[4T4R] diagnostic build: live capture disabled\n");
        return;
    }
    printf("[4T4R] EXPERIMENTAL LIVE: 4TX/4RX, FSTEP=0xAB, 16-channel defaults; hardware validation pending\n");
#endif
    // 调用 Flash 读写测试
    // test_flash_rw();

	perf_logger_init();

	ota_trigger_ab_init();
	ota_sem = xSemaphoreCreateBinary();
	calib_adc_sem = xSemaphoreCreateBinary();
	
    // thread_create("spi_test", 2048, spi_test, NULL);
	int index = 0;
    struct camera_info *info;
    int ret;
	// struct timeval det_start_time, det_end_time;

	// 参数设置
	int numSkippedFrms = 0;
	// maxFrames = 10;

	bool isProcDetection = true;	// 标识：是否检测层运算
	bool isProcTracking = true;		// 标识：是否航迹层运算
	
	bool isCalibInstallAng = true;	// 标识：是否校准安装角
	bool isInhighAltitude = false;	// 标识：是否高海拔
#if (BOARD_TYPE == BOARD_TYPE_LONG_STRIP)
	bool isFlipInstall_azm = true;	// 标识：是否翻转安装角-方位
#else
	bool isFlipInstall_azm = false;
#endif

	float radarInstallAng = 0.0f;	// 雷达安装角（度）

	// extern float* winRng;
	// extern float* winDop;
	// extern float* winAz;

#if USE_WARN_LIGHT
	board_init();	// 初始化板级应用
#if WARN_REPLAY
	warn_replay_init();	// 初始化警告测试数据重放
#endif
#endif

	// 初始化参数管理器
	config_manager_init(cheetah_default_config, sizeof(cheetah_default_config),
                        radarInstallAng, (uint8_t)isCalibInstallAng, 
                        (uint8_t)isInhighAltitude, (uint8_t)isFlipInstall_azm);

	// 动态读取 Flash 中的配置项（直接局部读取，不占用全局RAM）
	uint8_t temp_u8;
	if (param_get(PARAM_IS_CALIB_INSTALL_ANG, &temp_u8, 1) == 0 && temp_u8 != 0xFF) {
		isCalibInstallAng = temp_u8;
	}
	if (param_get(PARAM_IS_IN_HIGH_ALTITUDE, &temp_u8, 1) == 0 && temp_u8 != 0xFF) {
		isInhighAltitude = temp_u8;
	}
	if (param_get(PARAM_IS_FLIP_INSTALL_AZM, &temp_u8, 1) == 0 && temp_u8 != 0xFF) {
		isFlipInstall_azm = temp_u8;
	}

	// 上电时读取一次 outRawDataFlg，之后整个上电周期不再切换
	if (param_get(PARAM_OUT_RAW_DATA_FLG, &g_outRawDataFlg, 1) != 0 || g_outRawDataFlg > 1) {
		g_outRawDataFlg = 0;  // Flash被擦除或无效值时默认为0
	}
	printf("[RADAR_CFG] outRawDataFlg: %d (USB output: %s, Raw Data: %s)\n", 
		   g_outRawDataFlg, 
		   USE_USB_OUTPUT ? "enabled" : "disabled",
		   g_outRawDataFlg ? "enabled" : "disabled");

	float mech_ang = 0.0f;
	float calib_ang = 0.0f;

	printf("[RADAR_CFG] Read PARAM_IS_CALIB_INSTALL_ANG from flash: %d\n", isCalibInstallAng);

	// 1. 读取基础安装角（机械偏角）
	if (param_get(PARAM_RADAR_INSTALL_ANG, &mech_ang, 4) == 0) {
		printf("[RADAR_CFG] Read PARAM_RADAR_INSTALL_ANG from flash: %f\n", mech_ang);
		// 校验浮点数合法性
		radarInstallAng = mech_ang;

	}
	// 动态覆盖角度补偿矩阵
	fft_cpx_f32 temp_mat[NUM_VX_SF1];
	if (param_get(PARAM_ANG_CALIB_MAT, temp_mat, sizeof(temp_mat)) == 0) {
		// 防空白校验：如果所有数据的绝对值均小于0.0001，则视为无效，使用默认值
		int invalid_mat = 1;
		for (int i = 0; i < (int)(sizeof(temp_mat) / sizeof(fft_cpx_f32)); i++) {
			if (temp_mat[i].r < -0.0001f || temp_mat[i].r > 0.0001f ||
			    temp_mat[i].i < -0.0001f || temp_mat[i].i > 0.0001f) {
				invalid_mat = 0;
				break;
			}
		}
		if (!invalid_mat) {
			memcpy(angCalibMat, temp_mat, sizeof(angCalibMat));
			memcpy(angCalibMat_back, temp_mat, sizeof(angCalibMat_back));
		} else {
			printf("[RADAR_CFG] PARAM_ANG_CALIB_MAT values too small (<0.0001), using default values\n");
		}
	}
	// 动态覆盖角度FFT配置（angFFT_interp 在 board_calib_config.c 中定义）
	float temp_angFFT[128];
	if (param_get(PARAM_ANG_FFT_CFG, temp_angFFT, sizeof(temp_angFFT)) == 0) {
		// 防空白校验：如果所有数据的绝对值均小于0.0001，则视为无效，使用默认值
		int invalid_fft = 1;
		for (int i = 0; i < (int)(sizeof(temp_angFFT) / sizeof(float)); i++) {
			if (temp_angFFT[i] < -0.0001f || temp_angFFT[i] > 0.0001f) {
				invalid_fft = 0;
				break;
			}
		}
		if (!invalid_fft) {
			memcpy(angFFT_interp, temp_angFFT, sizeof(temp_angFFT));
		} else {
			printf("[RADAR_CFG] PARAM_ANG_FFT_CFG values too small (<0.0001), using default values\n");
		}
	}
	printf("初始化完成: angFFT_interp[0] = %.12f, angCalibMat[0] = (%.6f, %.6f)\n", 
	       angFFT_interp[0], angCalibMat[0].i, angCalibMat[0].r);
	// 初始化板级标定配置（用默认值填充 angCalibMat_back）
	board_calib_init();

	// 2. 独立读取下线校准补偿角，只要值合法即叠加，不依赖算法的整体坐标旋转开关
	if (param_get(PARAM_CALIB_RESULT_ANG, &calib_ang, 4) == 0) {
		printf("[RADAR_CFG] Read PARAM_CALIB_RESULT_ANG from flash: %f\n", calib_ang);
		// 校验校准浮点数合法性
		if (calib_ang >= -20.0f && calib_ang <= 20.0f) {
			radarInstallAng += calib_ang;
		} else {
			printf("[RADAR_CFG] WARNING: calib_ang out of bounds, ignored!\n");
		}
	}
	
	printf("[RADAR_CFG] Final combined radarInstallAng applied: %f\n", radarInstallAng);

    if (param_get(PARAM_MAX_FRAME_CNT, &maxFrames, sizeof(maxFrames)) != 0)
    {
        printf("Error: read maxFrameCnt from Flash failed, using default=0\r\n");
        maxFrames = -1;
    }

    if (maxFrames == (int32_t)0xFFFFFFFF)
    {
        printf("Info: maxFrameCnt in Flash is erased state, using default=0\r\n");
        maxFrames = -1;
    }

    update_frmCnt(maxFrames);
    g_wait_for_sensor_start = 0;
    isNewTest = 0;
    printf("CLI init ok: maxFrameCnt=%d (from Flash)\r\n", maxFrames);
	// 初始化ADC完成mutex
	mutex_init(&adc_mutex);
	adc_finished = 0;

	// 初始化CAN完成mutex
	mutex_init(&can_mutex);
	can_finished = 0;

	// 初始化当前帧完成mutex
	mutex_init(&curFrame_mutex);
	curFrame_is_processing = 0;

    // 设置全局参数
    for(int i = 0; i < MAXPOOLNUM; i++)
	{
		glbCtx[i] = set_parameters(isInhighAltitude, isFlipInstall_azm, radarInstallAng, i);
		if (!glbCtx[i])
		{
			fprintf(stderr, "set_parameters 失败\n");
			return;
		}
		else
		{
			printf("set_parameters glbCtx[%d] 成功\n", i);
		}
	}

	mem_print_system_heap("Before init memory pool");
	
	InitPhaseCalibInit(glbCtx[0]);

	// 初始化全局内存池
    for(int i = 0; i < MAXPOOLNUM; i++)
	{
		// mem_print_system_heap("Processing init memory pool");
		int adc_flag;
#if ((USE_USB_OUTPUT == 1)&&(UART_ADC_SEND == 0)&&(NET_ADC_SEND == 0))
		adc_flag = g_outRawDataFlg;  // USB 按 flash 参数（仅当两路都无 ADC 时）
#else
		adc_flag = 0;  // NET_ADC_SEND 或 UART_ADC_SEND 已由 memory_pool.c 处理
#endif
		printf("[POOL_INIT] i=%d adc_flag=%d UART_ADC=%d NET_ADC=%d USE_NET=%d\n",
		       i, adc_flag, UART_ADC_SEND, NET_ADC_SEND, USE_NET);
		if (init_memory_pool(glbCtx[i], i, adc_flag, 0) != 0)
		{
			printf("内存池初始化失败 glbCtx[%d]\n", i);
			return;
		}
		else
		{
			printf("内存池初始化成功 glbCtx[%d], g_memoryPool[%d].initialized: %d\n", i, i, g_memoryPool[i].initialized);
		}
	}
	mem_print_stats();

	mem_print_system_heap("After init memory pool");

	g_total_adc_size = get_adc_total_size(glbCtx[0]);
	printf("Total ADC size per frame: %zu bytes\n", g_total_adc_size);

	printf("项目信息: %s_%s_%s_%s_%s\n", PRODUCT_VERSION, PRODUCT_PLATFORM, PRODUCT_NAME, SOFTWARE_VERSION, RELEASE_DATE);

	// // 窗函数赋值
	// blackman_window(winRng, WIN_LEN_RANGE_FFT);
	// hanning_window(winDop, WIN_LEN_DOPPLER_FFT);
	// hanning_window(winAz, WIN_LEN_AZIMUTH_FFT);

	if (USE_UART_TRANS)
	{
		uart_open();
	}
	// if (USE_UART_CLI)
	// {
	// 	cli_init(0);
	// }
	if(USE_USB_UART)
	{
		gadget_usb_serial_test();
	}

	if (USE_CAN_TRANS)
	{
		mcp2515_task_init();
	}
#ifdef CONFIG_X2000_MAC0
	if (USE_NET)
	{
		int ret = 0;
		printf("---------- Initializing network start ----------\n");
		ret = net_client_init(3072, 4, 3);
		msleep(500);
		if(ret)
		{
			printf("network init FAIL !! Error Number : %d\n", ret);
		}
		else
		{
			printf("network init SUCCESS !!\n");
		}
		xTaskCreate(net_client_connect_task, "conn", 2048, NULL, 3, NULL);
		printf("---------- Initializing network finish ----------\n");
	}
#endif

	cheetah_spi_init();
    //cheetah_reset();
    //mdelay(500);
    //set_regs_to_target(cheetah_default_config, sizeof(cheetah_default_config) / sizeof(struct reg_line));


    // 执行从Flash读取配置，并配置，启动雷达雷达配置
    radar_config_start();

    g_camera = camera_detect(index);
    if (!g_camera) {
        printf("camera not found\n");
        return;
    }

    info = camera_get_info(g_camera);
    assert(info);

    printf("camera found %s (%dx%d) %d\n", info->name, info->width, info->height, info->frame_align_size);

    ret = camera_power_on(g_camera);
    if (ret < 0) {
        printf("camera failed to power on\n");
        return;
    }

    ret = camera_stream_on(g_camera);
    if (ret < 0) {
        printf("camera failed to stream on\n");
        camera_power_off(g_camera);
        return;
    }
#if (CPU1_ENABLE == 1)
        cpu0_mailbox_init();
        cpu1_init();
#endif
    
	if (CHEETAH_SAVE)
	{
		init_filename();
		// save_init(dump_file_name);
	}

	// cheetah_start();
	cheetah_stop();

	int retry_count = 0;

	// while (frmCnt < maxFrames + numSkippedFrms)
	while(1)
	{
		/* Block when OTA upgrade is in progress, resume after completion (success or failure) */
		if (ota_is_busy()) {
			printf("ota: main loop blocked, waiting for OTA upgrade to complete...\n");
			xSemaphoreTake(ota_sem, portMAX_DELAY);
			printf("ota: main loop resumed, OTA upgrade finished\n");
		}

		// 使用锁来安全地检查条件，防止在更新变量时执行
		mutex_lock(&curFrame_mutex);
		int should_process = (!isNewTest && !g_wait_for_sensor_start &&
				      ((frmCnt <= maxFrames + numSkippedFrms) ||
				       (maxFrames == -1)));
		mutex_unlock(&curFrame_mutex);
		
		if (should_process)
		{
			if(cheetah_running_flg == 0)
			{
				cheetah_running_flg = 1;
				first_frame_flg = 1;
#if !(CHEETAH_CONFIG == cheetah_256_512_config)
				if (g_host_config_ready) {
					g_host_config_ready = 0;
					printf("[HOST_CAPTURE] using configuration supplied by host\n");
				} else if (!g_ps_cal_mode) {
					cheetah_reset();
					mdelay(500);
					set_regs_to_target(cheetah_default_config,
							   sizeof(cheetah_default_config) /
							   sizeof(struct reg_line));
				}
				ret = camera_stream_on(g_camera);
				if (ret < 0) {
					printf("camera failed to stream on\n");
					cheetah_running_flg = 0;
					if (g_ps_cal_local_mode) {
						isNewTest = 1;
						g_wait_for_sensor_start = 1;
						g_host_config_ready = 0;
						g_ps_cal_capture_start_error = ret;
						if (calib_adc_sem != NULL)
							xSemaphoreGive(calib_adc_sem);
						msleep(20);
						continue;
					}
					return;
				}
				msleep(20);
				if (g_ps_cal_local_mode)
					ret = cheetah_start_checked();
				else {
					cheetah_start();
					ret = 0;
				}
				if (ret < 0) {
					printf("[HOST_CAPTURE] Cheetah data-flow start failed: %d\n",
					       ret);
					cheetah_stop();
					camera_stream_off(g_camera);
					cheetah_running_flg = 0;
					if (g_ps_cal_local_mode) {
						isNewTest = 1;
						g_wait_for_sensor_start = 1;
						g_host_config_ready = 0;
						g_ps_cal_capture_start_error = ret;
						if (calib_adc_sem != NULL)
							xSemaphoreGive(calib_adc_sem);
					} else if (g_ps_cal_mode) {
						isNewTest = 1;
						g_wait_for_sensor_start = 1;
						g_host_config_ready = 0;
					}
					msleep(20);
					continue;
				}
#endif

				if (CHEETAH_SAVE)
				{
					// init_filename();
					save_init(dump_file_name);
				}

#if ZU3_FORMAT_DATA_REPLAY == 1
#if ((BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_3T4R) && (CHEETAH_CONFIG == cheetah_128_512_config))
				zu3_adc_init("/mmcblk2p0/21_06_17_3T4R_506x128_bpm_Zu3Format.dat");
#elif ((BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_3T4R) && (CHEETAH_CONFIG == cheetah_256_512_config))
				zu3_adc_init("/mmcblk2p0/21_12_58_3T4R_506x256_bpm_Zu3Format_100.dat");
#elif ((BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_4T4R) && (CHEETAH_CONFIG == cheetah_128_512_config))
				zu3_adc_init("/mmcblk2p0/21_08_21_4T4R_506x128_bpm_Zu3Format.dat");
#elif ((BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_4T4R) && (CHEETAH_CONFIG == cheetah_256_512_config))
				zu3_adc_init("/mmcblk2p0/21_13_54_4T4R_506x256_bpm_Zu3Format_100.dat");
#endif
#endif

				// 跳过前几帧
				if (!g_ps_cal_mode)
					camera_skip_frames(g_camera, 3);
			}

			if (FRAME_RUNTIME_LOG_ENABLE)
				printf("\n############### Frame %d ###############\n", frmCnt);

			update_pool_index();

			mutex_lock(&curFrame_mutex);
			curFrame_is_processing = 1;

			// 记录当前时间
			uint64_t start_time, end_time;
			start_time = get_time_ns();

			struct timeval get_adc_start_time, get_adc_end_time;
			gettimeofday(&get_adc_start_time, NULL);

			info = camera_get_info(g_camera);
			void *buf = NULL;
#if ((ADC_REPLAY == 0) && (ZU3_FORMAT_DATA_REPLAY == 0))
			buf = camera_wait_frame(g_camera);
			if (buf == NULL) {
				camera_frame_error_type err = camera_get_frame_error(g_camera);
				printf("camera get frame error, %d\n", err);
				if (retry_count++ >= 10) {
					printf("camera reset failed permanently (10 consecutive errors). Aborting.\n");
					if (g_ps_cal_mode) {
						uint32_t frame_mode = 0;
						uint32_t int_status = 0;
						int mode_ret = cheetah_reg_read_single(
							0, 0x1053, &frame_mode, CHEETAH_CHECK);
						int int_ret = cheetah_reg_read_single(
							0, 0x10D2, &int_status, CHEETAH_CHECK);
						printf("[PhaseCalib] First-frame timeout Cheetah state: "
						       "0x1053=0x%08x (ret=%d), 0x10D2=0x%08x "
						       "(ret=%d)\n", frame_mode, mode_ret,
						       int_status, int_ret);
						isNewTest = 1;
						g_wait_for_sensor_start = 1;
						g_host_config_ready = 0;
						if (g_ps_cal_local_mode) {
							g_ps_cal_capture_start_error = -ETIMEDOUT;
							if (calib_adc_sem != NULL)
								xSemaphoreGive(calib_adc_sem);
						}
					}
					curFrame_is_processing = 0;
					mutex_unlock(&curFrame_mutex);
					if (g_ps_cal_mode) {
						cheetah_stop();
						msleep(50);
						camera_stream_off(g_camera);
						g_reconfig_stop_pending = 0;
						cheetah_running_flg = 0;
						retry_count = 0;
						continue;
					}
					return;
				}
				
				// if (err == camera_error_dma_error) {
				//     // camera_stream_off(camera); 
				//     camera_stream_on(camera);
				// } else {
				//     // camera_power_off(camera);
				//     camera_power_on(camera);
				//     camera_stream_on(camera);
				// }

				curFrame_is_processing = 0;
				mutex_unlock(&curFrame_mutex);
				continue;
			}
			gettimeofday(&get_adc_end_time, NULL);
			// 计算并输出ADC获取时间
			long get_adc_time_ms = timeval_diff_ms(&get_adc_start_time, &get_adc_end_time);
			if (FRAME_RUNTIME_LOG_ENABLE)
				printf("!!! csi获取ADC总时间: %ld 毫秒\n", get_adc_time_ms);
#endif

			retry_count = 0;

			if(0)
			{
				if (camera_fmt_is_NV12(info->data_fmt)) {
					printf("camera: frame:%p uv_buffer: %p\n", buf, buf + info->uv_data_offset);
				} else {
					printf("uv_buffer: %p\n", buf);
				}
			}
			if(0)
			{
				unsigned char *pchirp;

				// for (int k = 0; k < 4; k ++)
				// {
					pchirp = (unsigned char *)buf;
					for (int j = 0; j < 64; j++)
					{
						printf("%02x ", (pchirp[j]));
						if (j % 16 == 15)
						{
							printf("\r\n");
						}
					}
					printf("\r\n");
				// }
			}

			if(0)
			{
				uint16_t *pchirp;

				// for (int k = 0; k < 4; k ++)
				// {
					pchirp = (uint16_t *)buf;
					for (int j = 0; j < 64; j++)
					{
						printf("%04x ", (pchirp[j] >> 4));
						if (j % 16 == 15)
						{
							printf("\r\n");
						}
					}
					printf("\r\n");
				// }
			}

			if (g_ps_cal_mode)
			{
				uint16_t *pchirp = (uint16_t *)buf;

				for (int j = 0; j < 64; j++)
				{
					printf("%04x ", pchirp[j] >> 4);
					if (j % 16 == 15)
						printf("\r\n");
				}
				printf("\r\n");
			}

			if (isProcDetection) {
#if ADC_REPLAY
				unsigned long offset = (frmCnt - 1) * g_total_adc_size;
				read_frame_raw_data("/mmcblk2p0/Record_20260922_105743_adc.dat", (frmCnt - 1), g_memoryPool[current_pool_index].saver_pkt_buf_raw, g_total_adc_size, offset);
#elif ZU3_FORMAT_DATA_REPLAY
				/* ZU3格式：跳过25712字节文件头，按TLV格式读取ADC帧数据 */
				/* 参考 get_adc_from_dat_file.c 中 get_adc_from_dat_file() 的解析方式 */
				{
					PERF_BEGIN(Read_zu3_adc);
					int zu3_ret = read_zu3_adc_frame_data(frmCnt, g_memoryPool[current_pool_index].saver_pkt_buf_raw, g_total_adc_size);
					if (zu3_ret <= 0) {
						perf_logger_printf("ZU3数据读取失败(帧%u)，跳过本帧\n", frmCnt);
						frmCnt++;
						continue;
					}
					// perf_logger_printf("read_zu3_adc_data: Frame %u, %d bytes\n", frmCnt, zu3_ret);
					PERF_END(Read_zu3_adc, 0);
				}
#else
				uint32_t copy_size = (info && info->frame_size < g_total_adc_size) ?
						     info->frame_size : g_total_adc_size;
				memcpy(g_memoryPool[current_pool_index].saver_pkt_buf_raw, buf, copy_size);
#endif

				PERF_BEGIN(Parse_adc);
				// 普通模式解析ADC供算法使用；移相器校准直接上送完整原始帧
				/* Phase calibration: notify calibration task that raw data is ready */
				if (g_ps_cal_mode && calib_adc_sem != NULL) {
					flush_dcache_all();
					asm volatile ("sync");
					g_ps_cal_ready_pool_index = current_pool_index;
					xSemaphoreGive(calib_adc_sem);
				}

				// 普通模式解析ADC供算法使用；移相器校准保留原始帧供本地算法使用
				if (!g_ps_cal_mode &&
				    extract_ADCData_from_buffer(glbCtx[current_pool_index],
								g_memoryPool[current_pool_index].saver_pkt_buf_raw) != 0) {
					printf("ADC数据读取失败，跳过帧\n");
					break;
				}
				PERF_END(Parse_adc, 0);

				if (CHEETAH_SAVE)
				{
					if (SAVE_RAW_DATA)
					{
						save_frame_pkt_update_raw(buf, g_total_adc_size, frmCnt, current_pool_index);
						printf("\n2) 保存原始数据开始\n");
						//msleep(2);
					}
				}

				// 跳帧处理
				if (frmCnt < numSkippedFrms) {
					frmCnt++;
					continue;
				}

#if (CHEETAH_SAVE == 1) && defined(USE_SAVE_THREAD)
				// 等待ADC完成信号
				// 等待ADC完成信号，超时时间1000ms
				uint64_t start_time_adc = get_time_ns();
				uint64_t timeout_ns_adc = (uint64_t)1000 * 1000000;
				
				while (1) {
					mutex_lock(&adc_mutex);
					if (adc_finished) {
						// ADC已完成，重置标志
						adc_finished = 0;
						mutex_unlock(&adc_mutex);
						break;
					}
					mutex_unlock(&adc_mutex);
					
					// 检查超时
					if (get_time_ns() - start_time_adc > timeout_ns_adc) {
						break;
					}
					
					// 短暂延时避免忙等待
					thread_wait_timeout(10);
				}

				printf("[*** frame %d ***] ADC 等待 总耗时: %.2f 毫秒\n", frmCnt, (get_time_ns() - start_time_adc) / 1000000.0);

#endif
				InitPhaseCalibProcess();
				// gettimeofday(&det_start_time, NULL);
				// printf("\n2) 开始检测处理\n");
#if (CPU1_ENABLE == 1)
				if (!g_ps_cal_mode && frmCnt >= numSkippedFrms)
				{
#if CHEETAH_CONFIG == cheetah_256_512_config
					memcpy(g_memoryPool[current_pool_index].dopplerFFT_res, g_memoryPool[last_pool_index].dopplerFFT_res_tmp,
						   (nextPowerOfTwo(glbCtx[last_pool_index]->wave_params.adcLen) / 2) * nextPowerOfTwo(glbCtx[last_pool_index]->wave_params.numChirps) * glbCtx[last_pool_index]->basic_params.numRxs * sizeof(fft_cpx_f32));
#endif
					perf_logger_printf("[CPU1] det_1 处理...\n");
					flush_dcache_all();
					ccu_write_reg(CCU_MBR(1), 1);
				}
#endif
			}

			if((!first_frame_flg || g_ps_cal_mode) && !g_ps_cal_local_mode)
			{
				// 临界区保护：确保跟踪处理在正确的临界区状态下运行
				// 先退出所有可能的临界区，确保状态干净
				//while (os_is_enter_critical()) {
				//	printf("警告: 检测到嵌套临界区，退出...\n");
				//	os_exit_critical();
				//}

				//os_enter_critical();
				//if(!first_frame_flg)
				//{
					// glbCtx2.adcData = glbCtx[last_pool_index]->adcData;
					// glbCtx2.basic_params = glbCtx[last_pool_index]->basic_params;
					// glbCtx2.detInfo = glbCtx[last_pool_index]->detInfo;
					// glbCtx2.detInfoRD = glbCtx[last_pool_index]->detInfoRD;
					// glbCtx2.dopplerCfarCfg = glbCtx[last_pool_index]->dopplerCfarCfg;
					// glbCtx2.frmInfo = glbCtx[last_pool_index]->frmInfo;
					// glbCtx2.radarInfo = glbCtx[last_pool_index]->radarInfo;
					// glbCtx2.rangeCfarCfg = glbCtx[last_pool_index]->rangeCfarCfg;
					// glbCtx2.wave_params = glbCtx[last_pool_index]->wave_params;
#if (DOT_REPLAY == 0)
				//struct timeval det_2_start_time, det_2_end_time;
				//gettimeofday(&det_2_start_time, NULL);
#if (CPU1_ENABLE == 0)
				// 检测处理
				if (isProcDetection && (frmCnt >= numSkippedFrms) && detection_processing(glbCtx[last_pool_index], angCalibMat_back, last_pool_index) != 0) {
					printf("检测处理失败\n"); break;
				}
#endif
				perf_logger_printf("[CPU0] det_2 处理...\n");

				PERF_BEGIN(Detection_processing_2);
				if (!g_ps_cal_mode && isProcDetection && (frmCnt >= numSkippedFrms) && detection_processing_v2(glbCtx[last_pool_index], angCalibMat_back, last_pool_index) != 0) {
					printf("det_2 处理失败\n"); break;
				}
				PERF_END(Detection_processing_2, 0);
#else
				// 从TLV格式文件中读取当前帧的DETINFO并解析填充到 glbCtx
				// 使用 parse_dat_detInfo 逐帧顺序解析 (利用 FILE* 指针前移特性)
				// 注意: first_frame_flg 跳过了程序第1帧，所以 frmCnt=2 对应文件中第1帧
				static FILE* det_fid = NULL;
				static uint32_t det_file_frm_cnt = 0;
				static bool det_file_eof_reached = false;
				const char* det_filename = "/mmcblk2p0/Record_20260625_155208.dat";
				uint32_t file_frame_id = (uint32_t)((frmCnt > 1) ? (frmCnt - 1) : 1);

				// 首次打开文件 (或之前已经 EOF 被关闭时重新打开)
				if (!det_file_eof_reached && det_fid == NULL) {
					det_fid = fopen(det_filename, "rb");
					if (det_fid == NULL) {
						printf("[DOT_REPLAY] frmCnt=%u, 无法打开 detInfo 文件 %s, detInfo保持默认(0)\n",
							   glbCtx[last_pool_index]->frmInfo.frmID, det_filename);
						// 打开失败，标记为已结束，避免每帧都去尝试打开
						det_file_eof_reached = true;
					} else {
						det_file_frm_cnt = 0;
						printf("[DOT_REPLAY] frmCnt=%u, 已打开 detInfo 文件 %s, 准备按 parse_dat_detInfo 逐帧解析\n",
							   glbCtx[last_pool_index]->frmInfo.frmID, det_filename);
					}
				}

				if (det_fid != NULL) {
					// 当前帧需要是文件中第 file_frame_id 帧; det_file_frm_cnt 表示已经从文件中解析的帧数
					// 若 det_file_frm_cnt 已超过目标帧，说明文件被重置过，这里直接重新对齐 (仅在首次使用时生效)
					bool need_skip = (det_file_frm_cnt < (file_frame_id - 1));

					// 清零目标 detInfo (避免上一帧残留)
					memset(&glbCtx[last_pool_index]->detInfo, 0, sizeof(glbCtx[last_pool_index]->detInfo));

					int parse_ret = 0;

					// 循环调用: 跳过中间帧, 最后一帧解析 DETINFO
					while (det_file_frm_cnt < file_frame_id) {
						bool isSkipFrm = (det_file_frm_cnt < (file_frame_id - 1));
						parse_ret = parse_dat_detInfo(det_fid, glbCtx[last_pool_index], isSkipFrm);
						if (parse_ret == -1) {
							// EOF: 文件结束，关闭文件，标记不再重新打开
							printf("[DOT_REPLAY] frmCnt=%u, parse_dat_detInfo 返回 EOF(%d)，文件读取完毕 (已解析%u帧)\n",
								   glbCtx[last_pool_index]->frmInfo.frmID, parse_ret, det_file_frm_cnt);
							fclose(det_fid);
							det_fid = NULL;
							det_file_eof_reached = true;
							break;
						} else if (parse_ret == -2) {
							printf("[DOT_REPLAY] frmCnt=%u, parse_dat_detInfo 数据格式错误(%d)，停止解析\n",
								   glbCtx[last_pool_index]->frmInfo.frmID, parse_ret);
							fclose(det_fid);
							det_fid = NULL;
							det_file_eof_reached = true;
							break;
						}
						det_file_frm_cnt++;
						(void)need_skip;
					}

					if (parse_ret == 0) {
						printf("[DOT_REPLAY] frmCnt=%u, parse_dat_detInfo 解析完成，detInfo.numDets=%d\n",
							   glbCtx[last_pool_index]->frmInfo.frmID, glbCtx[last_pool_index]->detInfo.numDets);
					} else {
						printf("[DOT_REPLAY] frmCnt=%u, 未获取到有效 detInfo，保持默认(0)\n", glbCtx[last_pool_index]->frmInfo.frmID);
					}
				}
#endif

				//}

				// 分流：进入旁路下线校准或进入正常跟踪处理
				if (g_calib_operation == 1 && (frmCnt >= numSkippedFrms)) {
					// EOL 校准模式（绕过正常跟踪）
					eol_calib_process_frame(&glbCtx[last_pool_index]->detInfo);
				} else if (!g_ps_cal_mode && isProcTracking && (frmCnt >= numSkippedFrms)) {
					PERF_BEGIN(Ego_processing);
					getEgoVehInfo(glbCtx[last_pool_index], last_pool_index); // 获取自车信息（外部传入）
					PERF_END(Ego_processing, 0);

					PERF_BEGIN(Ego_motion_processing);
					compute_ego_motion(glbCtx[last_pool_index]);
					PERF_END(Ego_motion_processing, 0);

					PERF_BEGIN(Post_processing);
					// printf("\n4) 开始后处理\n"
					// 后处理
					detection_post_processing(glbCtx[last_pool_index]);
					// printf("后处理完成\n");
					PERF_END(Post_processing, 0);

					// 进入临界区保护跟踪处理
					PERF_BEGIN(Tracking_processing);
					// printf("\n3) 开始跟踪处理\n");
					if (tracking_processing(glbCtx[last_pool_index], last_pool_index) != 0) {
						printf("跟踪处理失败\n"); break;
					}
					PERF_END(Tracking_processing, 0);
				}
				else
				{
					glbCtx[last_pool_index]->egoVehInfo.egoSpeed = 0.0f;		// 自车速度 (m/s) — 關閉 ego 估計
					// glbCtx[last_pool_index]->egoVehInfo.egoSpeedLat = 0.0f;	// 自车横向速度 (m/s) — 關閉 ego 估計
					compute_ego_motion(glbCtx[last_pool_index]);
                	// 后处理
					detection_post_processing(glbCtx[last_pool_index]);
				}

				/* Final point order after sorting/filtering; no protocol fields added. */
				if (!g_ps_cal_mode)
					rcs_process_frame(glbCtx[last_pool_index], last_pool_index);
				else
					rcs_clear_frame(last_pool_index);

				PERF_BEGIN(Pkt_processing);

				Mmw_pkt_info *pktInfo = update_mmw_pkt_info_from_ctx(glbCtx[last_pool_index], last_pool_index);

#if USE_WARN_LIGHT
#if WARN_REPLAY
				static Mmw_pkt_info replay_pktInfo = {0};
				if (g_warn_replay_fd >= 0) {
					if (warn_replay_read_frame(&replay_pktInfo) == 0) {
						Warn_frame_pkt_update(&replay_pktInfo, g_warn_replay_frame);
					}
				} 
#else
				Warn_frame_pkt_update(pktInfo, frmCnt);
#endif
#endif

				/* 在主任务中统一封包到共享缓冲区，减少memcpy */
				{
#ifdef CONFIG_X2000_MAC0
					/* 等待该 pool 的上一次网络发送完成，防止 buffer 被提前覆盖 */
					if (USE_NET) {
						int wait_cnt = 0;
						while (!g_net_send_done[last_pool_index]) {
							msleep(2);
							if (++wait_cnt > 1000) {
								printf("net send wait timeout! pool=%d\n", last_pool_index);
								break;
							}
						}
					}
#endif
					char *sharedBuf = g_memoryPool[last_pool_index].shared_pkt_buf;
					uint32_t sharedBufSize = g_memoryPool[last_pool_index].shared_pkt_buf_size;
					int32_t sharedPktSize = 0;
					uint32_t send_frame_id = frmCnt;
#if ((USE_USB_OUTPUT == 1)&&(UART_ADC_SEND == 0))
                    if(g_outRawDataFlg == 1 || g_ps_cal_mode == 1)
					{
						// save_frame_pkt_update_proc(buf, total_adc_size, frmCnt, current_pool_index);
						sharedPktSize = adc_packetize_results(g_memoryPool[current_pool_index].saver_pkt_buf_raw, g_total_adc_size, frmCnt, sharedBuf, sharedBufSize, NULL, 0, 0);
						printf("frmCnt: %d, total_adc_size: %zu, sharedPktSize: %d\n", frmCnt, g_total_adc_size, sharedPktSize);
						printf("\n2) 保存处理后的数据开始\n");
						//msleep(2);
					}
					else
                    {
#endif
					if (POINT_CLOUD_OUTPUT_ENABLE) {
#if (DOT_REPLAY == 0)
#if WARN_REPLAY
					// WARN_REPLAY模式下使用replay_pktInfo打包数据，确保包含测试预警信息
					if (g_warn_replay_fd >= 0) {
						sharedPktSize = mmw_packetize_results(&replay_pktInfo, g_warn_replay_frame, sharedBuf, NULL, 0, 0);
						send_frame_id = g_warn_replay_frame;
					} else {
						sharedPktSize = mmw_packetize_results(pktInfo, frmCnt, sharedBuf, NULL, 0, 0);
					}
#else
#if ((USE_USB_OUTPUT == 1)&&(UART_ADC_SEND == 1)) || (USE_NET && NET_ADC_SEND == 1)
					sharedPktSize = mmw_packetize_results(pktInfo, frmCnt, sharedBuf, g_memoryPool[current_pool_index].saver_pkt_buf_raw, g_total_adc_size, MMW_OUTPUT_MSG_ADC_FRAME);
#else
					sharedPktSize = mmw_packetize_results(pktInfo, frmCnt, sharedBuf, NULL, 0, 0);
#endif
#endif
#else
					sharedPktSize = mmw_packetize_results(pktInfo, DetFrameNum, sharedBuf, NULL, 0, 0);
#endif
					}

#if ((USE_USB_OUTPUT == 1)&&(UART_ADC_SEND == 0))
                    }
#endif
					if (!g_is_eol_mode && USE_UART_TRANS && sharedPktSize > 0 && (GetInitPhaseStepFlg() == INIT_PHASE_STEP_IDLE))
					{
#if WARN_REPLAY
						if (g_warn_replay_fd >= 0) {
							uart_frame_pkt_update(&replay_pktInfo, sharedBuf, sharedPktSize, send_frame_id);
						} else {
							uart_frame_pkt_update(pktInfo, sharedBuf, sharedPktSize, send_frame_id);
						}
#else
						uart_frame_pkt_update(pktInfo, sharedBuf, sharedPktSize, send_frame_id);
#endif
					}
					if (!g_is_eol_mode && USE_CAN_TRANS && sharedPktSize > 0 && (GetInitPhaseStepFlg() == INIT_PHASE_STEP_IDLE))
					{
#if WARN_REPLAY
						if (g_warn_replay_fd >= 0) {
							can_frame_pkt_update(&replay_pktInfo, sharedBuf, sharedPktSize, send_frame_id);
						} else {
							can_frame_pkt_update(pktInfo, sharedBuf, sharedPktSize, frmCnt);
						}
#else
#if NEW_PROTOCOL_ENABLE
						CanNewProtocolPkt(pktInfo, frmCnt);
#else
						can_frame_pkt_update(pktInfo, sharedBuf, sharedPktSize, frmCnt);
#endif
#endif
						uint64_t start_time_can = get_time_ns();
						uint64_t timeout_ns_can = (uint64_t)1000 * 1000000;
						while (1)
						{
							mutex_lock(&can_mutex);
							if (can_finished)
							{
								// CAN已完成，重置标志
								can_finished = 0;
								mutex_unlock(&can_mutex);
								break;
							}
							mutex_unlock(&can_mutex);

							// 检查超时
							if (get_time_ns() - start_time_can > timeout_ns_can)
							{
								break;
							}

							// 短暂延时避免忙等待
							thread_wait_timeout(10);
						}

						printf("[*** frame %d ***] CAN 等待 总耗时: %.2f 毫秒\n", frmCnt, (get_time_ns() - start_time_can) / 1000000.0);
					}
#ifdef CONFIG_X2000_MAC0
					if (!g_is_eol_mode && USE_NET && sharedPktSize > 0)
					{
						if (sharedPktSize > (int32_t)sharedBufSize) {
							printf("net_server_send error! Buffer overflow: %d > %u\n", sharedPktSize, sharedBufSize);
						} else {
							g_net_send_done[last_pool_index] = 0;
							int ret = net_client_send_async_nocopy((const uint8_t *)sharedBuf, sharedPktSize, 10,
							                                       &g_net_send_done[last_pool_index]);
							if (ret < 0) {
								printf("net_server_send error! Error Number %d\n", ret);
								g_net_send_done[last_pool_index] = 1;
							}
						}
					}
#endif
					if (CHEETAH_SAVE && !SAVE_RAW_DATA && sharedPktSize > 0)
					{
						save_frame_pkt_update(sharedBuf, sharedPktSize, frmCnt, last_pool_index);
					}
				}
				PERF_END(Pkt_processing, 0);
			}
			else
			{
				first_frame_flg = 0; 	
			}

			// printf("current pool index: %d, last pool index: %d\n", current_pool_index, last_pool_index);

#if (CPU1_ENABLE == 1)
			if (!g_ps_cal_mode && frmCnt >= numSkippedFrms) {
				PERF_BEGIN(Wait_det1_processing);
				xSemaphoreTake(cpu0_mailbox_sem, portMAX_DELAY);
				flush_dcache_all();
				asm volatile ("sync");

				PERF_END(Wait_det1_processing, 0);
			}
#endif

			// PERF_BEGIN(Get_temp_processing);
			float temp = 0;
			if (!g_ps_cal_mode) {
				temp = cheetah_get_temperature(0);
				if (FRAME_RUNTIME_LOG_ENABLE)
					printf("\n温度: %.2f C\n", temp);
			}
			// PERF_END(Get_temp_processing, 0);
			/** 复制跟踪信息到当前内存池的跟踪管理器 */
			glbCtx[current_pool_index]->trackIDManager = glbCtx[last_pool_index]->trackIDManager;
			glbCtx[current_pool_index]->trkInfo = glbCtx[last_pool_index]->trkInfo;

			// printf("确认当前运行的内存池索引: %d, 上一帧的内存池索引: %d\n", current_pool_index, last_pool_index);
			// 检测处理
			if (!g_ps_cal_mode && frmCnt >= numSkippedFrms &&
			    (cpu1_detection_result != 0)) {
				printf("检测处理失败\n"); break;
			}
			// gettimeofday(&det_end_time, NULL);
			// 计算并输出检测处理时间
			// long det_processing_time_ms = timeval_diff_ms(&det_start_time, &det_end_time);
			// printf("!!! 检测处理总时间: %ld 毫秒\n", det_processing_time_ms);

			PERF_BEGIN(Reset_processing);
			// glbCtx2 = *glbCtx[0];
			// 帧后处理：重置参数
#if (DOT_REPLAY == 0)
            ++frmCnt;
            for(int i = 0; i < MAXPOOLNUM; i++)
			{
				glbCtx[i]->frmInfo.lastFrmID = glbCtx[i]->frmInfo.frmID;
				glbCtx[i]->frmInfo.frmID = frmCnt;
			}
#else 
            ++frmCnt;
            for(int i = 0; i < MAXPOOLNUM; i++)
			{
				glbCtx[i]->frmInfo.lastFrmID = glbCtx[i]->frmInfo.frmID;
				glbCtx[i]->frmInfo.frmID = DetFrameNum;
			}
			// printf("compute_ego_motion:frmID: %d, lastFrmID: %d, frame_period: %f\n", glbCtx[current_pool_index]->frmInfo.frmID, glbCtx[current_pool_index]->frmInfo.lastFrmID);
#endif
			// glbCtx2.frmInfo.frmID = glbCtx[0]->frmInfo.frmID;
			memset(&glbCtx[last_pool_index]->detInfo, 0, sizeof(glbCtx[last_pool_index]->detInfo));
			memset(&glbCtx[last_pool_index]->detInfoRD, 0, sizeof(glbCtx[last_pool_index]->detInfoRD));

			camera_put_frame(g_camera, buf);

			PERF_END(Reset_processing, 0);

			end_time = get_time_ns();
			// 计算并输出处理时间
			double processing_time_ms = (end_time - start_time) / 1000000.0;
			if (FRAME_RUNTIME_LOG_ENABLE)
				printf("\n[*** frmCnt %d ***] main thread 总耗时: %.2f 毫秒\n", (frmCnt - 1), processing_time_ms);

			// 刷新并打印所有性能记录
			perf_logger_flush();

			curFrame_is_processing = 0;
			mutex_unlock(&curFrame_mutex);
		}
		else
		{
			isNewTest = 0;
			
			msleep(10);
			if(cheetah_running_flg == 1)
			{
				if (g_reconfig_stop_pending) {
					cheetah_stop_for_reconfig();
				} else {
					cheetah_stop();
				}
				msleep(50);

				if (CHEETAH_SAVE)
				{
					save_close();
				}
#if (ZU3_FORMAT_DATA_REPLAY == 1)
				{
					zu3_close_adc_file();
				}
#endif

				// 清空所有内存池中的跟踪状态和检测结果
				for (int i = 0; i < MAXPOOLNUM; i++)
				{
					memset(&glbCtx[i]->trkInfo, 0, sizeof(glbCtx[i]->trkInfo));
					memset(&glbCtx[i]->trackIDManager, 0, sizeof(glbCtx[i]->trackIDManager));
					memset(&glbCtx[i]->detInfo, 0, sizeof(glbCtx[i]->detInfo));
					memset(&glbCtx[i]->detInfoRD, 0, sizeof(glbCtx[i]->detInfoRD));
					rcs_clear_frame(i);
				}

				// radar_config_start();

				// 跳过前几帧
				// camera_skip_frames(camera, info->frame_nums);

				camera_stream_off(g_camera);
				msleep(20);

				printf("cheetah_stop completed\n");
				g_reconfig_stop_pending = 0;
				cheetah_running_flg = 0;
			}
		}

	}

	cheetah_stop();
  	printf("Processing completed cheetah_stop...\n");

  	//msleep(120); // 等待所有线程结束
	
	if (CHEETAH_SAVE)
	{
	 	save_close();
	}

#if (ZU3_FORMAT_DATA_REPLAY == 1)
	{
		zu3_close_adc_file();
	}
#endif

	if (USE_UART_TRANS)
	{
		uart_close();
	}
#ifdef CONFIG_X2000_MAC0
		if(USE_NET)
		{
			net_client_deinit();
		}
#endif

	for(int i = 0; i < MAXPOOLNUM; i++)
	{
		if (glbCtx[i]->adcData.sf1)
		{
			free(glbCtx[i]->adcData.sf1);
		}
		if (glbCtx[i]->radarInfo.vxPosn_sf1_azm)
		{
			free(glbCtx[i]->radarInfo.vxPosn_sf1_azm);
		}
		free(glbCtx[i]);
    }


	// 释放窗函数数组
	// if (winRng)
	// {
	// 	free(winRng);
	// }
	// if (winDop)
	// {
	// 	free(winDop);
	// }
	// if (winAz)
	// {
	// 	free(winAz);
	// }

	for(int i = 0; i < MAXPOOLNUM; i++)
	{
		// 释放全局内存池
		free_memory_pool(i);
	}
	printf("Run complete !\n");
		
	return;
}
/**************************************CPU 1 function********************************************/

/* 
* Function Name: cpu1_init
* Input: void
* Output: void
* Return: int
* Description: 初始化CPU1，启动save_thread
*/
static int cpu1_init(void)
{
	mutex_init(&cpu1_mutex);

    // [关键] 主核提前开启系统全局加速器电源和总线权限
    arch_enable_fpu();
    ccu_write_reg(0x0fe0, 0xF); // CCU_CFCR: 开启所有核心全特性

    arch_startup_cpu(1, (unsigned long)Cpu1Thread);

    msleep(10);

    printf("CPU 1 is init\n");

	return 0;
}

/* 
* Function Name: Cpu1Thread
* Input: void
* Output: void
* Return: int
* Description: CPU1的save_thread线程
*/
static void Cpu1Thread(void *data)
{
	arch_enable_fpu(); // 保证 CPU1 开启FPU，因为耗时计算需要硬浮点支持
    cpu1_main_loop();
}

/* 
* Function Name: cpu1_delay_us
* Input: unsigned int us
* Output: void
* Return: int
* Description: CPU1的延时函数，单位微秒
*/
static void cpu1_delay_us(unsigned int us)
{
    // 简单的忙等待延时
    volatile unsigned int count = us * 1800;  // 根据CPU频率调整这个系数
    while (count--);
}

/* 
* Function Name: cpu1_delay_ms
* Input: unsigned int ms
* Output: void
* Return: int
* Description: CPU1的延时函数，单位毫秒
*/
static void cpu1_delay_ms(unsigned int ms)
{
    for (unsigned int i = 0; i < ms; i++) {
        cpu1_delay_us(1000);
    }
}

/* 
* Function Name: cpu1_main_loop
* Input: void
* Output: void
* Return: int
* Description: CPU1的主要循环函数
*/
__attribute__((optimize("O3"))) static void cpu1_main_loop(void)
{
    ccu_write_reg(CCU_MBR(1), 0); // Clear mailbox 1

    while (1)
    {
        // [核心优化] 轮询硬件邮箱寄存器，增加空循环减少总线竞争
        while (ccu_read_reg(CCU_MBR(1)) == 0) {
            for (volatile int i = 0; i < 500; i++);
        }
        
        flush_dcache_all();
        asm volatile ("sync");
        ccu_write_reg(CCU_MBR(1), 0); // Ack

		if (g_ps_cal_mode) {
			ccu_write_reg(CCU_MBR(0), 1);
			continue;
		}

        frame_count++;
        
        // 执行雷达检测算法 (支持 MSA 加速)
        // cpu1_detection_result = detection_processing(glbCtx, angCalibMat);
		if (g_ps_cal_mode) {
			cpu1_detection_result = 0;
		} else {
			PERF_BEGIN(Detection_processing_1);
			cpu1_detection_result = detection_processing(glbCtx[current_pool_index], angCalibMat, current_pool_index);
			PERF_END(Detection_processing_1, 1);
		}
		// for (volatile int i = 0; i < 3000000; i++);
		cpu1_delay_us(100);
        
        flush_dcache_all();
        asm volatile ("sync");
        // 运算完成，通知主核 (触发 CPU0 中断)
        ccu_write_reg(CCU_MBR(0), 1);
	}
}

static void update_pool_index(void)
{

	/* 非第一帧，更新 pool_index。普通模式由 CPU1 的 frame_count
	 * 驱动；校准模式绕过 CPU1，改用 CPU0 的 frmCnt 保持三池轮转。 */
	last_pool_index = current_pool_index;
	if (g_ps_cal_mode)
		current_pool_index = frmCnt % MAXPOOLNUM;
	else
		current_pool_index = (frame_count + 1) % MAXPOOLNUM;

	// printf("current_pool_index: %d, last_pool_index: %d\n", current_pool_index, last_pool_index);
}

void update_frmCnt(int input_cnt)
{
	mutex_lock(&curFrame_mutex);
	if (curFrame_is_processing == 1)
	{
		// 等待当前帧处理完成
		while (curFrame_is_processing == 1)
		{
			mutex_unlock(&curFrame_mutex);
			// 短暂延时，避免忙等待
			msleep(1); // 1ms
			mutex_lock(&curFrame_mutex);
		}
	}

	isNewTest = 1;
	maxFrames = input_cnt;
	frmCnt = 1;
	current_pool_index = 0;
	last_pool_index = 0;
	frame_count = 0;
	mutex_unlock(&curFrame_mutex);
}

void configure_host_frame_count(int input_cnt)
{
	/*
	 * Publish the stop request before taking curFrame_mutex.  The capture
	 * thread holds this mutex while camera_wait_frame() is blocking, so
	 * taking the mutex first can deadlock when the radar has stopped
	 * producing frames.
	 */
	isNewTest = 1;
	g_wait_for_sensor_start = 1;

	mutex_lock(&curFrame_mutex);
	maxFrames = input_cnt == 0 ? -1 : input_cnt;
	frmCnt = 1;
	current_pool_index = 0;
	last_pool_index = 0;
	frame_count = 0;
	mutex_unlock(&curFrame_mutex);

	while (cheetah_running_flg == 1)
		msleep(10);
}

int radar_capture_host_stop(void)
{
	int timeout_ms = 5000;
	int was_running = cheetah_running_flg == 1;

	/* See configure_host_frame_count(): wake the capture state machine first. */
	isNewTest = 1;
	g_wait_for_sensor_start = 1;
	g_host_config_ready = 0;

	while (cheetah_running_flg == 1 && timeout_ms > 0) {
		msleep(10);
		timeout_ms -= 10;
	}

	if (cheetah_running_flg == 1)
		return -ETIMEDOUT;

	/* Several configuration layers defensively request stop.  Only report a
	 * real running -> stopped transition; repeated calls while already stopped
	 * are successful no-ops and should not look like repeated hardware stops. */
	if (was_running)
		printf("[HOST_CAPTURE] sensorStop complete: radar and CSI/VIC stopped\n");
	return 0;
}

int radar_capture_reconfig_stop(void)
{
	int ret;

	g_reconfig_stop_pending = 1;
	ret = radar_capture_host_stop();
	if (ret != 0) {
		g_reconfig_stop_pending = 0;
		return ret;
	}

	/*
	 * RadarStudio sends SetMaxFrameCount before gropstart.  The former may
	 * have already stopped the capture loop, so no running loop remains to
	 * consume g_reconfig_stop_pending.  Complete the same reset sequence
	 * used by masbranch before accepting the calibration configuration.
	 */
	if (g_reconfig_stop_pending) {
		cheetah_stop_for_reconfig();
		g_reconfig_stop_pending = 0;
	}

	return 0;
}

int radar_capture_host_start(void)
{
	if (!g_camera)
		return -ENODEV;
	if (g_ps_cal_mode && !g_ps_cal_mem_ready)
		return -EINVAL;
	if (cheetah_running_flg != 0)
		return -EBUSY;

	mutex_lock(&curFrame_mutex);
	g_host_config_ready = 1;
	isNewTest = 0;
	g_wait_for_sensor_start = 0;
	mutex_unlock(&curFrame_mutex);

	printf("[HOST_CAPTURE] sensorStart accepted: waiting for main capture loop\n");
	return 0;
}

int exit_local_ps_cal_mode(void)
{
	struct sensor_attr *attr;
	uint32_t chirps = g_ps_cal_saved_chirps;
	uint32_t adc_len = g_ps_cal_saved_adc_len;
	int adc_flag;
	int pixel_width;
	int ret = 0;

	if (!g_ps_cal_local_mode || chirps == 0 || adc_len == 0)
		return -EINVAL;

	radar_capture_host_stop();
	mutex_lock(&curFrame_mutex);
	for (int i = 0; i < MAXPOOLNUM; i++)
		free_memory_pool(i);
	for (int i = 0; i < MAXPOOLNUM; i++) {
		glbCtx[i]->waveParams.numChirps[0] = chirps;
		glbCtx[i]->waveParams.adcLen[0] = adc_len;
	}
	g_ps_cal_mem_ready = 0;
	g_ps_cal_ready_pool_index = -1;
	g_ps_cal_capture_start_error = 0;
	g_ps_cal_mode = 0;
	g_ps_cal_local_mode = 0;
	mutex_unlock(&curFrame_mutex);

	/* Local calibration uses two VIC circular buffers.  Rebuild the normal
	 * camera first so its configured buffer set receives a contiguous block
	 * before the normal DSP pools fragment the released calibration memory. */
	pixel_width = (sizeof(CheetahChirpHeadInfo) +
		       sizeof(uint16_t) * glbCtx[0]->basicParams.numRXPerMMIC * adc_len +
		       CHIRP_END_BYTES) / 2;
	attr = cheetah_get_sensor_attr();
	if (g_camera) {
		camera_unregister_sensor(0, attr);
		g_camera = NULL;
	}
	ret = camera_set_frame_count(0, CONFIG_SOC_X2000_CAMERA_VIC0_FRAME_NUMS);
	if (ret < 0)
		return ret;
	cheetah_sensor_dynamic_reconfig(pixel_width, chirps);
	ret = camera_register_sensor(0, attr);
	if (ret < 0)
		return ret;
	g_camera = camera_detect(0);
	if (!g_camera)
		return -ENODEV;
	mutex_lock(&curFrame_mutex);
#if ((USE_USB_OUTPUT == 1)&&(UART_ADC_SEND == 0)&&(NET_ADC_SEND == 0))
	adc_flag = g_outRawDataFlg;
#else
	adc_flag = 0;
#endif
	for (int i = 0; i < MAXPOOLNUM; i++) {
		if (init_memory_pool(glbCtx[i], i, adc_flag, 0) != 0) {
			ret = -ENOMEM;
			break;
		}
	}
	if (ret != 0) {
		for (int i = 0; i < MAXPOOLNUM; i++)
			free_memory_pool(i);
	}
	g_total_adc_size = get_adc_total_size(glbCtx[0]);
	mutex_unlock(&curFrame_mutex);
	if (ret != 0)
		return ret;

#if PHASE_CALIB_AUTO_RESTART_RADAR
	radar_config_start();
	return radar_capture_host_start();
#else
	printf("[PhaseCalib] Auto restart disabled; radar remains stopped.\n");
	return 0;
#endif
}

void restart_cheetah(void)
{
	cheetah_reset();
	mdelay(500);
	set_regs_to_target(cheetah_default_config, sizeof(cheetah_default_config) / sizeof(struct reg_line));
}

float cheetah_get_temperature(int devidx)
{
    uint32_t value;
    float tempr;
    
    // 使用SPI读取温度寄存器
    cheetah_reg_read_single(devidx, 0x10A2, &value, CHEETAH_CHECK);

    tempr = (float)(value>>20);
    return (tempr/4096*1.8-0.6454)*200+40;
}
