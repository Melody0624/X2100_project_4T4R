#ifndef _SOC_AIC_H_
#define _SOC_AIC_H_

#include <driver/pcm.h>

#define AIC_NUMS 5
#define MIN_PERIODS 3
#define DEFAULT_BUFFER_PERIO_MS 10
#define MIN_BUFFER_TIME_MS 3
#define DEFAULT_BUFFER_TIME_MS 100

struct aic_params {
    unsigned char id;
    unsigned char channels;
    unsigned char data_bits;
    unsigned char is_master;
    unsigned char is_capture;
    unsigned char sysclk_out;
    unsigned char is_split_clk;

    unsigned int sys_freq;
    unsigned int sample_rate;
    unsigned int buffer_time_ms;
    unsigned int period_time_ms;

    struct clk *sysclk;
    struct clk *gate_clk;
};

#endif /* _SOC_AIC_H_ */