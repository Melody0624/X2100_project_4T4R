#ifndef _SPI_H_
#define _SPI_H_

#include <list.h>
#include <os.h>
#include <wake_lock.h>
#include <soc/spi.h>

enum spi_data_endian {
    /* 传输数据时，最高有效位先传输 */
    Spi_endian_msb_first = 0,
    /* 传输数据时，最低有效位先传输 */
    Spi_endian_lsb_first = 2,
};

enum spi_cs_valid_level {
    Spi_valid_low,  /* 表示传输过程的CS有效电平为低，即当CS被拉低时启动传输 */
    Spi_valid_high, /* 表示传输过程的CS有效电平为高，即当CS被拉高时启动传输 */
};

struct spi_config_data;
typedef void (*dma_async_cb)(struct spi_config_data *config);

struct spi_config_data {
    unsigned int id;        /* SPI 控制器 ID */
    unsigned int cs_pin;    /* 指定作为 cs 脚的 GPIO */
    char *name;             /* name 仅作为一个标识 */
    unsigned int clk_rate;  /* 时钟频率(默认为 1*1000*1000) */

    enum spi_cs_valid_level cs_valid_level; /* 有效电平 */

    /* 数据传输的大小端模式选择,具体可选类型为 spi_data_endian 所定义的类型 */
    enum spi_data_endian tx_endian;
    enum spi_data_endian rx_endian;

    /* bits_per_word 代表数据的位宽.
        例如:bits_per_word = 32 时,SPI传输过程中的最小数据单位为32bit. */
    unsigned int bits_per_word;

    /**
     * 极性 spi_pol :
     * 当spi_pol=0，在时钟空闲即无数据传输时,clk电平为低电平
     * 当spi_pol=1，在时钟空闲即无数据传输时,clk电平为高电平
     * 相位 spi_pha :
     * 当spi_pha=0，表示在第一个跳变沿开始传输数据，下一个跳变沿完成传输
     * 当spi_pha=1，表示在第二个跳变沿开始传输数据，下一个跳变沿完成传输
     */
    unsigned int spi_pol;
    unsigned int spi_pha;

    unsigned int loop_mode; /* 循环模式，可用于测试 */

    unsigned int dma_unit; /* dma传输单位，内部使用 */
    dma_async_cb dma_cb;
};

struct spi_bus_ops;

struct spi_device {
    struct spi_config_data config;
    int spi_bus_id;
    struct wake_lock w_lock;
    struct spi_bus *spi_bus;
    struct list_head link;
};

struct spi_message {
    /* NOTE:在君正平台下,dma传输需要保证发送和接收的个数相等,其他传输方式不作要求.
     * tlen 发送数据的个数.发送数据的大小＝发送数据的个数(tlen)＊数据对应的字节长度
     * rlen 接收数据的个数.接收数据的大小＝接收数据的个数(rlen)＊数据对应的字节长度
     *
     * bits_per_word 范围是 0～32；
     * 建议选择 8、16、32 作为 bits_per_word 的值，其余的可参考对应的驱动程序.
     * 当 bits_per_word = 8,对应的数据字节长度为1.
     * 当 bits_per_word = 16,对应的数据字节长度为2.
     * 当 bits_per_word = 32,对应的数据字节长度为4.
     */
    int tlen;
    int rlen;
    const void *tx_buf; /* 发送缓冲区 */
    void *rx_buf;       /* 接收缓冲区 */
    int use_dma;        /* 0:不使用dma方式传输； 1：使用dma方式传输 
                            扩展： 为了提高dma总线的使用效率，设置dma传输unit
                                1 -> 1字节
                                2 -> 16字节
                                3 -> 32字节
                                4 -> 64字节
                            限制：（数据的个数(len)＊数据对应的字节）长度必须可以被dma_unit整除
                                    buf的地址也必须dma_unit对齐！！！
                            注意：以第一个use_dma的dma_unit为准，后面的use_dma字节数自动使用第一个设定
                        */
    int cs_change;      /* 传输结束时,改变cs的状态 */
};

/**
 * SPI 初始化， 无返回值
 */
void spi_init(void);

/**
 * SPI 设备注册
 * config: config 结构体
 */
struct spi_device *spi_register(struct spi_config_data *config);

/**
 * 释放 SPI 资源
 * spi: 从设备句柄
 */
void spi_unregister(struct spi_device *spi);

/**
 * SPI 传输
 * spi: 从设备句柄
 * msg: msg 结构体,包含传输过程的相关信息
 * count: 一个msg中包含的transfer的数目
 * 具体用法可参考 spi_example.c 或 spi_dma_example.c
 */
void spi_transfer(struct spi_device *spi, struct spi_message *msg, int count);

/**
 * SPI 使能中断内dma传输
 * spi: 从设备句柄
 * msg: msg 结构体,包含传输过程的相关信息
 * dma_cb: 传输完成调用回调
 */
void spi_dma_start(struct spi_device *spi, struct spi_message *msg, dma_async_cb dma_cb);

/**
 * SPI 失能中断内dma传输
 * spi: 从设备句柄
 */
void spi_dma_stop(struct spi_device *spi);

/**
 * SPI 中断内dma传输
 * spi: 从设备句柄
 * msg: msg 结构体,包含传输过程的相关信息
 */
void spi_dma_async_transfer(struct spi_device *spi, struct spi_message *msg);

#endif