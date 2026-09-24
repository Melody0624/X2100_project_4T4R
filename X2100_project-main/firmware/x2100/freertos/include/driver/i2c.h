#ifndef _I2C_H_
#define _I2C_H_

#include <list.h>
#include <wake_lock.h>
#include <soc/i2c.h>

enum i2c_addr_type {
    I2C_ADDR_BIT_7,
    I2C_ADDR_BIT_10,
};

struct i2c_device {
    int bus_num;
    char *name;
    unsigned short addr;
    enum i2c_addr_type addr_bit;
    struct wake_lock w_lock;
    struct i2c_bus *i2c_bus;
    struct list_head link;
};

struct i2c_bus_ops;

#define I2C_SMBUS_BLOCK_MAX	32	/* As specified in SMBus standard */

/* 传输标识 */
#define I2C_M_WR        0x0
#define I2C_M_TEN       0x0010
#define I2C_M_RD        0x0001
#define I2C_M_NOSTART   0x4000
#define I2C_M_REV_DIR_ADDR	0x2000	/* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_IGNORE_NAK	0x1000	/* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_NO_RD_ACK		0x0800	/* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_RECV_LEN		0x0400	/* length will be first received byte */

struct i2c_msg {
    int len;        /* 数据传输的个数 */
    void *buf;      /* 数据缓冲区 */
    /**
     * flags 传输标识.有以下选择：
     * I2C_M_TEN : 选择 10bit 地址
     * I2C_M_RD  : 发送读操作命令
     * I2C_M_NOSTART ： 传输过程不产生开始信号
     * NOTE： 以上传输标识可组合使用,具体用法可参考i2c_example.c
     */
    int flags;
};

/**
 * I2C 初始化， 无返回值
 */
void i2c_init(void);

/**
 * I2C 设备注册
 * id: I2C 总线号
 * addr: 从设备地址
 * addr_bit: 从设备地址位宽
 * name: i2c设备名(仅作为标识)
 */
struct i2c_device *i2c_register(int i2c_bus_num, unsigned short addr, enum i2c_addr_type addr_bit, char *name);

/**
 * 释放 I2C 资源
 * i2c: 从设备句柄
 */
void i2c_unregister(struct i2c_device *i2c);

/**
 * I2C 传输, 返回 msg 个数
 * i2c: 从设备句柄
 * msg: msg 结构体,包含传输过程的相关信息
 * count: 一个msg中包含的transfer的数目
 * 具体用法可参考 i2c_example.c
 */
int i2c_transfer(struct i2c_device *i2c, struct i2c_msg *msg, int count);

int i2c_detect_device(struct i2c_device *i2c);

#endif