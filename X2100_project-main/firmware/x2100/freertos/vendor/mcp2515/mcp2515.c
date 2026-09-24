#include "mcp2515.h"
#include <driver/spi.h>
#include <driver/gpio.h>
#include <os.h>
#include <os/thread.h>
#include "heap_malloc.h"

static uint8_t mcp2515_spi_read(struct mcp2515_dev *dev, uint8_t reg)
{
    u8 tx_buf[] = {MCP2515_CMD_READ, reg, 0x00};
    u8 rx_buf[3];
    struct spi_message msg[] = {
        {
            .tx_buf = tx_buf,
            .rx_buf = rx_buf,
            .tlen = 3,
            .rlen = 3,
            .cs_change = 0,
        },
    };
    
    spi_transfer(dev->spi, msg, 1);
    return rx_buf[2];
}

static void mcp2515_spi_write(struct mcp2515_dev *dev, uint8_t reg, uint8_t val)
{
    u8 tx_buf[] = {MCP2515_CMD_WRITE, reg, val};
    struct spi_message msg[] = {
        {
            .tx_buf = tx_buf,
            .rx_buf = NULL,
            .tlen = 3,
            .rlen = 0,
            .cs_change = 0,
        },
    };
    
    spi_transfer(dev->spi, msg, 1);
}

static void mcp2515_spi_bit_modify(struct mcp2515_dev *dev, uint8_t reg, uint8_t mask, uint8_t val)
{
    u8 tx_buf[] = {MCP2515_CMD_BIT_MODIFY, reg, mask, val};
    struct spi_message msg[] = {
        {
            .tx_buf = tx_buf,
            .rx_buf = NULL,
            .tlen = 4,
            .rlen = 0,
            .cs_change = 0,
        },
    };
    
    spi_transfer(dev->spi, msg, 1);
}

static void mcp2515_spi_reset(struct mcp2515_dev *dev)
{
    u8 tx_buf[] = {MCP2515_CMD_RESET};
    struct spi_message msg[] = {
        {
            .tx_buf = tx_buf,
            .rx_buf = NULL,
            .tlen = 1,
            .rlen = 0,
            .cs_change = 0,
        },
    };
    
    spi_transfer(dev->spi, msg, 1);
    msleep(1);
}

struct mcp2515_dev *mcp2515_init(uint32_t spi_id, uint32_t cs_pin, struct mcp2515_bit_timing *bit_timing)
{
    struct mcp2515_dev *dev = (struct mcp2515_dev *)malloc(sizeof(struct mcp2515_dev));
    if (!dev) {
        return NULL;
    }
    
    struct spi_config_data config = {
        .id = spi_id,
        .cs_pin = cs_pin,
        .clk_rate = 2 * 1000 * 1000,
        .cs_valid_level = Spi_valid_low,
        .tx_endian = Spi_endian_msb_first,
        .rx_endian = Spi_endian_msb_first,
        .bits_per_word = 8,
        .spi_pha = 0,
        .spi_pol = 0,
        .loop_mode = 0,
    };
    
    dev->spi = spi_register(&config);
    if (!dev->spi) {
        free(dev);
        return NULL;
    }
    
    dev->cs_pin = cs_pin;
    
    /* 重置MCP2515 */
    mcp2515_spi_reset(dev);
    
    /* 进入配置模式 */
    mcp2515_set_mode(dev, MCP2515_MODE_CONFIG);
    
    /* 配置位定时 */
    if (bit_timing) {
        mcp2515_spi_write(dev, 0x2A, bit_timing->cnf1);
        mcp2515_spi_write(dev, 0x29, bit_timing->cnf2);
        mcp2515_spi_write(dev, 0x28, bit_timing->cnf3);
    } else {
        /* 默认配置为500Kbps @ 16MHz晶振 */
        mcp2515_spi_write(dev, 0x2A, 0x00);
        mcp2515_spi_write(dev, 0x29, 0xD4);
        mcp2515_spi_write(dev, 0x28, 0x03);
    }
    
    /* 配置接收缓冲区 */
    mcp2515_spi_write(dev, MCP2515_REG_RXB0CTRL, 0x60);
    mcp2515_spi_write(dev, MCP2515_REG_RXB1CTRL, 0x60);
    
    /* 启用中断 */
    mcp2515_spi_write(dev, MCP2515_REG_CANINTE, 0x1F);
    
    /* 进入正常模式 */
    mcp2515_set_mode(dev, MCP2515_MODE_NORMAL);
    
    return dev;
}

uint8_t mcp2515_get_tx_status(struct mcp2515_dev *dev, uint8_t txbn)
{
    uint8_t regs[] = {MCP2515_TXB0CTRL, MCP2515_TXB1CTRL, MCP2515_TXB2CTRL};
    return mcp2515_spi_read(dev, regs[txbn]);
}

