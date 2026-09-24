#include "mcp2515.h"
#include <os.h>
#include <os/thread.h>
#include <os/thread_cond.h>
#include <soc/gpio.h>
#include <driver/gpio.h>
#include <driver/irq.h>
#include <stdio.h>
#include <math.h>
#include "mmw_msg_pkt.h"
#include "memory_pool.h"
#include "general_functions.h"
#include "../config_manager.h"
#include "../eol_cal/eol_calibration.h"
#include "board.h"
#include "radar_types.h"

#define MCP2515_INT_GPIO GPIO_PB(27)

//协议选择
#define OUTPUT_PROTOCOL_CAN_ID_DEFAULT                     1       //默认协议
#define OUTPUT_PROTOCOL_CAN_ID_EXCELLE                     0       //凯越协议  
#define OUTPUT_PROTOCOL_CAN_ID_ZONSEN                      0       //宗申协议

#if NEW_PROTOCOL_ENABLE
/****************************New Protocol structure********************************* */
typedef struct {
    uint64_t Reserved                            :8;
    uint64_t m_egoVelocity_mps                   :16;
    uint64_t m_numTrks                           :8;
    uint64_t m_FrameNum                          :32;
} Obj_0_Status_t;

typedef struct {
    uint64_t m_ID                                :8;
    uint64_t m_x_output                          :12;
    uint64_t m_y_output                          :12;
    uint64_t m_Vx_output                         :12;
    uint64_t m_Vy_output                         :12;
    uint64_t m_Montion_state                     :3;
    uint64_t m_isValid                           :1;
    uint64_t m_Reserved                          :4;
} Obj_General_Info_t;

typedef struct {
    uint64_t m_ID                                :8;
    uint64_t m_maxWild_tcs                       :5;
    uint64_t m_Reserved_0                        :3;
    uint64_t m_maxLen_tcs                        :8;
    uint64_t m_heading_output                    :10;
    uint64_t m_Reserved_1                        :30;
} Obj_Extended_Info_t;

typedef struct {
    uint64_t m_BSD_LCMA_SW_Rsp                   :1;
    uint64_t m_SYSTEM_STATUS_BSD                 :3;
    uint64_t m_BSD_STATUS_L                      :2;
    uint64_t m_BSD_STATUS_R                      :2;
    uint64_t m_LCW_STATUS_L                      :2;
    uint64_t m_LCW_STATUS_R                      :2;
    uint64_t m_DOW_STATUS_L                      :2;
    uint64_t m_DOW_STATUS_R                      :2;
    uint64_t m_RCW_STATUS                        :2;
    uint64_t FCW_swt_state                       :1;
    uint64_t SYSTEM_STATUS_FCW                   :3;
    uint64_t SWA_Func_LED_L                      :1;
    uint64_t SWA_Func_LED_R                      :1;
    uint64_t SWA_Left_dis                        :8;
    uint64_t SWA_Right_dis                       :8;
    uint64_t SWA_voice                           :1;
    uint64_t FCW_STATUS                          :3;
    uint64_t SWA_ERROR_state_F                   :2;
    uint64_t SWA_ERROR_state_R                   :2;
    uint64_t m_Reserved                          :16;
} Radar_Status_Msg_t;

Obj_0_Status_t Obj_0_Status = {0};
Obj_General_Info_t Obj_General_Info[MAX_TRACKS] = {0};
Obj_Extended_Info_t Obj_Extended_Info[MAX_TRACKS] = {0};
Radar_Status_Msg_t Radar_Status_Msg = {0};

#endif
// ----- EOL Calibration Global Variables and Constants -----


#define CAN_ID_RADAR_TARGET       0x500
#define CAN_ID_RADAR_STATUS       0x401

static struct mcp2515_dev *g_mcp2515_dev = NULL;
static Mmw_pkt_info *g_pPktInfo = NULL;
static const char *g_pktBuf = NULL;
static int32_t g_pktSize = 0;
static uint32_t gFrameID;

static struct mutex frame_mutex;
static thread_cond_t can_thread_cond;
static thread_ptr_t can_thread = NULL;
static volatile int can_thread_running = 0;
static volatile int data_ready = 0;

extern volatile int can_finished;
extern struct mutex can_mutex;
extern volatile int g_is_eol_mode;

static void can_frame_pkt_send(void);
static void can_radar_target_send(void);
static void can_radar_status_send(void);
static int excelle_frame_pkt_send(void);
static int zosen_frame_pkt_send(void);
static void excelle_map_warn_to_status(const WarnResult *warnResult, RadarStatusMsg *statusMsg);

/* 全局标志，通知线程有中断发生 */
static volatile uint8_t can_irq_pending = 0;
static int g_can_irq = -1;

#if NEW_PROTOCOL_ENABLE
int CanNewProtocolPkt(Mmw_pkt_info *pktInfo, uint32_t frameID)
{
    RadarStatusMsg statusMsg;

    if (!g_mcp2515_dev)
    {
        printf("MCP2515 device not initialized\n");
        return -1;
    }

    mutex_lock(&frame_mutex);

    g_pPktInfo = pktInfo;
    gFrameID = frameID;
    g_pktBuf = NULL;
    g_pktSize = 0;

    Obj_0_Status.m_egoVelocity_mps = g_pPktInfo->egoVlcInfo.egoVelocity_mps;
    Obj_0_Status.m_numTrks = g_pPktInfo->trkInfo.header.numTrks;
    Obj_0_Status.m_FrameNum = gFrameID;

    for (int i = 0; i < Obj_0_Status.m_numTrks; i++) {
        Obj_General_Info[i].m_ID = g_pPktInfo->trkInfo.trkObj[i].trkID;
        Obj_General_Info[i].m_x_output = g_pPktInfo->trkInfo.trkObj[i].x_output;
        Obj_General_Info[i].m_y_output = g_pPktInfo->trkInfo.trkObj[i].y_output;
        Obj_General_Info[i].m_Vx_output = g_pPktInfo->trkInfo.trkObj[i].vx_output;
        Obj_General_Info[i].m_Vy_output = g_pPktInfo->trkInfo.trkObj[i].vy_output;
        Obj_General_Info[i].m_Montion_state = g_pPktInfo->trkInfo.trkObj[i].motion_state;
        Obj_General_Info[i].m_isValid = g_pPktInfo->trkInfo.trkObj[i].isvalid;

        Obj_Extended_Info[i].m_ID = g_pPktInfo->trkInfo.trkObj[i].trkID;
        Obj_Extended_Info[i].m_maxWild_tcs = g_pPktInfo->trkInfo.trkObj[i].maxWid_tcs;
        Obj_Extended_Info[i].m_maxLen_tcs = g_pPktInfo->trkInfo.trkObj[i].maxLen_tcs;
        Obj_Extended_Info[i].m_heading_output = g_pPktInfo->trkInfo.trkObj[i].heading_output;
    }

    excelle_map_warn_to_status(&g_pPktInfo->warnInfo.warnResult, &statusMsg);

    Radar_Status_Msg.m_BSD_LCMA_SW_Rsp = statusMsg.BSD_LCMA_SW_Rsp;
    Radar_Status_Msg.m_SYSTEM_STATUS_BSD = statusMsg.SYSTEM_STATUS_BSD;
    Radar_Status_Msg.m_BSD_STATUS_L = statusMsg.BSD_STATUS_L;
    Radar_Status_Msg.m_BSD_STATUS_R = statusMsg.BSD_STATUS_R;
    Radar_Status_Msg.m_LCW_STATUS_L = statusMsg.LCW_STATUS_L;
    Radar_Status_Msg.m_LCW_STATUS_R = statusMsg.LCW_STATUS_R;
    Radar_Status_Msg.m_DOW_STATUS_L = statusMsg.DOW_STATUS_L;
    Radar_Status_Msg.m_DOW_STATUS_R = statusMsg.DOW_STATUS_R;
    Radar_Status_Msg.m_RCW_STATUS = statusMsg.RCW_STATUS;
    Radar_Status_Msg.FCW_swt_state = statusMsg.FCW_swt_state;
    Radar_Status_Msg.SYSTEM_STATUS_FCW = statusMsg.SYSTEM_STATUS_FCW;
    Radar_Status_Msg.SWA_Func_LED_L = statusMsg.SWA_Func_LED_L;
    Radar_Status_Msg.SWA_Func_LED_R = statusMsg.SWA_Func_LED_R;
    Radar_Status_Msg.SWA_Left_dis = statusMsg.SWA_Left_dis;
    Radar_Status_Msg.SWA_Right_dis = statusMsg.SWA_Right_dis;
    Radar_Status_Msg.SWA_voice = statusMsg.SWA_voice;
    Radar_Status_Msg.FCW_STATUS = statusMsg.FCW_STATUS;
    Radar_Status_Msg.SWA_ERROR_state_F = statusMsg.SWA_ERROR_state_F;
    Radar_Status_Msg.SWA_ERROR_state_R = statusMsg.SWA_ERROR_state_R;

    data_ready = 1;

    mutex_unlock(&frame_mutex);

    thread_cond_signal(&can_thread_cond);

    return 0;
}
#endif

/**
 * @brief 处理TX完成中断：读取INTF、清除标志位
 *        仅在can_send_data_nb的等待循环中调用（发送期间才会触发TX中断）
 */
static void process_tx_interrupts(void)
{
    if (!can_irq_pending)
        return;

    can_irq_pending = 0;
    uint8_t intf = getInterrupts(g_mcp2515_dev);
    /* 处理 TX 完成中断（唤醒对应的 waiter） */
    if (intf & CANINTF_TX0IF) {
        mcp2515_clear_tx_int(g_mcp2515_dev, 0);
    }
    if (intf & CANINTF_TX1IF) {
        mcp2515_clear_tx_int(g_mcp2515_dev, 1);
    }
    if (intf & CANINTF_TX2IF) {
        mcp2515_clear_tx_int(g_mcp2515_dev, 2);
    }
}

/**
 * @brief 自旋等待TX缓冲区完成
 *        轮询CTRL寄存器的同时处理TX中断，不依赖外部唤醒
 */
