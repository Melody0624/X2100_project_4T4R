#ifndef _PWM_H_
#define _PWM_H_

#include <soc/pwm.h>

enum pwm_shutdown_mode {
    /**
     * pwm停止输出时,尽量保证pwm的信号结尾是一个完整的周期
     */
    PWM_graceful_shutdown,
    /**
     * pwm停止输出时,立刻将pwm设置成空闲时电平
     */
    PWM_abrupt_shutdown,
};

enum pwm_idle_level {
    /**
     * pwm 空闲时电平为低
     */
    PWM_idle_low,
    /**
     * pwm 空闲时电平为高
     */
    PWM_idle_high,
};

enum pwm_accuracy_priority {
    /**
     * 优先满足pwm的目标频率的精度,级数可能不准确
     */
    PWM_accuracy_freq_first,
    /**
     * 优先满足pwm的级数设置,pwm频率可能不准确
     */
    PWM_accuracy_levels_first,
};

struct pwm_config_data {
    enum pwm_shutdown_mode shutdown_mode;
    enum pwm_idle_level idle_level;
    enum pwm_accuracy_priority accuracy_priority;
    char *clk_id;   /* 指定时钟源ID，可参考对应SOC中的clk.h,比如 "rtc" "ext1" */
    unsigned long freq;
    unsigned long levels;
};

void pwm_init(void);

/**
 * 申请 PWM 资源
 * gpio: 指定 GPIO 号
 * name: PWM 名，根据场景命名
 * 返回值: 对应的 PWM 通道号
 */
int pwm_request(int gpio, const char *name);

/**
 * 设置 PWM 调制后的频率和周期级数, 无返回值
 * ch: 指定 PWM 通道号
 * config: pwm 的配置数据
 * 返回值: 0表示成功, 其他表示失败
 */
int pwm_config(int ch, struct pwm_config_data *config);

/**
 * 释放 PWM 资源, 无返回值
 * ch: 指定 PWM 通道号
 */
void pwm_release(int ch);

/**
 * 设置 PWM 调制的级数
 * ch: 指定 PWM 通道号
 * level: pwm 调制的级数,即一个周期内非空闲电平的长度
 *
 * NOTE: 当 level >  0 时，PWM 调制波形会输出
 *       当 level == 0 时, PWM 调制波形会停止输出
 *
 */
void pwm_set_level(int ch, unsigned long level);

/**
 * 获取 PWM 调制后频率
 * ch: 指定 PWM 通道号
 *
 * 返回值: PWM 调制后频率
 *
 */
unsigned long pwm_get_freq(int ch);


/* ---- dma mode ---- */

enum pwm_dma_start_level {
    PWM_start_low,   /* pwm dma模式的起始电平为低 */
    PWM_start_high,  /* pwm dma模式的起始电平为高 */
};

/*
    请确保dma数据的高低电平不能有零计数，
    如果不能确保，手动把宏定义打开。
 */
// #define PWM_CHECK_DMA_DATA

/*
    注意: high和low不能为零
        时间单位由pwm2_dma_init返回
 */
struct pwm_data {
    /* 低电平个数 */
    unsigned low:16;
    /* 高电平个数 */
    unsigned high:16;
};

struct pwm_dma_config {
    enum pwm_idle_level idle_level;     // 空闲电平
    enum pwm_dma_start_level start_level;   // 起始电平
    void (*dma_complete_cb)(void *data);    // dma 回调函数，该参数为 NULL 时使用默认回调，为避免指针异常必须进行初始化
};

struct pwm_dma_data {
    struct pwm_data *data;
    unsigned int data_count;
    unsigned int dma_loop;
};

/* 初始化pwm的dma模式
    返回值： 失败返回-1, 成功返回dma模式频率*/
int pwm_dma_init(int id, struct pwm_dma_config *dma_config);

/*
    使用dma模式连续更新pwm的频率
    普通dma模式: 函数会阻塞到dma数据全部转换成对应pwm输出
    循环dma模式：函数不会阻塞，需要调用pwm2_dma_disable_loop停止dma
 */
int pwm_dma_update(int id, struct pwm_dma_data *dma_data);

/* 停止dma的循环模式 */
int pwm_dma_disable_loop(int id);

/* 获取 PWM DMA 当前源地址(循环模式可用于计算读指针) */
unsigned long pwm_dma_read_src_addr(int id);

/* 用于多通道同时开启时的失能 */
void pwm_set_not_really_disable(int id, int enable);

/* 用于多通道同时开启时的使能 */
void pwm_set_not_really_enable(int id, int enable);

/*
 * 多通道同时开启
 * channels为需要启动的通道，每位bit对应通道号
 */
void pwm_enable_channels(unsigned int channels);

/*
 * 多通道同时关闭
 * channels为需要启动的通道，每位bit对应通道号
 */
void pwm_disable_channels(unsigned int channels);

/**
 * @brief pwm dma设置起始时钟
 * @param id pwm通道
 * @param data 填入的数据地址
 * @return 成功与否
 *          > 0 设置成功
 *          < 0 设置失败
 */
int pwm_dma_set_start(int id, struct pwm_data *data);

/**
 * @brief 设置pwm dma 分频
 * @param id pwm通道
 * @param div 分频系数
 * @return 成功与否
 *          > 0 设置成功
 *          < 0 设置失败
 */
int pwm_dma_set_div(int id, int div);

#endif
