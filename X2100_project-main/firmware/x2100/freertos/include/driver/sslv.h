#ifndef _SSLV_H_
#define _SSLV_H_

#include <list.h>
#include <os.h>
#include <wake_lock.h>

enum sslv_status_type {
    SSLV_IDLE,
    SSLV_BUSY,
};

struct sslv_config_data {
    unsigned int id;        /* SSLV 控制器 ID */
    char *name;             /* name 仅作为一个标识 */

    /* bits_per_word 代表数据的位宽.
        例如:bits_per_word = 32 时,SSLV传输过程中的最小数据单位为32bit. */
    unsigned int bits_per_word;

    /**
     * 极性 sslv_pol :
     * 当sslv_pol=0，在时钟空闲即无数据传输时,clk电平为低电平
     * 当sslv_pol=1，在时钟空闲即无数据传输时,clk电平为高电平
     * 相位 sslv_pha :
     * 当sslv_pha=0，表示在第一个跳变沿开始传输数据，下一个跳变沿完成传输
     * 当sslv_pha=1，表示在第二个跳变沿开始传输数据，下一个跳变沿完成传输
     */
    unsigned int sslv_pol;
    unsigned int sslv_pha;

    unsigned int loop_mode; /* 循环模式，可用于测试 */
};

struct sslv_device {
    struct sslv_config_data config;
    int sslv_id;
    int status;
    int rx_flags;
    int cb_flags;
    struct mutex lock;
    struct wake_lock w_lock;
};

/**
 * SSLV 初始化, 无返回值
 * 使能时钟, 配置一些基本的控制寄存器, 将格式设置成标准格式
 */
void sslv_init(void);

/**
 * SSLV 设备注册
 * config: config 结构体
 * 将用户设置的pol/pha/bits_per_word等参数写入到寄存器内
 */
struct sslv_device *sslv_register(struct sslv_config_data *config);

/**
 * 释放 SSLV 资源
 * dev: 从设备句柄
 * 关时钟, 失能SSLV
 */
void sslv_unregister(struct sslv_device *dev);

/**
 * 获取 SSLV 接收FIFO个数
 * dev: 从设备句柄
 * 读取接收FIFO个数寄存器并将结果返回
 */
unsigned int sslv_get_rx_fifo_num(struct sslv_device *dev);

/**
 * SSLV 写FIFO数据
 * dev: 从设备句柄
 * data: 需要发送出去的数据
 * 将data数据写入数据寄存器
 */
void sslv_write_tx_fifo(struct sslv_device *dev, unsigned int data);

/**
 * SSLV 读FIFO数据
 * dev: 从设备句柄
 * 将数据寄存器内接收到的数据读取出来
 */
unsigned int sslv_read_rx_fifo(struct sslv_device *dev);

/**
 * SSLV 发送
 * dev: 从设备句柄
 * tx_buf: 发送数据
 * tx_len: 发送长度
 * 将tx_buf内的tx_len长度的数据依次写入到数据寄存器然后通过FIFO发送出去, FIFO写满时等待直至数据全部写入
 */
void sslv_transmit(struct sslv_device *dev, unsigned char *tx_buf, int tx_len);

/**
 * SSLV 接收
 * dev: 从设备句柄
 * rx_buf: 接收数据
 * rx_len: 接收长度
 * 读取数据寄存器内rx_len长度的数据并写入到rx_buf内, FIFO内数据不够时等待直至数据接收完全
 */
void sslv_receive(struct sslv_device *dev, unsigned char *rx_buf, int rx_len);

/**
 * SSLV 回调接收
 * dev: 从设备句柄
 * rx_threshold: 设置接收FIFO阈值
 * cb: 回调函数
 * 在开启中断前设置接收FIFO阈值为rx_threshold, 若接收FIFO个数大于rx_threshold就进入中断,
 * 在中断内跳转至cb回调函数接收数据, 直至应用调用sslv_stop_cb_receive关闭中断后停止接收数据
 */
void sslv_start_cb_receive(struct sslv_device *dev, unsigned char rx_threshold,
                    void (*cb)(struct sslv_device *dev));

/**
 * SSLV 停止回调接收
 * dev: 从设备句柄
 * 调用后关闭接收中断从而停止接收数据
 */
void sslv_stop_cb_receive(struct sslv_device *dev);

#endif