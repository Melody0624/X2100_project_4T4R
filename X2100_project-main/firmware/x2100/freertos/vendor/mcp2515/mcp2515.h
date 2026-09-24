#ifndef __MCP2515_H__
#define __MCP2515_H__

#include <stdint.h>
#include "mmw_msg_pkt.h"

/* MCP2515寄存器地址 */
#define MCP2515_REG_CANSTAT       0x0E
#define MCP2515_REG_CANCTRL       0x0F
#define MCP2515_REG_BFPCTRL       0x0C
#define MCP2515_REG_TXRTSCTRL     0x0D

#define MCP2515_REG_RXB0CTRL      0x60
#define MCP2515_REG_RXB1CTRL      0x70

#define MCP2515_REG_CANINTE       0x2B
#define MCP2515_REG_CANINTF       0x2C

#define MCP2515_REG_EFLG          0x2D

/* TX缓冲区寄存器基地址 */
#define MCP2515_TXB0CTRL          0x30
#define MCP2515_TXB0SIDH          0x31
#define MCP2515_TXB1CTRL          0x40
#define MCP2515_TXB1SIDH          0x41
#define MCP2515_TXB2CTRL          0x50
#define MCP2515_TXB2SIDH          0x51

/* TXBnCTRL 位定义 */
#define TXB_TXREQ                 (1 << 3)   /* 发送请求位 */
#define TXB_TXERR                 (1 << 4)   /* 发送错误位 */
#define TXB_MLOA                  (1 << 5)   /* 报文丢失仲裁位 */
#define TXB_ABTF                  (1 << 6)   /* 报文中止发送位 */

/* CANINTF 中断标志位 */
#define CANINTF_TX0IF             (1 << 2)
#define CANINTF_TX1IF             (1 << 3)
#define CANINTF_TX2IF             (1 << 4)

/* CAN帧标志和掩码 */
#define CAN_EFF_FLAG              0x80000000UL  /* 扩展帧标志 */
#define CAN_RTR_FLAG              0x40000000UL  /* 远程帧标志 */
#define CAN_SFF_MASK              0x000007FFUL  /* 标准帧ID掩码 */
#define CAN_EFF_MASK              0x1FFFFFFFUL  /* 扩展帧ID掩码 */
#define CAN_MAX_DLEN              8             /* 最大数据长度 */

/* RTR掩码 */
#define RTR_MASK                  0x40

/* TX缓冲区数量 */
#define N_TXBUFFERS               3

/* 返回码 */
#define MCP2515_OK                0
#define MCP2515_FAILTX            -1
#define MCP2515_ALLTXBUSY         -2
#define MCP2515_TX_TIMEOUT        -3

/* MCP2515命令 */
#define MCP2515_CMD_RESET         0xC0
#define MCP2515_CMD_READ          0x03
#define MCP2515_CMD_WRITE         0x02
#define MCP2515_CMD_RTS           0x80
#define MCP2515_CMD_READ_STATUS   0xA0
#define MCP2515_CMD_RX_STATUS     0xB0
#define MCP2515_CMD_BIT_MODIFY    0x05

/* 模式定义 */
#define MCP2515_MODE_NORMAL       0x00
#define MCP2515_MODE_SLEEP        0x20
#define MCP2515_MODE_LOOPBACK     0x40
#define MCP2515_MODE_LISTENONLY   0x60
#define MCP2515_MODE_CONFIG       0x80

/* 位定时配置 */
struct mcp2515_bit_timing {
    uint8_t cnf1;
    uint8_t cnf2;
    uint8_t cnf3;
};

/* CAN消息结构 */
struct can_message {
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint8_t extended;
};

/* MCP2515设备结构 */
struct mcp2515_dev {
    struct spi_device *spi;
    uint8_t cs_pin;
    volatile uint8_t tx_busy_mask;
};

/* 函数声明 */
uint8_t mcp2515_get_tx_status(struct mcp2515_dev *dev, uint8_t txbn);
uint8_t getInterrupts(struct mcp2515_dev *dev);
void mcp2515_clear_tx_int(struct mcp2515_dev *dev, uint8_t txbn);
uint8_t mcp2515_get_error_flags(struct mcp2515_dev *dev);
void mcp2515_clear_error_flags(struct mcp2515_dev *dev);
int mcp2515_abort_tx(struct mcp2515_dev *dev, uint8_t txbn);
struct mcp2515_dev *mcp2515_init(uint32_t spi_id, uint32_t cs_pin, struct mcp2515_bit_timing *bit_timing);
int mcp2515_send(struct mcp2515_dev *dev, struct can_message *msg);
int mcp2515_send_nb(struct mcp2515_dev *dev, uint8_t txbn, struct can_message *msg);
int mcp2515_recv(struct mcp2515_dev *dev, struct can_message *msg);
int mcp2515_set_mode(struct mcp2515_dev *dev, uint8_t mode);
void mcp2515_deinit(struct mcp2515_dev *dev);
void mcp2515_task_init(void);
int can_frame_pkt_update(Mmw_pkt_info *pktInfo, const char *pktBuf, int32_t pktSize, uint32_t frameID);
int CanNewProtocolPkt(Mmw_pkt_info *pktInfo, uint32_t frameID);
#endif /* __MCP2515_H__ */