uint8_t getInterrupts(struct mcp2515_dev *dev)
{
    return mcp2515_spi_read(dev, MCP2515_REG_CANINTF);
}

void mcp2515_clear_tx_int(struct mcp2515_dev *dev, uint8_t txbn)
{
    uint8_t intf_bit = (txbn == 0) ? CANINTF_TX0IF : (txbn == 1) ? CANINTF_TX1IF : CANINTF_TX2IF;
    mcp2515_spi_bit_modify(dev, MCP2515_REG_CANINTF, intf_bit, 0);
}

uint8_t mcp2515_get_error_flags(struct mcp2515_dev *dev)
{
    return mcp2515_spi_read(dev, MCP2515_REG_EFLG);
}

void mcp2515_clear_error_flags(struct mcp2515_dev *dev)
{
    mcp2515_spi_bit_modify(dev, MCP2515_REG_EFLG, 0xFF, 0);
}

int mcp2515_abort_tx(struct mcp2515_dev *dev, uint8_t txbn)
{
	uint8_t ctrl_regs[N_TXBUFFERS] = {MCP2515_TXB0CTRL, MCP2515_TXB1CTRL, MCP2515_TXB2CTRL};

	if (!dev || txbn >= N_TXBUFFERS) {
		return -1;
	}

	mcp2515_spi_bit_modify(dev, ctrl_regs[txbn], TXB_TXREQ, 0);
	dev->tx_busy_mask &= ~(1 << txbn);

	return 0;
}

int mcp2515_send(struct mcp2515_dev *dev, struct can_message *msg)
{
    uint8_t sidh, sidl, eid8, eid0;
    uint8_t tx_buf[16]; // 增大缓冲区大小
    int i;
    
    if (!dev || !msg) {
        return -1;
    }
    
    if (msg->dlc > 8) {
        msg->dlc = 8;
    }
    
    /* 准备发送数据 */
    if (msg->extended) {
        sidh = (msg->id >> 21) & 0xFF;
        sidl = ((msg->id >> 13) & 0xE0) | ((msg->id >> 16) & 0x03) | 0x08;
        eid8 = (msg->id >> 8) & 0xFF;
        eid0 = msg->id & 0xFF;
    } else {
        sidh = (msg->id >> 3) & 0xFF;
        sidl = (msg->id << 5) & 0xE0;
        eid8 = 0;
        eid0 = 0;
    }
    
    tx_buf[0] = MCP2515_CMD_WRITE;
    tx_buf[1] = 0x31; /* TXB0SIDH */
    tx_buf[2] = sidh;
    tx_buf[3] = sidl;
    tx_buf[4] = eid8;
    tx_buf[5] = eid0;
    tx_buf[6] = msg->dlc;
    
    for (i = 0; i < msg->dlc; i++) {
        tx_buf[7 + i] = msg->data[i];
    }
    
    /* 请求发送指令 */
    u8 tx_rts[] = {MCP2515_CMD_RTS | 0x01};

    struct spi_message msg_both[] = {
        {
            .tx_buf = tx_buf,
            .rx_buf = NULL,
            .tlen = 7 + msg->dlc,
            .rlen = 0,
            .cs_change = 1, // 强制 CS 翻转，区隔 Data 和 RTS
        },
        {
            .tx_buf = tx_rts,
            .rx_buf = NULL,
            .tlen = 1,
            .rlen = 0,
            .cs_change = 0,
        }
    };
    
    // 一次系统调用合并处理，消除 1.5ms 的 OS 调度延迟
    spi_transfer(dev->spi, msg_both, 2);
    
    return 0;
}