static int can_send_wait_tx_done(int txbn, int timeout_ms)
{
    uint64_t start = get_time_ns();
    uint64_t timeout_ns = (uint64_t)timeout_ms * 1000000;

    if (!g_mcp2515_dev || txbn >= N_TXBUFFERS)
    {
        return MCP2515_FAILTX;
    }

    while (1) {
        uint8_t ctrl = mcp2515_get_tx_status(g_mcp2515_dev, txbn);

        if ((ctrl & (TXB_ABTF | TXB_MLOA | TXB_TXERR)) != 0) {
            /* 发生错误：中止发送，释放TX缓冲区，避免死锁 */
            mcp2515_abort_tx(g_mcp2515_dev, txbn);
            return MCP2515_FAILTX;
        }

        if ((ctrl & TXB_TXREQ) == 0) {
            return MCP2515_OK;
        }

        /* 处理TX中断，清除MCP2515的INTF标志位 */
        process_tx_interrupts();

        if (get_time_ns() - start > timeout_ns) {
            return MCP2515_TX_TIMEOUT;
        }

        // 极短延时后重试（避免忙轮询）
        for (volatile int d = 0; d < 200; d ++) __asm__("nop");
    }
}

void mcp2515_can_irq_handler(int irq, void *data)
{
    can_irq_pending = 1;
}

static void can_send_thread_func(void *data)
{
    // uint64_t start_time, end_time;
	
    while (can_thread_running)
    {
        mutex_lock(&frame_mutex);

        while (!data_ready && can_thread_running)
        {
            thread_cond_wait(&can_thread_cond, &frame_mutex);
        }

        data_ready = 0;

        if (!can_thread_running)
        {
            mutex_unlock(&frame_mutex);
            break;
        }

        // start_time = get_time_ns();
        // printf("[*** frame %d ***] 开始发送数据(CAN)\n", gFrameID);
        if (OUTPUT_PROTOCOL_CAN_ID_DEFAULT)
        {
            can_frame_pkt_send();
        }

        if (OUTPUT_PROTOCOL_CAN_ID_EXCELLE)
        {
            excelle_frame_pkt_send();
        }

        if (OUTPUT_PROTOCOL_CAN_ID_ZONSEN)
        {
            zosen_frame_pkt_send();
        }


        // can_radar_target_send();
        // can_radar_status_send();

        // end_time = get_time_ns();
        // double send_time_ms = (end_time - start_time) / 1000000.0;
		// printf("[*** frame %d ***] can send thread 总耗时: %.2f 毫秒\n", gFrameID, send_time_ms);

        mutex_unlock(&frame_mutex);

        mutex_lock(&can_mutex);
        can_finished = 1;
        mutex_unlock(&can_mutex);
    }
}

static void eol_can_msg_handler(struct can_message *rx_msg)
{
    uint8_t resp_data[8] = {0};
    uint8_t resp_len = 0;

    // Delegate to the unified UDS processing engine
    eol_uds_process_request(rx_msg->data, rx_msg->dlc, resp_data, &resp_len);

    // Send the constructed UDS response via CAN
    if (resp_len > 0) {
        struct can_message tx_msg;
        memset(&tx_msg, 0, sizeof(tx_msg));
        tx_msg.id = 0x788;
        tx_msg.extended = 0;
        tx_msg.dlc = resp_len;
        memcpy(tx_msg.data, resp_data, resp_len);

        printf("[EOL] CAN TX -> ID: 0x%03X, DLC: %d, Data: ", tx_msg.id, tx_msg.dlc);
        for (int i = 0; i < tx_msg.dlc; i++) {
            printf("%02X ", tx_msg.data[i]);
        }
        printf("\n");

        mutex_lock(&can_mutex);
        mcp2515_send(g_mcp2515_dev, &tx_msg);
        mutex_unlock(&can_mutex);
    }
}
// --------------------------------------------------------

static void can_task(void *data)
{
    struct can_message rx_msg;
    int ret;
    
    /* 初始化MCP2515 */
    struct mcp2515_bit_timing bit_timing;
    
    /* 根据协议类型配置CAN总线时序 */
#if OUTPUT_PROTOCOL_CAN_ID_ZONSEN
    /* 宗申协议：500Kbps @ 16MHz晶振 */
    bit_timing.cnf1 = 0x00;  /* BRP=0, 1个TQ=0.125us */
    bit_timing.cnf2 = 0x9E; /* BTLMODE=1, SAM=0, PHSEG1=3(4TQ), PRSEG=6(7TQ) */
    bit_timing.cnf3 = 0x03; /* PHSEG2=3(4TQ) */
#elif OUTPUT_PROTOCOL_CAN_ID_EXCELLE
    /* 凯越协议：500Kbps @ 16MHz晶振 */
    bit_timing.cnf1 = 0x00;
    bit_timing.cnf2 = 0x9E;
    bit_timing.cnf3 = 0x03;
#elif OUTPUT_PROTOCOL_CAN_ID_DEFAULT
    /* 默认协议：1Mbps @ 16MHz晶振 */
    bit_timing.cnf1 = 0x00;
    bit_timing.cnf2 = 0x82;
    bit_timing.cnf3 = 0x02;
#else
    /* 默认：500Kbps @ 16MHz晶振 */
    bit_timing.cnf1 = 0x00;
    bit_timing.cnf2 = 0x9E;
    bit_timing.cnf3 = 0x03;
#endif
    
    g_mcp2515_dev = mcp2515_init(0, GPIO_PB(28), &bit_timing);
    if (!g_mcp2515_dev) {
        printf("MCP2515 init failed\n");
        return;
    }
    
    printf("MCP2515 init success\n");
    g_mcp2515_dev->tx_busy_mask = 0;
    gpio_request(MCP2515_INT_GPIO, "mcp2515_int");
    gpio_direction_input(MCP2515_INT_GPIO);
    mcp2515_clear_tx_int(g_mcp2515_dev, 0);
    mcp2515_clear_tx_int(g_mcp2515_dev, 1);
    mcp2515_clear_tx_int(g_mcp2515_dev, 2);
    g_can_irq = gpio_to_irq(MCP2515_INT_GPIO);
    if (g_can_irq < 0) {
        printf("GPIO to IRQ mapping failed\n");
        gpio_release(MCP2515_INT_GPIO);
    } else {
        request_irq(g_can_irq, IRQ_TYPE_EDGE_FALLING, mcp2515_can_irq_handler, "mcp2515_int", g_mcp2515_dev);
        // printf("MCP2515 INT IRQ configured: gpio=%d, irq=%d\n", MCP2515_INT_GPIO, g_can_irq);
    }

    /* 初始化互斥锁和条件变量 */
    mutex_init(&frame_mutex);
    thread_cond_init(&can_thread_cond);
    
    /* 创建CAN发送线程 */
    can_thread = thread_create("can_send_thread", 65535, can_send_thread_func, NULL);
    if (can_thread == NULL)
    {
        printf("Failed to create can send thread\n");
        return;
    }
    
    can_thread_running = 1;
    
    /* 循环接收CAN消息 */
    while (1) {
        /* 尝试接收CAN消息 */
        ret = mcp2515_recv(g_mcp2515_dev, &rx_msg);
        if (ret == 0) {
            if (rx_msg.id == 0x780) {
                eol_can_msg_handler(&rx_msg);
            } else if (rx_msg.id == 0x781) {
                // CAN ID 0x781: 预警测试指令
                // 第一字节为预警协议字节
                if (rx_msg.dlc >= 1) {
                    uint8_t warn_byte = rx_msg.data[0];
                    printf("[CAN WARN TEST] ID=0x781, DLC=%d, warn_byte=0x%02X\n",
                           rx_msg.dlc, warn_byte);
                    printf("    Data: ");
                    for (int i = 0; i < rx_msg.dlc; i++) {
                        printf("0x%02X ", rx_msg.data[i]);
                    }
                    printf("\n");
                    warn_test_handler(warn_byte);
                }
            } else {
                printf("CAN message received: ID=0x%X, DLC=%d, Data=", rx_msg.id, rx_msg.dlc);
                for (int i = 0; i < rx_msg.dlc; i++) {
                    printf("0x%02X ", rx_msg.data[i]);
                }
                printf("\n");
            }
        } else if (ret != -2) {
            printf("CAN message receive failed: %d\n", ret);
            msleep(10);
        } else {
            // 没有收到数据时，使用极短延时让出CPU，避免阻塞其他任务
            msleep(10);
        }
    }
}

static void can_send_data(uint32_t id,uint8_t *data, uint32_t len)
{
    struct can_message tx_msg;
    int ret;
    uint32_t sent = 0;
    uint8_t seq = 0;

    while (sent < len) {
        tx_msg.id = id + seq;
        tx_msg.extended = 0;
        tx_msg.dlc = (len - sent > 8) ? 8 : (len - sent);
        memcpy(tx_msg.data, data + sent, tx_msg.dlc);

        mutex_lock(&can_mutex);
        ret = mcp2515_send(g_mcp2515_dev, &tx_msg);
        mutex_unlock(&can_mutex);

        if (ret != 0) {
            printf("CAN send failed at seq %d: %d\n", seq, ret);
            return;
        }

        sent += tx_msg.dlc;
        seq++;
        
        usleep(50);
    }
}

static void can_send_data_nb(uint32_t id, uint8_t *data, uint32_t len)
{
    struct can_message tx_msg;
    uint32_t sent = 0;
    uint8_t seq = 0;
    int retry_count = 6;
    
    while (sent < len) {
        // tx_msg.id = id + seq;
        tx_msg.id = id;
        tx_msg.extended = 0;
        tx_msg.dlc = (len - sent > 8) ? 8 : (len - sent);
        memcpy(tx_msg.data, data + sent, tx_msg.dlc);
        
        // mutex_lock(&can_mutex);

        // 尝试获取空闲的TX缓冲区
        int txbn = -1;
        for (int tries = 0; tries < retry_count; tries++) {
            for (int i = 0; i < N_TXBUFFERS; i++) {
                uint8_t ctrl = mcp2515_get_tx_status(g_mcp2515_dev, i);
                if ((ctrl & TXB_TXREQ) == 0) {
                    txbn = i;
                    break;
                }
            }

            if (txbn >= 0) break;
            
            // 极短延时后重试（避免忙轮询）
            for (volatile int d = 0; d < 200; d ++) __asm__("nop");
        }

        if (txbn < 0) {
            // 所有缓冲区仍忙，等待任意一个完成（自旋轮询 + 处理TX中断）
            for (int i = 0; i < N_TXBUFFERS; i++) {
                if (can_send_wait_tx_done(i, 1) == MCP2515_FAILTX) {
                    return;
                }
            }
            continue; // 重新尝试
        }
        
        // 非阻塞填充并触发发送
        int ret = mcp2515_send_nb(g_mcp2515_dev, txbn, &tx_msg);
        if (ret == MCP2515_OK) {
            sent += tx_msg.dlc;
            seq++;
        } else if (ret == MCP2515_FAILTX) {
            // 错误发生：读取错误标志并恢复
            uint8_t eflg = mcp2515_get_error_flags(g_mcp2515_dev);
            printf("TX%d error: EFLG=0x%02X\n", txbn, eflg);
            // 清除错误（必要时）
            mcp2515_clear_error_flags(g_mcp2515_dev);
            // 丢弃当前帧
        }

        // mutex_unlock(&can_mutex);
    }
    
    // 发送完成后，等待所有缓冲区真正释放（避免下一帧叠加）
    for (int i = 0; i < N_TXBUFFERS; i++) {
        int ret = can_send_wait_tx_done(i, 3);

        if (ret != MCP2515_OK) {
			printf("CAN wait finish at txbn %d: %d\n", i, ret);
		}
    }
}

