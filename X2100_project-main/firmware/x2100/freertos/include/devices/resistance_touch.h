#ifndef _RTS_TOUCH_H_
#define _RTS_TOUCH_H_

#define RTS_DEBUG_ON                    false

#define RTS_MAX_12BIT                   ((1 << 12) - 1)
#define RTS_ABS(x)                      ((x) > 0 ? (x) : -(x))
#define RTS_MIN(x,y)                    ((x) < (y) ? (x) : (y))
#define RTS_ERROR(fmt,arg...)           printf("<<-RTS-ERROR->> "fmt"\n",##arg)
#define RTS_INFO(fmt,arg...)            printf("<<-RTS-INFO->> "fmt"\n",##arg)
#define RTS_DEBUG(fmt,arg...)           do{\
                                         if(RTS_DEBUG_ON)\
                                         printf("<<-RTS-DEBUG->> [%d] "fmt"\n",__LINE__, ##arg);\
                                        }while(0)

enum {
    X_AXIS = 0,
    Y_AXIS,
    Z1_AXIS,
    Z2_AXIS,
    AXIS_CNT,
};

struct rts_adc_params {
    int coords_max;
    int adc_max;
    int adc_min;
    int power;
    int minus;
    int adc_channel;
};

struct rts_filter_params {
    int valid_range;
    int z1_threshold;
    int press_threshold;
    int dither_delay_ms;
};

struct rts_params {
    char *dev_name;
    int delay_ms;
    struct rts_adc_params x;
    struct rts_adc_params y;
    int x_coords_flip;
    int y_coords_flip;
    int x_y_coords_exchange;
};

struct adc_rts_dev;
struct adc_rts_dev *adc_resistance_touch_init(struct rts_params *params);
void adc_resistance_touch_deinit(struct adc_rts_dev *dev);
void rts_modify_filter_params(struct adc_rts_dev *dev, struct rts_filter_params params);

#endif
