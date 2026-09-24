#ifndef __BOARD_H__
#define __BOARD_H__

#include "mmw_msg_pkt.h"

#define LEFT_LED GPIO_PC(2)
#define RIGHT_LED GPIO_PC(3)
#define BUZZER GPIO_PC(1)

#define LED_ON             1
#define LED_OFF            0
#define BUZZER_ON             1
#define BUZZER_OFF            0 

typedef enum {
    WARN_NONE = 0,
    WARN_BSD_LEFT,
    WARN_BSD_RIGHT,
    WARN_AOA_LEFT,
    WARN_AOA_RIGHT,
    WARN_LCA_LEFT,
    WARN_LCA_RIGHT,
    WARN_RCW
} WarnType;

/**
 * @brief 初始化板级应用
 *
 * @param  void
 * @return void
 */
void board_init(void);

int Warn_frame_pkt_update(Mmw_pkt_info* pktInfo, uint32_t frameID);

/* ================================================================
 * 预警测试协议定义（用于UART/CAN接收上位机指令后驱动LED/蜂鸣器）
 *  预警协议解析规则
 * |-----------------------------------------------------------------------------------|
 * |方向 (bits 0-1)	|bit2       |bit3       |bit4      |bit5       |bit6      |bit7     |
 * |-----------------------------------------------------------------------------------|
 * |01=正后方(RCW)	|RCW使能	 |≤10m       |保留	    |保留	     |保留	     |保留     |
 * |-----------------------------------------------------------------------------------|
 * |10=左方	        |BSD使能	 |BSD≤10m	 |AOA使能	|AOA≤10m	|LCA使能	|LCA≤10m  |
 * |-----------------------------------------------------------------------------------|
 * |11=右方	        |BSD使能	 |BSD≤10m    |AOA使能	|AOA≤10m	|LCA使能	|LCA≤10m  |
 * |-----------------------------------------------------------------------------------|
 * ================================================================ */
// 预警测试协议解析结果
typedef struct {
    uint8_t direction;      // 方向: 0=无效, 1=正后方(RCW), 2=左方, 3=右方
    bool warn_active;       // 是否有预警
    bool distance_near;     // 是否<=10m
    bool rcw;               // RCW预警（正后方）
    bool bsd;               // BSD预警（左/右）
    bool aoa;               // AOA预警（左/右）
    bool lca;               // LCA预警（左/右）
} WarnTestResult;

// 预警测试方向常量
#define WARN_DIR_BACK   0x01
#define WARN_DIR_LEFT   0x02
#define WARN_DIR_RIGHT  0x03

/**
 * @brief 预警测试统一入口（UART/CAN指令解析后调用）
 * @param warn_byte 预警协议字节
 *        bits[0-1]: 方向 (01=后方, 10=左方, 11=右方)
 *        bits[2-7]: 按方向不同含义参见协议文档
 */
void warn_test_handler(uint8_t warn_byte);

/**
 * @brief 预警测试扩展接口（直接传入解析结果）
 * @param result 已解析的预警测试结果
 */
void warn_test_handler_ext(WarnTestResult *result);

/**
 * @brief 预警指令测试入口（UART文本命令格式）
 * @param binary_str 二进制字符串，如 "01000100"
 */
void warn_test_from_binary_string(const char *binary_str);

#endif