/**
 * @brief CAN数据发送函数（固定ID，不自增）
 * 
 * 与 can_send_data_nb 不同，此函数保持原始ID不变，数据长度限制为8字节。
 * 适用于发送固定ID的CAN报文，如宗申协议的状态报文和目标信息报文。
 * 
 * @param id CAN报文ID（保持不变）
 * @param data 发送数据指针
 * @param len 数据长度（最大8字节，超出部分会被截断）
 */
static void can_send_data_nb_fixed_id(uint32_t id, uint8_t *data, uint32_t len)
{
    struct can_message tx_msg;
    int retry_count = 6;
    
    // 限制数据长度为8字节
    uint32_t send_len = (len > 8) ? 8 : len;
    
    // 填充CAN消息
    tx_msg.id = id;                    // 固定ID，不自增
    tx_msg.extended = 0;
    tx_msg.dlc = send_len;
    memcpy(tx_msg.data, data, send_len);
    
    // 尝试获取空闲的TX缓冲区
    int txbn = -1;
    for (int tries = 0; tries < retry_count; tries++) {
        for (int i = 0; i < N_TXBUFFERS; i++) {
            uint8_t ctrl = mcp2515_get_tx_status(g_mcp2515_dev, i);
            if ((ctrl & TXB_TXREQ) == 0) {
                txbn = i;
                break;
            }
        }

        if (txbn >= 0) break;
        
        // 极短延时后重试（避免忙轮询）
        for (volatile int d = 0; d < 200; d ++) __asm__("nop");
    }

    if (txbn < 0) {
        // 所有缓冲区仍忙，等待任意一个完成（自旋轮询 + 处理TX中断）
        for (int i = 0; i < N_TXBUFFERS; i++) {
            if (can_send_wait_tx_done(i, 1) == MCP2515_FAILTX) {
                printf("CAN send fail: no available TX buffer for id 0x%03X\n", id);
                return;
            }
        }
        // 获取一个刚释放的缓冲区
        for (int i = 0; i < N_TXBUFFERS; i++) {
            uint8_t ctrl = mcp2515_get_tx_status(g_mcp2515_dev, i);
            if ((ctrl & TXB_TXREQ) == 0) {
                txbn = i;
                break;
            }
        }
    }
    
    if (txbn >= 0) {
        // 非阻塞填充并触发发送
        int ret = mcp2515_send_nb(g_mcp2515_dev, txbn, &tx_msg);
        if (ret != MCP2515_OK) {
            // 错误发生：读取错误标志并恢复
            uint8_t eflg = mcp2515_get_error_flags(g_mcp2515_dev);
            printf("TX%d error for id 0x%03X: EFLG=0x%02X\n", txbn, id, eflg);
            mcp2515_clear_error_flags(g_mcp2515_dev);
        }
        
        // 等待当前缓冲区发送完成
        can_send_wait_tx_done(txbn, 3);
    }
}

/* Send complete packet via CAN */
static void can_frame_pkt_send(void)
{
#if NEW_PROTOCOL_ENABLE
    if (!g_mcp2515_dev) {
        printf("MCP2515 not initialized\n");
        return;
    }

    // can_send_data(0x500, (uint8_t *)&Obj_0_Status, sizeof(Obj_0_Status));
    // for (int i = 0; i < Obj_0_Status.m_numTrks; i++) {
    //     can_send_data(0x501, (uint8_t *)&Obj_General_Info[i], sizeof(Obj_General_Info[i]));
    //     can_send_data(0x502, (uint8_t *)&Obj_Extended_Info[i], sizeof(Obj_Extended_Info[i]));
    // }
    // can_send_data(0x400, (uint8_t *)&Radar_Status_Msg, sizeof(Radar_Status_Msg));

    // 减少丢帧和脏数据，并保持ID不变
    can_send_data_nb(0x500, (uint8_t *)&Obj_0_Status, sizeof(Obj_0_Status));
    for (int i = 0; i < Obj_0_Status.m_numTrks; i++) {
        can_send_data_nb(0x501, (uint8_t *)&Obj_General_Info[i], sizeof(Obj_General_Info[i]));
        can_send_data_nb(0x502, (uint8_t *)&Obj_Extended_Info[i], sizeof(Obj_Extended_Info[i]));
    }
    can_send_data_nb(0x400, (uint8_t *)&Radar_Status_Msg, sizeof(Radar_Status_Msg));

    printf("CAN packet sent: FrameID=%d\n", gFrameID);
#else

    if (!g_mcp2515_dev || !g_pktBuf || g_pktSize <= 0)
    {
        printf("MCP2515 not initialized or invalid packet buffer\n");
        return;
    }

    can_send_data_nb(0x100, (uint8_t*)g_pktBuf, g_pktSize);

    printf("CAN packet sent: FrameID=%d, TotalSize=%u\n", gFrameID, g_pktSize);
#endif
        return;
}

int can_frame_pkt_update(Mmw_pkt_info *pktInfo, const char *pktBuf, int32_t pktSize, uint32_t frameID)
{
    if (!pktBuf || pktSize <= 0) {
        printf("ERROR: Invalid packet buffer or size\n");
        return -1;
    }

    if (!g_mcp2515_dev)
    {
        printf("MCP2515 device not initialized\n");
        return -1;
    }

    mutex_lock(&frame_mutex);

    g_pPktInfo = pktInfo;
    gFrameID = frameID;
    g_pktBuf = pktBuf;
    g_pktSize = pktSize;

    data_ready = 1;

    mutex_unlock(&frame_mutex);

    thread_cond_signal(&can_thread_cond);

    return 0;
}

void mcp2515_task_init(void)
{
    thread_create("can_task", 65535, can_task, NULL);
}




/************************** 凯越协议报文发送 ************************** */ 

/* 凯越协议 CAN ID */
#define EXCELLE_CAN_ID_RADAR_STATUS  0x401
#define EXCELLE_CAN_ID_RADAR_TARGET  0x500
#define EXCELLE_STATUS_SEND_INTERVAL_MS    100

/* 上次发送状态报文的时间戳（毫秒） */
static uint64_t g_excelle_last_status_send_time_ms = 0;

/* BSD/AOA启动车速阈值(单位：km/h) - 可配置 */
static float g_excelle_ego_speed_threshold_kph = 10.0f;

/* 预警优先级定义（数值越大优先级越高） */
/* RCW > AOA/DOW > BSD > LCA */
typedef enum {
    WARN_PRIORITY_NONE = 0,
    WARN_PRIORITY_LCA,
    WARN_PRIORITY_BSD,
    WARN_PRIORITY_AOA,
    WARN_PRIORITY_RCW
} WarnPriority;

/**
 * @brief 获取当前时间（毫秒）
 */
static uint64_t excelle_get_current_time_ms(void)
{
    return get_time_ns() / 1000000;
}

/**
 * @brief 根据trkID查找轨迹在数组中的索引
 * @param trkID 轨迹ID
 * @return 数组索引，未找到返回-1
 */
