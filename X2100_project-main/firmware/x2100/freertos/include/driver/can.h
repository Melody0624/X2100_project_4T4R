#ifndef _CAN_H_
#define _CAN_H_

#include <list.h>
#include <wake_lock.h>

enum can_bus_rate {
    CAN_BUS_RATE_5000 = 5000,
    CAN_BUS_RATE_10000 = 10000,
    CAN_BUS_RATE_25000 = 25000,
    CAN_BUS_RATE_50000 = 50000,
    CAN_BUS_RATE_100000 = 100000,
    CAN_BUS_RATE_125000 = 125000,
    CAN_BUS_RATE_250000 = 250000,
    CAN_BUS_RATE_500000 = 500000,
    CAN_BUS_RATE_1000000 = 1000000,
};

/* 若afid设置为该值, 可接收所有帧 */
#define CAN_ALL_ACCEPT_AFID 0xffffffff

struct can_filter_cfg {
    int is_enable;
    int afid;
};

struct can_config {
    int bus_id;
    enum can_bus_rate bus_rate;
    unsigned char rx_dma_mode;
    int rx_bufsize;
};

/* 传输标识 */
#define CAN_EXTENDED    0x01
#define CAN_REMOTE      0x10

struct can_msg {
    int frm_id;
    int len;
    unsigned char buf[8];
    /**
     * flags 传输标识, 有以下选择:
     * CAN_EXTENDED: 设置发送帧格式为 扩展帧
     * CAN_REMOTE: 向总线上其他节点请求发送具有同一frm_id的数据帧
     */
    int flags;
};

/**
 * CAN 初始化, 无返回值
 */
void can_init(void);

/**
 * CAN 设备使能
 * cfg: 设备配置信息结构体, 包含 CAN 设备ID,通讯频率,rx dma 模式及rx使用的缓冲区大小
 */
void can_start(struct can_config *cfg);

/**
 * 释放 CAN 资源
 * cfg: 设备配置信息结构体
 */
void can_stop(struct can_config *cfg);

/**
 * CAN 传输, 失败返回负数, 成功返回0
 * cfg: 设备配置信息结构体
 * msg: msg 结构体,包含传输过程的相关信息
 * 具体用法可参考 can_example.c
 */
int can_send_frame(struct can_config *cfg, struct can_msg *msg);

/**
 * CAN 接收, 失败返回负数, 成功返回0
 * cfg: 设备配置信息结构体
 * msg: msg 结构体,包含传输过程的相关信息
 * 具体用法可参考 can_example.c
 */
int can_receive_frame(struct can_config *cfg, struct can_msg *msg);

/**
 * 配置 CAN 接收仲裁, 共 4 个 接收仲裁过滤器, 互相独立
 * cfg: 设备配置信息结构体
 * af_cfg: 接收仲裁过滤器配置结构体
 * 具体用法可参考 can_example.c
 */
void can_set_acceptance_filter(struct can_config *cfg, struct can_filter_cfg *af_cfg);

#endif /* _CAN_H_ */