int mcp2515_send_nb(struct mcp2515_dev *dev, uint8_t txbn, struct can_message *msg)
{
	uint8_t sidh_regs[N_TXBUFFERS] = {MCP2515_TXB0SIDH, MCP2515_TXB1SIDH, MCP2515_TXB2SIDH};
	uint8_t ctrl_regs[N_TXBUFFERS] = {MCP2515_TXB0CTRL, MCP2515_TXB1CTRL, MCP2515_TXB2CTRL};
	uint8_t sidh, sidl, eid8, eid0;
	uint8_t data[13];
	uint8_t tx_buf[16];
	int i;

	if (!dev || !msg || txbn >= N_TXBUFFERS) {
		return MCP2515_FAILTX;
	}

	if (msg->dlc > CAN_MAX_DLEN) {
		return MCP2515_FAILTX;
	}

	if (mcp2515_spi_read(dev, ctrl_regs[txbn]) & TXB_TXREQ) {
		return MCP2515_ALLTXBUSY;
	}

	if (msg->extended) {
		sidh = (msg->id >> 21) & 0xFF;
		sidl = ((msg->id >> 13) & 0xE0) | ((msg->id >> 16) & 0x03) | 0x08;
		eid8 = (msg->id >> 8) & 0xFF;
		eid0 = msg->id & 0xFF;
	} else {
		sidh = (msg->id >> 3) & 0xFF;
		sidl = (msg->id << 5) & 0xE0;
		eid8 = 0;
		eid0 = 0;
	}

	/* 组装发送数据: SIDH, SIDL, EID8, EID0, DLC, DATA */
    data[0] = sidh;
	data[1] = sidl;
	data[2] = eid8;
	data[3] = eid0;
	data[4] = msg->dlc;
	for (i = 0; i < msg->dlc; i++) {
		data[5 + i] = msg->data[i];
	}

	/* 一次性写入 SIDH ~ DATA */
	tx_buf[0] = MCP2515_CMD_WRITE;
	tx_buf[1] = sidh_regs[txbn];
	for (i = 0; i < 5 + msg->dlc; i++) {
		tx_buf[2 + i] = data[i];
	}

	/* 请求发送指令 */
	u8 tx_rts[] = {MCP2515_CMD_RTS | ((1 << txbn) & 0x07)};

	struct spi_message msg_both[] = {
		{
			.tx_buf = tx_buf,
			.rx_buf = NULL,
			.tlen = 2 + 5 + msg->dlc,
			.rlen = 0,
			.cs_change = 1, // 强制 CS 翻转，区隔 Data 和 RTS
		},
		{
			.tx_buf = tx_rts,
			.rx_buf = NULL,
			.tlen = 1,
			.rlen = 0,
			.cs_change = 0,
		},
	};

	// 一次系统调用合并处理，消除 OS 调度延迟
	spi_transfer(dev->spi, msg_both, 2);

	dev->tx_busy_mask |= (1 << txbn);

	return MCP2515_OK;
}

int mcp2515_recv(struct mcp2515_dev *dev, struct can_message *msg)
{
    uint8_t sidh, sidl, eid8, eid0;
    uint8_t rx_buf[16]; // 增大缓冲区大小
    int i;
    
    if (!dev || !msg) {
        return -1;
    }
    
    /* 检查接收缓冲区状态 */
    u8 tx_status[] = {MCP2515_CMD_RX_STATUS};
    u8 rx_status[2];
    struct spi_message msg_status[] = {
        {
            .tx_buf = tx_status,
            .rx_buf = rx_status,
            .tlen = 1,
            .rlen = 2,
            .cs_change = 0,
        },
    };
    
    spi_transfer(dev->spi, msg_status, 1);
    
    if (!(rx_status[1] & 0xC0)) {
        return -2; /* 无数据 */
    }
    
    /* 读取接收缓冲区 */
    u8 tx_read[] = {MCP2515_CMD_READ, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    struct spi_message msg_read[] = {
        {
            .tx_buf = tx_read,
            .rx_buf = rx_buf,
            .tlen = 16,
            .rlen = 16,
            .cs_change = 0,
        },
    };
    
    spi_transfer(dev->spi, msg_read, 1);
    
    /* 解析数据 */
    sidh = rx_buf[2];
    sidl = rx_buf[3];
    eid8 = rx_buf[4];
    eid0 = rx_buf[5];
    
    if (sidl & 0x08) {
        /* 扩展帧 */
        msg->extended = 1;
        msg->id = ((uint32_t)sidh << 21) | ((uint32_t)(sidl & 0xE0) << 13) | 
                  ((uint32_t)(sidl & 0x03) << 16) | ((uint32_t)eid8 << 8) | eid0;
    } else {
        /* 标准帧 */
        msg->extended = 0;
        msg->id = ((uint32_t)sidh << 3) | ((uint32_t)(sidl & 0xE0) >> 5);
    }
    
    msg->dlc = rx_buf[6] & 0x0F;
    for (i = 0; i < msg->dlc; i++) {
        msg->data[i] = rx_buf[7 + i];
    }
    
    /* 清除中断标志 */
    mcp2515_spi_bit_modify(dev, MCP2515_REG_CANINTF, 0x01, 0x00);
    
    return 0;
}

int mcp2515_set_mode(struct mcp2515_dev *dev, uint8_t mode)
{
    if (!dev) {
        return -1;
    }
    
    mcp2515_spi_bit_modify(dev, MCP2515_REG_CANCTRL, 0xE0, mode);
    
    /* 等待模式切换完成 */
    while ((mcp2515_spi_read(dev, MCP2515_REG_CANSTAT) & 0xE0) != mode) {
        msleep(1);
    }
    
    return 0;
}

void mcp2515_deinit(struct mcp2515_dev *dev)
{
    if (dev) {
        if (dev->spi) {
            spi_unregister(dev->spi);
        }
        free(dev);
    }
}