static int excelle_find_track_index(uint8_t trkID)
{
    if (!g_pPktInfo) {
        return -1;
    }
    uint8_t num_trks = g_pPktInfo->trkInfo.header.numTrks;
    for (uint8_t i = 0; i < num_trks; i++) {
        if (g_pPktInfo->trkInfo.trkObj[i].trkID == trkID) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 获取航迹距离
 * @param trkID 轨迹ID（按照ID匹配查找，不是数组索引）
 */
static float excelle_get_track_distance(uint8_t trkID)
{
    int idx = excelle_find_track_index(trkID);
    if (idx < 0) {
        return 0.0f;
    }
    return (g_pPktInfo->trkInfo.trkObj[idx].x_output / 8.0f - 255.0f);
}

/**
 * @brief 获取航迹径向速度
 * @param trkID 轨迹ID（按照ID匹配查找，不是数组索引）
 */
static float excelle_get_track_radial_velocity(uint8_t trkID)
{
    int idx = excelle_find_track_index(trkID);
    if (idx < 0) {
        return 0.0f;
    }
    MotorCycle_TrkObj *trk = &g_pPktInfo->trkInfo.trkObj[idx];
    return (trk->vx_output / 20.0f - 102.0f);
}

/**
 * @brief 计算TTC (Time To Collision)
 * 支持负坐标：distance与radial_velocity符号相反时表示靠近
 */
static float excelle_calculate_ttc(float distance, float radial_velocity)
{
    if (distance * radial_velocity >= 0) {
        return 999.0f;
    }
    float ttc = -distance / radial_velocity;
    if (ttc < 0) ttc = 999.0f;
    return ttc;
}

/**
 * @brief 根据距离获取报警级别
 * 
 * 报警级别规则（与 board.c 灯控逻辑一致）：
 *   - 距离 > 10 米且 <= 70 米：标准型报警（LED常亮）= 0x01
 *   - 距离 > 0 米且 <= 10 米：增强型报警（LED闪烁）= 0x02
 *   - 其他：无报警 = 0x00
 */
static uint8_t excelle_get_alarm_level_by_distance(float distance)
{
    float abs_dist = (distance < 0) ? -distance : distance;
    
    if (abs_dist > 10.0f && abs_dist <= 70.0f) {
        return ALARM_STANDARD;  /* 标准型报警 - LED常亮 */
    } else if (abs_dist > 0.0f && abs_dist <= 10.0f) {
        return ALARM_ENHANCED;  /* 增强型报警 - LED闪烁 */
    }
    
    return ALARM_NONE;  /* 无报警 */
}

/**
 * @brief 根据TTC获取报警级别
 * 
 * TTC阈值规则（与 board.c 灯控逻辑一致）：
 *   - TTC <= 2.0s：增强型报警（LED闪烁）
 *   - TTC <= 3.5s：标准型报警（LED常亮）
 *   - 其他：无报警
 */
static uint8_t excelle_get_alarm_level_by_ttc(float ttc)
{
    if (ttc <= 2.0f) {
        return ALARM_ENHANCED;  /* 增强型报警 - LED闪烁 */
    } else if (ttc <= 3.5f) {
        return ALARM_STANDARD;  /* 标准型报警 - LED常亮 */
    }
    return ALARM_NONE;  /* 无报警 */
}

/**
 * @brief 获取指定预警类型中TTC最小的目标ID和距离
 * @param trkIDs 目标ID数组
 * @param trkCnt 目标数量
 * @param out_distance 输出：TTC最小目标对应的距离
 * @return TTC最小目标的目标ID，无有效目标返回255
 */
static uint8_t excelle_get_min_ttc_trkID(const uint8_t *trkIDs, uint8_t trkCnt, float *out_distance)
{
    float min_ttc = 999.0f;
    uint8_t best_trkID = 255;
    *out_distance = 0.0f;
    
    for (int i = 0; i < trkCnt; i++) {
        float distance = excelle_get_track_distance(trkIDs[i]);
        if (distance != 0.0f) {
            float radial_v = excelle_get_track_radial_velocity(trkIDs[i]);
            float ttc = excelle_calculate_ttc(distance, radial_v);
            if (ttc < min_ttc) {
                min_ttc = ttc;
                best_trkID = trkIDs[i];
                *out_distance = distance;
            }
        }
    }
    return best_trkID;
}

/**
 * @brief 查找最近的轨迹ID
 * @return 最近轨迹的ID，无轨迹返回255
 */
static uint8_t excelle_find_closest_track(void)
{
    if (!g_pPktInfo) {
        return 255;
    }
    
    uint8_t closest_trkID = 255;
    float min_distance = 9999.0f;
    uint8_t num_trks = g_pPktInfo->trkInfo.header.numTrks;
    
    for (uint8_t i = 0; i < num_trks; i++) {
        float x = (g_pPktInfo->trkInfo.trkObj[i].x_output / 8.0f - 255.0f);
        float abs_x = (x < 0) ? -x : x;
        
        if (abs_x < min_distance && abs_x > 0) {
            min_distance = abs_x;
            closest_trkID = g_pPktInfo->trkInfo.trkObj[i].trkID;
        }
    }
    
    return closest_trkID;
}

/**
 * @brief 根据报警级别查找对应的轨迹ID
 * @param level 报警级别（1=常亮, 2=闪烁）
 * @return 轨迹ID，未找到返回255
 */
static uint8_t excelle_find_track_by_warning_level(uint8_t level)
{
    if (!g_pPktInfo) {
        return 255;
    }
    
    uint8_t num_trks = g_pPktInfo->trkInfo.header.numTrks;
    float min_distance = 9999.0f;
    uint8_t best_trkID = 255;
    
    for (uint8_t i = 0; i < num_trks; i++) {
        float x = (g_pPktInfo->trkInfo.trkObj[i].x_output / 8.0f - 255.0f);
        float abs_x = (x < 0) ? -x : x;
        
        // 根据报警级别筛选距离范围
        bool in_range = false;
        if (level == 1) {
            // 常亮级别：10-70m
            in_range = (abs_x > 10.0f && abs_x <= 70.0f);
        } else if (level == 2) {
            // 闪烁级别：0-10m
            in_range = (abs_x > 0 && abs_x <= 10.0f);
        } else {
            // 未知级别，接受所有距离
            in_range = (abs_x > 0);
        }
        
        if (in_range && abs_x < min_distance) {
            min_distance = abs_x;
            best_trkID = g_pPktInfo->trkInfo.trkObj[i].trkID;
        }
    }
    
    // 如果指定级别没找到，放宽条件找最近的轨迹
    if (best_trkID == 255) {
        min_distance = 9999.0f;
        for (uint8_t i = 0; i < num_trks; i++) {
            float x = (g_pPktInfo->trkInfo.trkObj[i].x_output / 8.0f - 255.0f);
            float abs_x = (x < 0) ? -x : x;
            
            if (abs_x < min_distance && abs_x > 0) {
                min_distance = abs_x;
                best_trkID = g_pPktInfo->trkInfo.trkObj[i].trkID;
            }
        }
    }
    
    return best_trkID;
}

/**
 * @brief 将 WarnResult 映射到 RadarStatusMsg（宗申协议方式）
 * 
 * 报文逻辑（与宗申协议一致）：
 *   - 状态字段：所有警告类型独立上报，各状态字段独立设置
 *   - 目标距离(SWA_Left_dis/SWA_Right_dis)：按优先级顺序选择第一个触发的预警类型的目标
 *     AOA(优先级3) → BSD(优先级2) → LCA(优先级1)
 *     RCW只更新状态，不输出目标距离
 * 
 * 优先级顺序（数值越大优先级越高）：
 *   RCW(4) > AOA/DOW(3) > BSD(2) > LCA(1)
 */
static void excelle_map_warn_to_status(const WarnResult *warnResult, RadarStatusMsg *statusMsg)
{
    if (!warnResult || !statusMsg) {
        return;
    }

    memset(statusMsg, 0, sizeof(RadarStatusMsg));

    /* 获取当前车速（单位：km/h） */
    float ego_speed_kph = (g_pPktInfo->egoVlcInfo.egoVelocity_mps / 100.0f) * 3.6f;
    bool is_speed_ok = (ego_speed_kph >= g_excelle_ego_speed_threshold_kph);

    /* ========== 第一步：设置状态报文（所有警告类型独立上报） ========== */
    
    /* RCW状态 - 只更新状态，不参与目标距离选择 */
    if (warnResult->RCW && warnResult->RCW_cnt > 0) {
        float rcw_dist;
        excelle_get_min_ttc_trkID(warnResult->RCW_IDs, warnResult->RCW_cnt, &rcw_dist);
        statusMsg->RCW_STATUS = excelle_get_alarm_level_by_distance(rcw_dist);
    }
    
    /* AOA（DOW预警）状态 - 左侧 */
    if (is_speed_ok && warnResult->AOA_left) {
        float dist;
        uint8_t trkID;
        if (warnResult->AOA_left_cnt > 0) {
            trkID = excelle_get_min_ttc_trkID(warnResult->AOA_left_IDs, warnResult->AOA_left_cnt, &dist);
        } else {
            trkID = excelle_find_closest_track();
            dist = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
        }
        statusMsg->DOW_STATUS_L = excelle_get_alarm_level_by_distance(dist);
    }
    
    /* AOA（DOW预警）状态 - 右侧 */
    if (is_speed_ok && warnResult->AOA_right) {
        float dist;
        uint8_t trkID;
        if (warnResult->AOA_right_cnt > 0) {
            trkID = excelle_get_min_ttc_trkID(warnResult->AOA_right_IDs, warnResult->AOA_right_cnt, &dist);
        } else {
            trkID = excelle_find_closest_track();
            dist = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
        }
        statusMsg->DOW_STATUS_R = excelle_get_alarm_level_by_distance(dist);
    }
    
    /* LCA状态 - 左侧 */
    if (warnResult->LCA_left) {
        float dist = 0.0f;
        uint8_t trkID;
        if (warnResult->LCA_left_cnt > 0) {
            trkID = excelle_get_min_ttc_trkID(warnResult->LCA_left_IDs, warnResult->LCA_left_cnt, &dist);
            // 如果通过ID没找到有效目标，尝试其他方法
            if (trkID == 255 && warnResult->LCA_left_level > 0) {
                trkID = excelle_find_track_by_warning_level(warnResult->LCA_left_level);
                dist = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
            }
            if (trkID == 255) {
                trkID = excelle_find_closest_track();
                dist = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
            }
        } else if (warnResult->LCA_left_level > 0) {
            trkID = excelle_find_track_by_warning_level(warnResult->LCA_left_level);
            dist = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
        } else {
            trkID = excelle_find_closest_track();
            dist = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
        }
        statusMsg->LCW_STATUS_L = excelle_get_alarm_level_by_distance(dist);
    }
    
    /* LCA状态 - 右侧 */
    if (warnResult->LCA_right) {
        float dist = 0.0f;
        uint8_t trkID;
        if (warnResult->LCA_right_cnt > 0) {
            trkID = excelle_get_min_ttc_trkID(warnResult->LCA_right_IDs, warnResult->LCA_right_cnt, &dist);
            // 如果通过ID没找到有效目标，尝试其他方法
            if (trkID == 255 && warnResult->LCA_right_level > 0) {
                trkID = excelle_find_track_by_warning_level(warnResult->LCA_right_level);
                dist = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
            }
            if (trkID == 255) {
                trkID = excelle_find_closest_track();
                dist = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
            }
        } else if (warnResult->LCA_right_level > 0) {
            trkID = excelle_find_track_by_warning_level(warnResult->LCA_right_level);
            dist = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
        } else {
            trkID = excelle_find_closest_track();
            dist = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
        }
        statusMsg->LCW_STATUS_R = excelle_get_alarm_level_by_distance(dist);
    }
    
    /* BSD状态 - 左侧 */
    if (is_speed_ok && warnResult->BSD_left && warnResult->BSD_left_cnt > 0) {
        float dist;
        excelle_get_min_ttc_trkID(warnResult->BSD_left_IDs, warnResult->BSD_left_cnt, &dist);
        statusMsg->BSD_STATUS_L = excelle_get_alarm_level_by_distance(dist);
    }
    
    /* BSD状态 - 右侧 */
    if (is_speed_ok && warnResult->BSD_right && warnResult->BSD_right_cnt > 0) {
        float dist;
        excelle_get_min_ttc_trkID(warnResult->BSD_right_IDs, warnResult->BSD_right_cnt, &dist);
        statusMsg->BSD_STATUS_R = excelle_get_alarm_level_by_distance(dist);
    }
    
    /* ========== 第二步：选择目标距离（按优先级顺序：RCW→AOA→BSD→LCA） ========== */
    
    /* RCW：只更新状态，不输出目标距离，继续检查下一个 */
    
    /* AOA：优先级最高的目标警告类型 */
    /* 左侧AOA */
    float left_distance = 0.0f;
    if (is_speed_ok && warnResult->AOA_left) {
        uint8_t trkID;
        if (warnResult->AOA_left_cnt > 0) {
            trkID = excelle_get_min_ttc_trkID(warnResult->AOA_left_IDs, warnResult->AOA_left_cnt, &left_distance);
        } else {
            trkID = excelle_find_closest_track();
            left_distance = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
        }
    }
    
    /* 右侧AOA */
    float right_distance = 0.0f;
    if (is_speed_ok && warnResult->AOA_right) {
        uint8_t trkID;
        if (warnResult->AOA_right_cnt > 0) {
            trkID = excelle_get_min_ttc_trkID(warnResult->AOA_right_IDs, warnResult->AOA_right_cnt, &right_distance);
        } else {
            trkID = excelle_find_closest_track();
            right_distance = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
        }
    }
    
    /* BSD：优先级次高，只有AOA没选中时才选 */
    /* 左侧BSD */
    if (left_distance == 0.0f && is_speed_ok && warnResult->BSD_left && warnResult->BSD_left_cnt > 0) {
        excelle_get_min_ttc_trkID(warnResult->BSD_left_IDs, warnResult->BSD_left_cnt, &left_distance);
    }
    /* 右侧BSD */
    if (right_distance == 0.0f && is_speed_ok && warnResult->BSD_right && warnResult->BSD_right_cnt > 0) {
        excelle_get_min_ttc_trkID(warnResult->BSD_right_IDs, warnResult->BSD_right_cnt, &right_distance);
    }
    
    /* LCA：优先级最低，只有AOA和BSD都没选中时才选 */
    /* 左侧LCA */
    if (left_distance == 0.0f && warnResult->LCA_left) {
        uint8_t trkID;
        if (warnResult->LCA_left_cnt > 0) {
            trkID = excelle_get_min_ttc_trkID(warnResult->LCA_left_IDs, warnResult->LCA_left_cnt, &left_distance);
            // 如果通过ID没找到有效目标，尝试其他方法
            if (trkID == 255 && warnResult->LCA_left_level > 0) {
                trkID = excelle_find_track_by_warning_level(warnResult->LCA_left_level);
                left_distance = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
            }
            if (trkID == 255) {
                trkID = excelle_find_closest_track();
                left_distance = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
            }
        } else if (warnResult->LCA_left_level > 0) {
            trkID = excelle_find_track_by_warning_level(warnResult->LCA_left_level);
            left_distance = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
        } else {
            trkID = excelle_find_closest_track();
            left_distance = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
        }
    }
    /* 右侧LCA */
    if (right_distance == 0.0f && warnResult->LCA_right) {
        uint8_t trkID;
        if (warnResult->LCA_right_cnt > 0) {
            trkID = excelle_get_min_ttc_trkID(warnResult->LCA_right_IDs, warnResult->LCA_right_cnt, &right_distance);
            // 如果通过ID没找到有效目标，尝试其他方法
            if (trkID == 255 && warnResult->LCA_right_level > 0) {
                trkID = excelle_find_track_by_warning_level(warnResult->LCA_right_level);
                right_distance = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
            }
            if (trkID == 255) {
                trkID = excelle_find_closest_track();
                right_distance = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
            }
        } else if (warnResult->LCA_right_level > 0) {
            trkID = excelle_find_track_by_warning_level(warnResult->LCA_right_level);
            right_distance = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
        } else {
            trkID = excelle_find_closest_track();
            right_distance = (trkID != 255) ? excelle_get_track_distance(trkID) : 0.0f;
        }
    }
    
    /* ========== 第三步：设置系统状态和输出 ========== */
    
    /* 系统状态设置 */
    statusMsg->SYSTEM_STATUS_BSD = 0x03; /* 功能激活 */
    statusMsg->BSD_LCMA_SW_Rsp = 0x01;   /* 功能开启 */

    /* FCW状态（暂不使用） */
    statusMsg->SYSTEM_STATUS_FCW = 0x02; /* 功能待机 */
    statusMsg->FCW_swt_state = 0x00;     /* 功能关闭 */
    statusMsg->FCW_STATUS = 0x00;        /* 无报警 */

    /* LED和蜂鸣器功能开关 - 固定开启 */
    statusMsg->SWA_Func_LED_L = 1;  /* 左后视镜LED功能开关 */
    statusMsg->SWA_Func_LED_R = 1;  /* 右后视镜LED功能开关 */
    statusMsg->SWA_voice = 1;       /* 蜂鸣器功能开关 */

    /* 左右侧预警目标距离（最大70米） */
    float abs_left_dist = (left_distance < 0) ? -left_distance : left_distance;
    float abs_right_dist = (right_distance < 0) ? -right_distance : right_distance;
    statusMsg->SWA_Left_dis = (abs_left_dist > 0 && abs_left_dist <= 70.0f) ? (uint8_t)abs_left_dist : 0;
    statusMsg->SWA_Right_dis = (abs_right_dist > 0 && abs_right_dist <= 70.0f) ? (uint8_t)abs_right_dist : 0;

    /* 故障状态（默认无故障） */
    statusMsg->SWA_ERROR_state_R = 0x00;
    statusMsg->SWA_ERROR_state_F = 0x00;
}

static int excelle_frame_pkt_send(void)
{
    RadarStatusMsg statusMsg;
    uint64_t current_time_ms;
    uint8_t status_data[8];

    if (!g_mcp2515_dev || !g_pPktInfo) {
        printf("MCP2515 not initialized or invalid packet info\n");
        return -1;
    }

    /* 映射预警数据到雷达状态 */
    excelle_map_warn_to_status(&g_pPktInfo->warnInfo.warnResult, &statusMsg);
    
    /* 复制到data数组 */
    memcpy(status_data, &statusMsg, sizeof(RadarStatusMsg));

    /* 发送雷达状态报文（按时间间隔） */
    current_time_ms = excelle_get_current_time_ms();
    if ((current_time_ms - g_excelle_last_status_send_time_ms >= EXCELLE_STATUS_SEND_INTERVAL_MS)) {
        
        /* 打印原始字节数据 */
        printf("[EXCELLE CAN TX] ID:0x%03X, Data: %02X %02X %02X %02X %02X %02X %02X %02X\n",
               EXCELLE_CAN_ID_RADAR_STATUS,
               status_data[0], status_data[1], status_data[2], status_data[3],
               status_data[4], status_data[5], status_data[6], status_data[7]);
        
        /* 打印结构化字段 */
        printf("[EXCELLE STATUS MSG] SW_Resp=%d, BSD_L=%d, BSD_R=%d, SYS=%d, LCW_L=%d, LCW_R=%d, DOW_L=%d, DOW_R=%d, RCW=%d, LED_L=%d, LED_R=%d, Voice=%d, SWA_L_dis=%d, SWA_R_dis=%d\n",
               statusMsg.BSD_LCMA_SW_Rsp,
               statusMsg.BSD_STATUS_L,
               statusMsg.BSD_STATUS_R,
               statusMsg.SYSTEM_STATUS_BSD,
               statusMsg.LCW_STATUS_L,
               statusMsg.LCW_STATUS_R,
               statusMsg.DOW_STATUS_L,
               statusMsg.DOW_STATUS_R,
               statusMsg.RCW_STATUS,
               statusMsg.SWA_Func_LED_L,
               statusMsg.SWA_Func_LED_R,
               statusMsg.SWA_voice,
               statusMsg.SWA_Left_dis,
               statusMsg.SWA_Right_dis);

        /* 发送CAN消息（使用固定ID） */
        mutex_lock(&can_mutex);
        can_send_data_nb_fixed_id(EXCELLE_CAN_ID_RADAR_STATUS, status_data, 8);
        mutex_unlock(&can_mutex);
        g_excelle_last_status_send_time_ms = current_time_ms;
    }

    return 0;
}


/*********************************** 宗申协议 ****************************************/

/* 宗申协议 CAN ID 定义 */
#define ZONSEN_CAN_ID_STATUS        0x50A    // BSD状态报文
#define ZONSEN_CAN_ID_LEFT_TARGET   0x60B    // 左侧报警目标信息
#define ZONSEN_CAN_ID_RIGHT_TARGET  0x61B    // 右侧报警目标信息

/* 宗申协议状态报文结构体（Motorola LSB格式） */
typedef struct {
    /* Byte 0 (位0-7): RESERVED[0], SW_Resp[1], RESERVED[7:2] */
    uint8_t RESERVED0_0 : 1;       // Byte 0, bit 0: 保留
    uint8_t SW_Resp : 1;           // Byte 0, bit 1: 功能开关 (0=OFF, 1=ON)
    uint8_t RESERVED0_7_2 : 6;     // Byte 0, bits 7-2: 保留
    
    /* Byte 1 (位8-15): BSD_STATUS_L[8:9], RESERVED[15:10] */
    uint8_t BSD_STATUS_L : 2;      // Byte 1, bits 8-9: 左侧盲点报警状态
    uint8_t RESERVED1_15_10 : 6;   // Byte 1, bits 10-15: 保留
    
    /* Byte 2 (位16-23): BSD_STATUS_R[16:17], RESERVED[23:18] */
    uint8_t BSD_STATUS_R : 2;      // Byte 2, bits 16-17: 右侧盲点报警状态
    uint8_t RESERVED2_23_18 : 6;   // Byte 2, bits 18-23: 保留
    
    /* Byte 3 (位24-31): SYSTEM_STATUS[24:26], RESERVED[27], LCMA_STATUS_L[28:29], LCMA_STATUS_R[30:31] */
    uint8_t SYSTEM_STATUS : 3;     // Byte 3, bits 24-26: 系统状态
    uint8_t RESERVED3_27 : 1;      // Byte 3, bit 27: 保留
    uint8_t LCMA_STATUS_L : 2;     // Byte 3, bits 28-29: 左侧变道辅助状态
    uint8_t LCMA_STATUS_R : 2;     // Byte 3, bits 30-31: 右侧变道辅助状态
    
    /* Byte 4 (位32-39): DOW_STATUS_L[32:33], DOW_STATUS_R[34:35], RCW_STATUS[36:37], RESERVED[39:38] */
    uint8_t DOW_STATUS_L : 2;      // Byte 4, bits 32-33: 左侧起步预警状态
    uint8_t DOW_STATUS_R : 2;      // Byte 4, bits 34-35: 右侧起步预警状态
    uint8_t RCW_STATUS : 2;        // Byte 4, bits 36-37: 后向碰撞预警状态
    uint8_t RESERVED4_39_38 : 2;   // Byte 4, bits 38-39: 保留
    
    uint8_t RESERVED5;             // Byte 5: 保留
    uint8_t RESERVED6;             // Byte 6: 保留
    
    uint8_t Software_version;      // Byte 7: 软件版本
} ZonsenStatusMsg;

/* 宗申协议目标信息报文结构体（Motorola LSB格式） */
typedef struct {
    uint8_t RESERVED0[2];          // Byte 0-1: 保留
    
    uint8_t DIST_X_L_LOW : 8;      // Byte 2: 纵向距离低字节
    uint8_t DIST_X_L_HIGH : 5;     // Byte 3, bits 0-4: 纵向距离高5位
    uint8_t RESERVED1 : 3;         // Byte 3, bits 5-7: 保留
    
    uint8_t RESERVED2;             // Byte 4: 保留
    
    uint8_t V_X_L_LOW : 6;         // Byte 5, bits 0-5: 纵向速度低6位
    uint8_t RESERVED3 : 2;         // Byte 5, bits 6-7: 保留
    uint8_t V_X_L_HIGH : 4;        // Byte 6, bits 0-3: 纵向速度高4位
    uint8_t RESERVED4 : 4;         // Byte 6, bits 4-7: 保留
    
    uint8_t RESERVED5;             // Byte 7: 保留
} ZonsenTargetMsg;

/* 宗申协议发送间隔 (ms) */
#define ZONSEN_STATUS_SEND_INTERVAL_MS   50
#define ZONSEN_TARGET_SEND_INTERVAL_MS   50

/* 预警优先级定义（数值越大优先级越高） */
#define ZONSEN_WARN_PRIORITY_NONE     0
#define ZONSEN_WARN_PRIORITY_BSD      1    // 盲点检测
#define ZONSEN_WARN_PRIORITY_LCA      2    // 变道辅助
#define ZONSEN_WARN_PRIORITY_AOA      3    // 起步预警(DOW)
#define ZONSEN_WARN_PRIORITY_RCW      4    // 后向碰撞预警（优先级最高）

static uint64_t g_zosen_last_status_send_time_ms = 0;
static uint64_t g_zosen_last_left_target_send_time_ms = 0;
static uint64_t g_zosen_last_right_target_send_time_ms = 0;

/**
 * @brief 获取当前时间（毫秒）
 */
static uint64_t zosen_get_current_time_ms(void)
{
    return get_time_ns() / 1000000;
}

/**
 * @brief 根据trkID查找轨迹在数组中的索引
 * @param trkID 轨迹ID
 * @return 数组索引，未找到返回-1
 */
static int zosen_find_track_index(uint8_t trkID)
{
    if (!g_pPktInfo) {
        return -1;
    }
    uint8_t num_trks = g_pPktInfo->trkInfo.header.numTrks;
    for (uint8_t i = 0; i < num_trks; i++) {
        if (g_pPktInfo->trkInfo.trkObj[i].trkID == trkID) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 获取航迹距离
 */
static float zosen_get_track_distance(uint8_t trkID)
{
    int idx = zosen_find_track_index(trkID);
    if (idx < 0) {
        return 0.0f;
    }
    return (g_pPktInfo->trkInfo.trkObj[idx].x_output / 8.0f - 255.0f);
}

/**
 * @brief 获取航迹纵向速度
 */
static float zosen_get_track_vx(uint8_t trkID)
{
    int idx = zosen_find_track_index(trkID);
    if (idx < 0) {
        return 0.0f;
    }
    return (g_pPktInfo->trkInfo.trkObj[idx].vx_output / 20.0f - 102.0f);
}

/**
 * @brief 查找最近的轨迹
 * @param is_left true=左侧, false=右侧
 * @return 最近轨迹的实际trkID，无轨迹返回0
 */
static uint8_t zosen_find_closest_track(bool is_left)
{
    if (!g_pPktInfo) {
        return 0;
    }
    
    uint8_t closest_trkID = 0;
    float min_distance = 9999.0f;
    uint8_t num_trks = g_pPktInfo->trkInfo.header.numTrks;
    
    // 直接在所有轨迹中查找最近的轨迹，不使用y坐标判断左右
    // 警告已经区分了左右，这里只需要找到最近的轨迹
    for (uint8_t i = 0; i < num_trks; i++) {
        float x = (g_pPktInfo->trkInfo.trkObj[i].x_output / 8.0f - 255.0f);
        
        // 计算距离绝对值（X轴是负轴，取绝对值）
        float abs_x = (x < 0) ? -x : x;
        
        if (abs_x < min_distance && abs_x > 0) {
            min_distance = abs_x;
            closest_trkID = g_pPktInfo->trkInfo.trkObj[i].trkID;
        }
    }
    
    return closest_trkID;
}

/**
 * @brief 根据报警级别查找对应的轨迹
 * @param is_left true=左侧, false=右侧（仅用于兼容接口，实际不使用y坐标判断）
 * @param level 报警级别（1=常亮, 2=闪烁）
 * @return 轨迹ID，未找到返回0
 */
static uint8_t zosen_find_track_by_warning_level(bool is_left, uint8_t level)
{
    if (!g_pPktInfo) {
        return 0;
    }
    
    uint8_t num_trks = g_pPktInfo->trkInfo.header.numTrks;
    float min_distance = 9999.0f;
    uint8_t best_trkID = 255;
    
    // 直接在所有轨迹中查找，不使用y坐标判断左右
    // 警告已经区分了左右，这里只需要找到符合距离范围的最近轨迹
    for (uint8_t i = 0; i < num_trks; i++) {
        float x = (g_pPktInfo->trkInfo.trkObj[i].x_output / 8.0f - 255.0f);
        
        // 计算距离绝对值（X轴是负轴，取绝对值）
        float abs_x = (x < 0) ? -x : x;
        
        // 根据报警级别筛选距离范围
        bool in_range = false;
        if (level == 1) {
            // 常亮级别：10-70m
            in_range = (abs_x > 10.0f && abs_x <= 70.0f);
        } else if (level == 2) {
            // 闪烁级别：0-10m
            in_range = (abs_x > 0 && abs_x <= 10.0f);
        } else {
            // 未知级别，接受所有距离
            in_range = (abs_x > 0);
        }
        
        if (in_range && abs_x < min_distance) {
            min_distance = abs_x;
            best_trkID = g_pPktInfo->trkInfo.trkObj[i].trkID;
        }
    }
    
    // 如果指定级别没找到，放宽条件找最近的轨迹
    if (best_trkID == 255) {
        min_distance = 9999.0f;
        for (uint8_t i = 0; i < num_trks; i++) {
            float x = (g_pPktInfo->trkInfo.trkObj[i].x_output / 8.0f - 255.0f);
            float abs_x = (x < 0) ? -x : x;
            
            if (abs_x < min_distance && abs_x > 0) {
                min_distance = abs_x;
                best_trkID = g_pPktInfo->trkInfo.trkObj[i].trkID;
            }
        }
    }
    
    return best_trkID;
}

/**
 * @brief 获取航迹径向速度
 */
static float zosen_get_track_radial_velocity(uint8_t trkID)
{
    int idx = zosen_find_track_index(trkID);
    if (idx < 0) {
        return 0.0f;
    }
    return (g_pPktInfo->trkInfo.trkObj[idx].vx_output / 20.0f - 102.0f);
}

/**
 * @brief 计算TTC（碰撞时间）
 * @param distance 纵向距离
 * @param radial_velocity 径向速度
 * @return TTC值（秒），如果无法计算则返回999.0f
 */
static float zosen_calculate_ttc(float distance, float radial_velocity)
{
    // 同号表示远离或静止，异号表示靠近
    if (distance * radial_velocity >= 0) {
        return 999.0f;
    }
    float ttc = -distance / radial_velocity;
    if (ttc < 0) ttc = 999.0f;
    return ttc;
}

/**
 * @brief 获取指定报警类型的最小TTC及其对应目标
 * @param warnType 报警类型（1=BSD, 2=LCA, 3=AOA, 4=RCW）
 * @param is_left 是否为左侧
 * @param trkIDs 目标ID数组
 * @param trkCnt 目标数量
 * @param out_trkID 输出：TTC最小的目标ID
 * @return 最小TTC值
 */
static float zosen_get_min_ttc_for_warn(uint8_t warnType, bool is_left, 
                                         const uint8_t *trkIDs, uint8_t trkCnt,
                                         uint8_t *out_trkID)
{
    float min_ttc = 999.0f;
    *out_trkID = 255;  /* 使用255作为无效ID，因为轨迹ID从0开始 */
    
    for (int i = 0; i < trkCnt; i++) {
        float distance = zosen_get_track_distance(trkIDs[i]);
        float radial_v = zosen_get_track_radial_velocity(trkIDs[i]);
        float ttc = zosen_calculate_ttc(distance, radial_v);
        if (ttc < min_ttc) {
            min_ttc = ttc;
            *out_trkID = trkIDs[i];
        }
    }
    return min_ttc;
}

/**
 * @brief 获取指定方向（左/右）的最小TTC
 * 遍历所有报警类型(BSD, LCA, AOA)，找到TTC最小的目标
 * RCW不参与目标选择（不输出距离和速度）
 * @param is_left true=左侧, false=右侧
 * @return 最小TTC值
 */
static float zosen_get_min_ttc(bool is_left)
{
    float min_ttc = 999.0f;
    const WarnResult *warnResult = &g_pPktInfo->warnInfo.warnResult;
    
    if (is_left) {
        // 左侧BSD
        if (warnResult->BSD_left && warnResult->BSD_left_cnt > 0) {
            uint8_t trkID = 255;
            float ttc = zosen_get_min_ttc_for_warn(1, true, 
                warnResult->BSD_left_IDs, warnResult->BSD_left_cnt, &trkID);
            if (ttc < min_ttc) min_ttc = ttc;
        }
        // 左侧LCA
        if (warnResult->LCA_left && warnResult->LCA_left_cnt > 0) {
            uint8_t trkID = 255;
            float ttc = zosen_get_min_ttc_for_warn(2, true, 
                warnResult->LCA_left_IDs, warnResult->LCA_left_cnt, &trkID);
            if (ttc < min_ttc) min_ttc = ttc;
        }
        // 左侧AOA（DOW预警）
        if (warnResult->AOA_left && warnResult->AOA_left_cnt > 0) {
            uint8_t trkID = 255;
            float ttc = zosen_get_min_ttc_for_warn(3, true, 
                warnResult->AOA_left_IDs, warnResult->AOA_left_cnt, &trkID);
            if (ttc < min_ttc) min_ttc = ttc;
        }
    } else {
        // 右侧BSD
        if (warnResult->BSD_right && warnResult->BSD_right_cnt > 0) {
            uint8_t trkID = 255;
            float ttc = zosen_get_min_ttc_for_warn(1, false, 
                warnResult->BSD_right_IDs, warnResult->BSD_right_cnt, &trkID);
            if (ttc < min_ttc) min_ttc = ttc;
        }
        // 右侧LCA
        if (warnResult->LCA_right && warnResult->LCA_right_cnt > 0) {
            uint8_t trkID = 255;
            float ttc = zosen_get_min_ttc_for_warn(2, false, 
                warnResult->LCA_right_IDs, warnResult->LCA_right_cnt, &trkID);
            if (ttc < min_ttc) min_ttc = ttc;
        }
        // 右侧AOA（DOW预警）
        if (warnResult->AOA_right && warnResult->AOA_right_cnt > 0) {
            uint8_t trkID = 255;
            float ttc = zosen_get_min_ttc_for_warn(3, false, 
                warnResult->AOA_right_IDs, warnResult->AOA_right_cnt, &trkID);
            if (ttc < min_ttc) min_ttc = ttc;
        }
    }
    
    return min_ttc;
}

/**
 * @brief 将物理距离转换为总线值
 * 公式：物理距离 = 总线值 × 0.04 + (-100) m
 * 反向：总线值 = (物理距离 + 100) / 0.04
 */
static uint16_t zosen_distance_to_bus(float distance)
{
    float bus_value = (distance + 100.0f) / 0.04f;
    if (bus_value < 0) return 0;
    if (bus_value > 0x1FFF) return 0x1FFF;
    return (uint16_t)bus_value;
}

/**
 * @brief 将物理速度转换为总线值
 * 公式：物理速度 = 总线值 × 0.25 + (-128) m/s
 * 反向：总线值 = (物理速度 + 128) / 0.25
 */
static uint16_t zosen_velocity_to_bus(float velocity)
{
    float bus_value = (velocity + 128.0f) / 0.25f;
    if (bus_value < 0) return 0;
    if (bus_value > 0x3FF) return 0x3FF;
    return (uint16_t)bus_value;
}

/**
 * @brief 构造宗申协议所有报文数据（0x50A, 0x60B, 0x61B）
 * 
 * 报文逻辑：
 *   - 状态报文(0x50A)：所有警告类型独立上报，各状态字段独立设置
 *   - 目标报文(0x60B/0x61B)：按优先级顺序选择第一个触发的警告类型的目标
 *     RCW(优先级4) → AOA(优先级3) → LCA(优先级2) → BSD(优先级1)
 *     RCW只更新状态，不输出目标
 * 
 * @param status_data 状态报文数据缓冲（0x50A）
 * @param left_target_data 左侧目标报文数据缓冲（0x60B）
 * @param right_target_data 右侧目标报文数据缓冲（0x61B）
 * @param warnResult 报警结果
 */
static void zosen_build_all_msgs(uint8_t *status_data, uint8_t *left_target_data, 
                                 uint8_t *right_target_data, const WarnResult *warnResult)
{
    /* 初始化状态报文 */
    ZonsenStatusMsg statusMsg;
    memset(&statusMsg, 0, sizeof(ZonsenStatusMsg));
    statusMsg.SW_Resp = BSD_FUNC_ENABLE;
    statusMsg.SYSTEM_STATUS = BSD_STATUS_ACTIVATED;
    statusMsg.Software_version = 0x01;
    
    /* 初始化目标信息报文 */
    ZonsenTargetMsg leftTargetMsg, rightTargetMsg;
    memset(&leftTargetMsg, 0, sizeof(leftTargetMsg));
    memset(&rightTargetMsg, 0, sizeof(rightTargetMsg));
    
    /* 左侧/右侧目标选择变量 */
    uint8_t best_trkID_left = 255, best_trkID_right = 255;
    bool left_target_found = false;
    bool right_target_found = false;
    
    /* ========== 第一步：设置状态报文（所有警告类型独立上报） ========== */
    
    /* RCW状态 - 只更新状态，不参与目标选择 */
    if (warnResult->RCW && warnResult->RCW_cnt > 0) {
        statusMsg.RCW_STATUS = ALARM_STANDARD;
    }
    
    /* AOA（DOW预警）状态 - 左侧 */
    if (warnResult->AOA_left) {
        uint8_t trkID = 255;
        if (warnResult->AOA_left_cnt > 0) {
            zosen_get_min_ttc_for_warn(3, true, warnResult->AOA_left_IDs, warnResult->AOA_left_cnt, &trkID);
        } else {
            trkID = zosen_find_closest_track(true);
        }
        float dist = zosen_get_track_distance(trkID);
        float abs_dist = (dist < 0) ? -dist : dist;
        if (abs_dist > 0 && abs_dist <= 10.0f) {
            statusMsg.DOW_STATUS_L = ALARM_ENHANCED;
        } else if (abs_dist > 10.0f && abs_dist <= 70.0f) {
            statusMsg.DOW_STATUS_L = ALARM_STANDARD;
        }
    }
    
    /* AOA（DOW预警）状态 - 右侧 */
    if (warnResult->AOA_right) {
        uint8_t trkID = 255;
        if (warnResult->AOA_right_cnt > 0) {
            zosen_get_min_ttc_for_warn(3, false, warnResult->AOA_right_IDs, warnResult->AOA_right_cnt, &trkID);
        } else {
            trkID = zosen_find_closest_track(false);
        }
        float dist = zosen_get_track_distance(trkID);
        float abs_dist = (dist < 0) ? -dist : dist;
        if (abs_dist > 0 && abs_dist <= 10.0f) {
            statusMsg.DOW_STATUS_R = ALARM_ENHANCED;
        } else if (abs_dist > 10.0f && abs_dist <= 70.0f) {
            statusMsg.DOW_STATUS_R = ALARM_STANDARD;
        }
    }
    
    /* LCA状态 - 左侧 */
    if (warnResult->LCA_left) {
        float dist = 0.0f;
        uint8_t trkID = 255;
        if (warnResult->LCA_left_cnt > 0) {
            zosen_get_min_ttc_for_warn(2, true, warnResult->LCA_left_IDs, warnResult->LCA_left_cnt, &trkID);
            // 如果通过ID没找到有效目标，尝试其他方法
            if (trkID == 255 && warnResult->LCA_left_level > 0) {
                trkID = zosen_find_track_by_warning_level(true, warnResult->LCA_left_level);
            }
            if (trkID == 255) {
                trkID = zosen_find_closest_track(true);
            }
            dist = zosen_get_track_distance(trkID);
        } else if (warnResult->LCA_left_level > 0) {
            trkID = zosen_find_track_by_warning_level(true, warnResult->LCA_left_level);
            if (trkID != 255) {
                dist = zosen_get_track_distance(trkID);
            }
        } else {
            trkID = zosen_find_closest_track(true);
            if (trkID != 255) {
                dist = zosen_get_track_distance(trkID);
            }
        }
        float abs_dist = (dist < 0) ? -dist : dist;
        if (abs_dist > 0 && abs_dist <= 10.0f) {
            statusMsg.LCMA_STATUS_L = ALARM_ENHANCED;
        } else if (abs_dist > 10.0f && abs_dist <= 70.0f) {
            statusMsg.LCMA_STATUS_L = ALARM_STANDARD;
        }
    }
    
    /* LCA状态 - 右侧 */
    if (warnResult->LCA_right) {
        float dist = 0.0f;
        uint8_t trkID = 255;
        if (warnResult->LCA_right_cnt > 0) {
            zosen_get_min_ttc_for_warn(2, false, warnResult->LCA_right_IDs, warnResult->LCA_right_cnt, &trkID);
            // 如果通过ID没找到有效目标，尝试其他方法
            if (trkID == 255 && warnResult->LCA_right_level > 0) {
                trkID = zosen_find_track_by_warning_level(false, warnResult->LCA_right_level);
            }
            if (trkID == 255) {
                trkID = zosen_find_closest_track(false);
            }
            dist = zosen_get_track_distance(trkID);
        } else if (warnResult->LCA_right_level > 0) {
            trkID = zosen_find_track_by_warning_level(false, warnResult->LCA_right_level);
            if (trkID != 255) {
                dist = zosen_get_track_distance(trkID);
            }
        } else {
            trkID = zosen_find_closest_track(false);
            if (trkID != 255) {
                dist = zosen_get_track_distance(trkID);
            }
        }
        float abs_dist = (dist < 0) ? -dist : dist;
        if (abs_dist > 0 && abs_dist <= 10.0f) {
            statusMsg.LCMA_STATUS_R = ALARM_ENHANCED;
        } else if (abs_dist > 10.0f && abs_dist <= 70.0f) {
            statusMsg.LCMA_STATUS_R = ALARM_STANDARD;
        }
    }
    
    /* BSD状态 - 左侧 */
    if (warnResult->BSD_left && warnResult->BSD_left_cnt > 0) {
        uint8_t trkID = 255;
        zosen_get_min_ttc_for_warn(1, true, warnResult->BSD_left_IDs, warnResult->BSD_left_cnt, &trkID);
        float dist = zosen_get_track_distance(trkID);
        float abs_dist = (dist < 0) ? -dist : dist;
        if (abs_dist > 0 && abs_dist <= 10.0f) {
            statusMsg.BSD_STATUS_L = ALARM_ENHANCED;
        } else if (abs_dist > 10.0f && abs_dist <= 70.0f) {
            statusMsg.BSD_STATUS_L = ALARM_STANDARD;
        }
    }
    
    /* BSD状态 - 右侧 */
    if (warnResult->BSD_right && warnResult->BSD_right_cnt > 0) {
        uint8_t trkID = 255;
        zosen_get_min_ttc_for_warn(1, false, warnResult->BSD_right_IDs, warnResult->BSD_right_cnt, &trkID);
        float dist = zosen_get_track_distance(trkID);
        float abs_dist = (dist < 0) ? -dist : dist;
        if (abs_dist > 0 && abs_dist <= 10.0f) {
            statusMsg.BSD_STATUS_R = ALARM_ENHANCED;
        } else if (abs_dist > 10.0f && abs_dist <= 70.0f) {
            statusMsg.BSD_STATUS_R = ALARM_STANDARD;
        }
    }
    
    /* ========== 第二步：选择目标报文的目标（按优先级顺序：RCW→AOA→BSD→LCA） ========== */
    
    /* RCW：只更新状态，不输出目标，继续检查下一个 */
    
    /* AOA：优先级最高的目标警告类型 */
    /* 左侧AOA */
    if (!left_target_found && warnResult->AOA_left) {
        uint8_t trkID = 255;
        if (warnResult->AOA_left_cnt > 0) {
            zosen_get_min_ttc_for_warn(3, true, warnResult->AOA_left_IDs, warnResult->AOA_left_cnt, &trkID);
        } else {
            trkID = zosen_find_closest_track(true);
        }
        if (trkID != 255) {
            best_trkID_left = trkID;
            left_target_found = true;
        }
    }
    /* 右侧AOA */
    if (!right_target_found && warnResult->AOA_right) {
        uint8_t trkID = 255;
        if (warnResult->AOA_right_cnt > 0) {
            zosen_get_min_ttc_for_warn(3, false, warnResult->AOA_right_IDs, warnResult->AOA_right_cnt, &trkID);
        } else {
            trkID = zosen_find_closest_track(false);
        }
        if (trkID != 255) {
            best_trkID_right = trkID;
            right_target_found = true;
        }
    }
    
    /* BSD：优先级次高，只有AOA没选中时才选 */
    /* 左侧BSD */
    if (!left_target_found && warnResult->BSD_left && warnResult->BSD_left_cnt > 0) {
        uint8_t trkID = 255;
        zosen_get_min_ttc_for_warn(1, true, warnResult->BSD_left_IDs, warnResult->BSD_left_cnt, &trkID);
        if (trkID != 255) {
            best_trkID_left = trkID;
            left_target_found = true;
        }
    }
    /* 右侧BSD */
    if (!right_target_found && warnResult->BSD_right && warnResult->BSD_right_cnt > 0) {
        uint8_t trkID = 255;
        zosen_get_min_ttc_for_warn(1, false, warnResult->BSD_right_IDs, warnResult->BSD_right_cnt, &trkID);
        if (trkID != 255) {
            best_trkID_right = trkID;
            right_target_found = true;
        }
    }
    
    /* LCA：优先级最低，只有AOA和BSD都没选中时才选 */
    /* 左侧LCA */
    if (!left_target_found && warnResult->LCA_left) {
        uint8_t trkID = 255;
        if (warnResult->LCA_left_cnt > 0) {
            zosen_get_min_ttc_for_warn(2, true, warnResult->LCA_left_IDs, warnResult->LCA_left_cnt, &trkID);
            // 如果通过ID没找到有效目标，尝试其他方法
            if (trkID == 255 && warnResult->LCA_left_level > 0) {
                trkID = zosen_find_track_by_warning_level(true, warnResult->LCA_left_level);
            }
            if (trkID == 255) {
                trkID = zosen_find_closest_track(true);
            }
        } else if (warnResult->LCA_left_level > 0) {
            trkID = zosen_find_track_by_warning_level(true, warnResult->LCA_left_level);
        } else {
            trkID = zosen_find_closest_track(true);
        }
        if (trkID != 255) {
            best_trkID_left = trkID;
            left_target_found = true;
        }
    }
    /* 右侧LCA */
    if (!right_target_found && warnResult->LCA_right) {
        uint8_t trkID = 255;
        if (warnResult->LCA_right_cnt > 0) {
            zosen_get_min_ttc_for_warn(2, false, warnResult->LCA_right_IDs, warnResult->LCA_right_cnt, &trkID);
            // 如果通过ID没找到有效目标，尝试其他方法
            if (trkID == 255 && warnResult->LCA_right_level > 0) {
                trkID = zosen_find_track_by_warning_level(false, warnResult->LCA_right_level);
            }
            if (trkID == 255) {
                trkID = zosen_find_closest_track(false);
            }
        } else if (warnResult->LCA_right_level > 0) {
            trkID = zosen_find_track_by_warning_level(false, warnResult->LCA_right_level);
        } else {
            trkID = zosen_find_closest_track(false);
        }
        if (trkID != 255) {
            best_trkID_right = trkID;
            right_target_found = true;
        }
    }
    
    /* ========== 第三步：输出结果 ========== */
    
    /* 输出状态报文 */
    memcpy(status_data, &statusMsg, sizeof(ZonsenStatusMsg));
    
    /* 输出左侧目标信息报文 */
    if (left_target_found) {
        float distance = zosen_get_track_distance(best_trkID_left);
        float velocity = zosen_get_track_vx(best_trkID_left);
        if (distance < 0) distance = -distance;  /* X轴取绝对值 */
        uint16_t dist_bus = zosen_distance_to_bus(distance);
        uint16_t vel_bus = zosen_velocity_to_bus(velocity);
        leftTargetMsg.DIST_X_L_LOW = (uint8_t)(dist_bus & 0xFF);
        leftTargetMsg.DIST_X_L_HIGH = (uint8_t)((dist_bus >> 8) & 0x1F);
        leftTargetMsg.V_X_L_LOW = (uint8_t)(vel_bus & 0x3F);
        leftTargetMsg.V_X_L_HIGH = (uint8_t)((vel_bus >> 6) & 0x0F);
    }
    memcpy(left_target_data, &leftTargetMsg, sizeof(ZonsenTargetMsg));
    
    /* 输出右侧目标信息报文 */
    if (right_target_found) {
        float distance = zosen_get_track_distance(best_trkID_right);
        float velocity = zosen_get_track_vx(best_trkID_right);
        if (distance < 0) distance = -distance;  /* X轴取绝对值 */
        uint16_t dist_bus = zosen_distance_to_bus(distance);
        uint16_t vel_bus = zosen_velocity_to_bus(velocity);
        rightTargetMsg.DIST_X_L_LOW = (uint8_t)(dist_bus & 0xFF);
        rightTargetMsg.DIST_X_L_HIGH = (uint8_t)((dist_bus >> 8) & 0x1F);
        rightTargetMsg.V_X_L_LOW = (uint8_t)(vel_bus & 0x3F);
        rightTargetMsg.V_X_L_HIGH = (uint8_t)((vel_bus >> 6) & 0x0F);
    }
    memcpy(right_target_data, &rightTargetMsg, sizeof(ZonsenTargetMsg));
}

/**
 * @brief 发送宗申协议CAN报文
 */
static int zosen_frame_pkt_send(void)
{
    struct can_message tx_msg;
    uint8_t status_data[8];
    uint8_t left_target_data[8];
    uint8_t right_target_data[8];
    int ret;
    uint64_t current_time_ms;

    if (!g_mcp2515_dev || !g_pPktInfo) {
        printf("MCP2515 not initialized or invalid packet info\n");
        return -1;
    }

    current_time_ms = zosen_get_current_time_ms();
    
    /* 计算所有报文数据（0x50A, 0x60B, 0x61B）- 每次都重新计算以保证实时性 */
    zosen_build_all_msgs(status_data, left_target_data, right_target_data, &g_pPktInfo->warnInfo.warnResult);
    
    /* 发送BSD状态报文 0x50A */
    if ((current_time_ms - g_zosen_last_status_send_time_ms >= ZONSEN_STATUS_SEND_INTERVAL_MS)) {
        ZonsenStatusMsg *statusMsg = (ZonsenStatusMsg *)status_data;
        
        printf("[ZONSEN CAN TX] ID:0x%03X, Data: %02X %02X %02X %02X %02X %02X %02X %02X\n",
               ZONSEN_CAN_ID_STATUS,
               status_data[0], status_data[1], status_data[2], status_data[3],
               status_data[4], status_data[5], status_data[6], status_data[7]);
        
        printf("[ZONSEN STATUS MSG] SW_Resp=%d, BSD_L=%d, BSD_R=%d, SYS=%d, LCMA_L=%d, LCMA_R=%d, DOW_L=%d, DOW_R=%d, RCW=%d, Ver=0x%02X\n",
               statusMsg->SW_Resp,
               statusMsg->BSD_STATUS_L,
               statusMsg->BSD_STATUS_R,
               statusMsg->SYSTEM_STATUS,
               statusMsg->LCMA_STATUS_L,
               statusMsg->LCMA_STATUS_R,
               statusMsg->DOW_STATUS_L,
               statusMsg->DOW_STATUS_R,
               statusMsg->RCW_STATUS,
               statusMsg->Software_version);
        
        can_send_data_nb(ZONSEN_CAN_ID_STATUS, status_data, 8);
        g_zosen_last_status_send_time_ms = current_time_ms;
    }
    
    /* 发送左侧目标信息报文 0x60B */
    if ((current_time_ms - g_zosen_last_left_target_send_time_ms >= ZONSEN_TARGET_SEND_INTERVAL_MS)) {
        ZonsenTargetMsg *targetMsg = (ZonsenTargetMsg *)left_target_data;
        
        printf("[ZONSEN CAN TX] ID:0x%03X, Data: %02X %02X %02X %02X %02X %02X %02X %02X\n",
               ZONSEN_CAN_ID_LEFT_TARGET,
               left_target_data[0], left_target_data[1], left_target_data[2], left_target_data[3],
               left_target_data[4], left_target_data[5], left_target_data[6], left_target_data[7]);
        
        uint16_t dist_bus = (targetMsg->DIST_X_L_HIGH << 8) | targetMsg->DIST_X_L_LOW;
        uint16_t vel_bus = (targetMsg->V_X_L_HIGH << 6) | targetMsg->V_X_L_LOW;
        float dist_phys = dist_bus * 0.04f - 100.0f;
        float vel_phys = vel_bus * 0.25f - 128.0f;
        float min_ttc = zosen_get_min_ttc(true);
        printf("[ZONSEN TARGET LEFT] TTC=%.2fs, DIST_X=0x%04X(%.2fm), V_X=0x%04X(%.2fm/s, %.2fkm/h)\n",
               min_ttc, dist_bus, dist_phys, vel_bus, vel_phys, vel_phys * 3.6f);
        
        can_send_data_nb(ZONSEN_CAN_ID_LEFT_TARGET, left_target_data, 8);
        g_zosen_last_left_target_send_time_ms = current_time_ms;
    }
    
    /* 发送右侧目标信息报文 0x61B */
    if ((current_time_ms - g_zosen_last_right_target_send_time_ms >= ZONSEN_TARGET_SEND_INTERVAL_MS)) {
        ZonsenTargetMsg *targetMsg = (ZonsenTargetMsg *)right_target_data;
        
        printf("[ZONSEN CAN TX] ID:0x%03X, Data: %02X %02X %02X %02X %02X %02X %02X %02X\n",
               ZONSEN_CAN_ID_RIGHT_TARGET,
               right_target_data[0], right_target_data[1], right_target_data[2], right_target_data[3],
               right_target_data[4], right_target_data[5], right_target_data[6], right_target_data[7]);
        
        uint16_t dist_bus = (targetMsg->DIST_X_L_HIGH << 8) | targetMsg->DIST_X_L_LOW;
        uint16_t vel_bus = (targetMsg->V_X_L_HIGH << 6) | targetMsg->V_X_L_LOW;
        float dist_phys = dist_bus * 0.04f - 100.0f;
        float vel_phys = vel_bus * 0.25f - 128.0f;
        float min_ttc = zosen_get_min_ttc(false);
        printf("[ZONSEN TARGET RIGHT] TTC=%.2fs, DIST_X=0x%04X(%.2fm), V_X=0x%04X(%.2fm/s, %.2fkm/h)\n",
               min_ttc, dist_bus, dist_phys, vel_bus, vel_phys, vel_phys * 3.6f);
        
        can_send_data_nb(ZONSEN_CAN_ID_RIGHT_TARGET, right_target_data, 8);
        g_zosen_last_right_target_send_time_ms = current_time_ms;
    }

    printf("Zonsen frame packet sent: FrameID=%d\n", gFrameID);
    return 0;
}

