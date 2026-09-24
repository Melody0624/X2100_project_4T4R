#include <stdio.h>
#include <common.h>
#include <os.h>
#include <driver/cache.h>
#include <soc/tpc.h>

/*每行单个数据通道输出多少字节*/
#define SHIFT_LINES_BYTES 48
/*数据通道*/
#define SHIFT_CHANNELS     1

#define HEAT_CHANNELS      2

#define MOTOR_CHANNLES     4
#define MOTOR_REPEAT_STEP  4

#define MOTOR_ALIGN_STEP   4

#define MOTOR_ID  0

#define TPC_US_CLKCNT(us)      ((uint64_t)(us)*(DEFAULT_TPC_CLK_RATE / (1*1000*1000)))

#define TPC_HEAT_ENABLE                 (1 << 24)
#define TPC_HEAT_DISABLE                (0 << 24)
#define TPC_HEAT_TRIGGER_BY_MOTOR       (0 << 24)
#define TPC_HEAT_TRIGGER_BY_LATCH       (1 << 24)



struct tpc_shift_cfg shift_cfg = {
    .big_end = 0,
    .channels = SHIFT_CHANNELS,
    .data_invert = 0,
    .spi_pha = 0,
    .spi_pol = 0,
    .line_bytes = SHIFT_LINES_BYTES,
    .out_clk_rate = 8*1000*1000,
};

struct tpc_heat_cfg heat_cfg = {
    .channels = HEAT_CHANNELS,
    .idle_level = 0,
    .max_heat_clkcnt = TPC_US_CLKCNT(40*1000),
    .wait_clkcnt_out = 1,
};

struct tpc_lat_cfg lat_cfg = {
    .active_clkcnt = TPC_US_CLKCNT(1),
    .idle_level = 1,
    .wait_clkcnt_out = TPC_US_CLKCNT(1),
    .wait_clkcnt_out_shift = TPC_US_CLKCNT(1),
};

struct tpc_motor_cfg motor_cfg_0 = {
    .channels = 4,
    .idle_level = 0b0000,

    .start_step_io = 0b0000,
    .start_step_clkcnt = TPC_US_CLKCNT(15*1000),

    .stop_step_io = 0b0000,
    .stop_step_clkcnt = TPC_US_CLKCNT(15*1000),

    .repeat_step = MOTOR_REPEAT_STEP,
    .repeat_step_io = {
        [0] = 0b1100,
        [1] = 0b0011,
        [2] = 0b0110,
        [3] = 0b1001,
    },

    .max_step_clkcnt = TPC_US_CLKCNT(40*1000),
    .align_step = MOTOR_ALIGN_STEP,
};


struct heat_data {
    unsigned int nxt_trigger_type;
    unsigned int channels[HEAT_CHANNELS];
};



static int motor_speed_list[] = {
    TPC_US_CLKCNT(15 * 1000), TPC_US_CLKCNT(12 * 1000),
    TPC_US_CLKCNT(10 * 1000), TPC_US_CLKCNT(9 * 1000),
    TPC_US_CLKCNT(8 * 1000), TPC_US_CLKCNT(7 * 1000),
    TPC_US_CLKCNT(6 * 1000), TPC_US_CLKCNT(5 * 1000),
    TPC_US_CLKCNT(4 * 1000),
};

static int motor_speed_up(int speed_now, unsigned int *motor_data)
{
    int i;
    int speed = speed_now;
    int fast_speed = ARRAY_SIZE(motor_speed_list) - 1;

    for (i = 0; i < MOTOR_REPEAT_STEP; i++) {
        speed++;

        if (speed < 0)
            speed = 0;

        if (speed > fast_speed)
            speed = fast_speed;

        motor_data[i] = motor_speed_list[speed];
    }

    return speed;
}

static int motor_speed_down(int speed_now, unsigned int *motor_data)
{

    int i;
    int speed = speed_now;
    int fast_speed = ARRAY_SIZE(motor_speed_list) - 1;

    for (i = 0; i < MOTOR_REPEAT_STEP; i++) {
        speed--;
        if (speed < 0)
            speed = 0;

        if (speed > fast_speed)
            speed = fast_speed;

        motor_data[i] = motor_speed_list[speed];
    }

    return speed;
}

/*绘制虚线*/
void draw_shift_data(void *shift_data, int lines)
{
    int i = 0;
    char *data = shift_data;
    for(i = 0; i < SHIFT_LINES_BYTES * lines; i++) {
        data[i] = 0x55;
    }
}

static void set_heat_data(struct heat_data *heat_data, int lines, int heat_time, int sync_with_motor, int motor_one_lines_timeus)
{
    int heat_trigger_lat_time = 0;

    /*当自动模式不和电机同步时，可以通过设置加热完毕后 等待多长时间触发才触发锁存。从而减慢整个打印头的流程， 手动与电机对齐*/
    if (!sync_with_motor)
        heat_trigger_lat_time =  TPC_US_CLKCNT(motor_one_lines_timeus - heat_time);

    /*准备加热数据，这里 以分两段加热、每段加热只有一个通道生效为例*/
    int i;
    for (i = 0; i < lines; i++) {
        heat_data[i].nxt_trigger_type =  heat_trigger_lat_time | !sync_with_motor << 24;
        heat_data[i].channels[0] = 1 << 24 | TPC_US_CLKCNT(heat_time);
        heat_data[i].channels[1] = 1 << 24 | TPC_US_CLKCNT(heat_time);
    }
}

static  void set_motor_time(unsigned int *motor_data, int lines, int step_timeus)
{
    int i;
    int j;
    for (i = 0; i < lines; i++) {
        for(j = 0; j < motor_cfg_0.repeat_step; j++) {
            motor_data[j + i * motor_cfg_0.repeat_step] = TPC_US_CLKCNT(step_timeus);
        }
    }
}

static int write_sync_mode_data(unsigned char *shift_data, void *heat_data, void *motor_data,
                                          int shift_size, int heat_size, int motor_size)
{

    int ret;
    int motor_w = motor_size;
    int heat_w = heat_size;
    int shift_w = shift_size;


   while(1) {
        ret = tpc_motor_write_data(MOTOR_ID, motor_data, motor_w);
        motor_data += ret;
        motor_w -= ret;

        ret = tpc_heat_write_data((unsigned int *)heat_data, heat_w);
        heat_data += ret;
        heat_w -= ret;


        ret = tpc_shift_write_data(shift_data, shift_w);
        shift_data +=  ret;
        shift_w -= ret;

        if (!motor_w && !heat_w && !shift_w)
            break;
    }

    return 0;
}


void tpc_sync_mode_with_motor_example(void)
{
    printf("start tpc sync mode with motor\n");

    int shift_size;
    unsigned char *shift_data;

    int motor_size;
    unsigned int *motor_data;

    int heat_size;
    struct heat_data *heat_data;

    motor_size = motor_cfg_0.repeat_step * 32 * sizeof(int);
    motor_data = malloc(motor_size);

    shift_size = SHIFT_LINES_BYTES * 32;
    shift_data = malloc(shift_size);

    heat_size =  sizeof(struct heat_data) * 32;
    heat_data = malloc(heat_size);

    /*先提前准备好32行数据*/
    /*绘制移位数据*/
    draw_shift_data(shift_data, 32);

    /*设置电机每步时长*/
    int step_timeus = 10 * 1000;
    set_motor_time(motor_data, 32, step_timeus);

    /*设置加热时长*/
    set_heat_data(heat_data, 32,  10 * 1000, 1, 0);


    int ret;
    ret = tpc_sync_mode_init(&shift_cfg, &heat_cfg, &lat_cfg, &motor_cfg_0, MOTOR_ID);
    if (ret < 0) {
        free(shift_data);
        free(motor_data);
        free(heat_data);
        return;
    }

    /*  第一次写dma，各个设备的数据必须大于等于 内部dma， 内部dma 默认大小为 32 * unit_size;
        shift_unit_size = shift_cfg.data_len * shift_cfg.channels * sizeof(char);
        heat_unit_size = (heat_cfg.channles + 1) * sizeof(int);
        motor_unit_size = motor_cfg.repeat_step * sizeof(int);
    */
   /*这里先一次性写 32 行数据*/
    write_sync_mode_data(shift_data, heat_data, motor_data, shift_size, heat_size, motor_size);
    tpc_sync_mode_start();

    free(shift_data);
    free(motor_data);
    free(heat_data);

    // /*---------------------------------------接下来一行一行写------------------------------------------------*/

    shift_size = SHIFT_LINES_BYTES;
    shift_data = malloc(shift_size);

    heat_size = sizeof(struct heat_data);
    heat_data = malloc(heat_size);

    motor_size = MOTOR_REPEAT_STEP * sizeof(int);
    motor_data = malloc(motor_size);

    int heat_clkcnt = TPC_US_CLKCNT(10 * 1000);

    int speed_up = 1;
    int speed_now = 0;

    int cnt = 32;
    /*后续的数据可以一行一行写，也可以几行几行写*/
    while(cnt--) {
        draw_shift_data(shift_data, 1);

        /*理论上 越往后加热时间越短，电机运行越快。一行的加热时长不可以超过电机走一行对应的时长*/
        heat_data[0].nxt_trigger_type = TPC_HEAT_TRIGGER_BY_MOTOR;
        heat_data[0].channels[0] = TPC_HEAT_ENABLE | heat_clkcnt;
        heat_data[0].channels[1] = TPC_HEAT_ENABLE | heat_clkcnt;


        /*开始加速到最快为止*/
        if (speed_up) {
            speed_now = motor_speed_up(speed_now, motor_data);
        } else {
            speed_now = motor_speed_down(speed_now, motor_data);
        }

        write_sync_mode_data(shift_data, heat_data, motor_data, shift_size, heat_size, motor_size);

        /*打印到一半开始减速*/
        if (cnt < 16)
            speed_up = 0;
    }

    /*------------------------------------------停止流程------------------------------------------*/

    draw_shift_data(shift_data, 1);

    /*heat_one_lines[0]的 第24位 设置为1表示下一次加热由锁存触发*/
    heat_data[0].nxt_trigger_type = TPC_HEAT_TRIGGER_BY_LATCH;
    heat_data[0].channels[0] = TPC_HEAT_ENABLE | heat_clkcnt;
    heat_data[0].channels[1] = TPC_HEAT_ENABLE | heat_clkcnt;

    motor_speed_down(0, motor_data);

    write_sync_mode_data(shift_data, heat_data, motor_data, shift_size, heat_size, motor_size);

    heat_data[0].nxt_trigger_type = 0;
    heat_data[0].channels[0] = 0;
    heat_data[0].channels[1] = 0;

    /*电机走的行数 = 打印行数 - 1， 所以结束需要写一行空数据*/
    write_sync_mode_data(shift_data, heat_data, NULL, shift_size, heat_size, 0);

    tpc_sync_mode_data_stream_end();
    tpc_sync_mode_stop(0);

    tpc_sync_mode_deinit();

    free(shift_data);
    free(heat_data);
    free(motor_data);
}

/*lat heat shift 自动对齐， 电机单独运行，需要自己手动对齐*/
void tpc_sync_mode_none_motor_example(void)
{

    printf("start tpc sync mode none motor\n");


    int shift_size;
    unsigned char *shift_data;

    int heat_size;
    struct heat_data *heat_data;

    int motor_size;
    unsigned int *motor_data;

    shift_size = SHIFT_LINES_BYTES * 32;
    shift_data = malloc(shift_size);

    heat_size = sizeof(struct heat_data) *  32;
    heat_data = malloc(heat_size);

    motor_size = motor_cfg_0.repeat_step * 32 * sizeof(int);
    motor_data = malloc(motor_size);


    draw_shift_data(shift_data, 32);

    /*设置电机每步时长*/
    int step_timeus = 10 * 1000;
    set_motor_time(motor_data, 32, step_timeus);

    /*当设置电机单独工作时， 加热时长需要根据电机一行的时间去等待*/
    int motor_one_lines_timeus = step_timeus * MOTOR_ALIGN_STEP;
    set_heat_data(heat_data, 32,  10 * 1000,
                  0, motor_one_lines_timeus);

    int ret;
    ret = tpc_sync_mode_init(&shift_cfg, &heat_cfg, &lat_cfg, NULL, -1);
    if (ret < 0) {
        free(shift_data);
        free(heat_data);
        free(motor_data);
        return;
    }

    ret = tpc_motor_init(MOTOR_ID, &motor_cfg_0);
    if (ret < 0) {
        free(shift_data);
        free(heat_data);
        free(motor_data);
        return;
    }

    /*  第一次写dma，各个设备的数据必须大于等于 内部dma， 内部dma 默认大小为 32 * unit_size;
        shift_unit_size = shift_cfg.data_len * shift_cfg.channels * sizeof(char);
        heat_unit_size = (heat_cfg.channles + 1) * sizeof(int);
        motor_unit_size = motor_cfg.repeat_step * sizeof(int);
    */

    write_sync_mode_data(shift_data, heat_data, motor_data, shift_size, heat_size, motor_size);
    tpc_sync_mode_start();
    tpc_motor_start(MOTOR_ID);

    free(shift_data);
    free(heat_data);
    free(motor_data);

    /*-----------------------接下来一行一行写------------------------------------------*/


    shift_size = SHIFT_LINES_BYTES;
    shift_data = malloc(shift_size);

    heat_size = sizeof(struct heat_data);
    heat_data = malloc(heat_size);

    motor_size = MOTOR_REPEAT_STEP * sizeof(int);
    motor_data = malloc(motor_size);


    int speed_up = 1;
    int speed_now = 0;

    int heat_clkcnt = TPC_US_CLKCNT(10 * 1000);

    int cnt = 32;
    /*后续的数据可以一行一行写，也可以几行几行写*/
    while(cnt--) {
        draw_shift_data(shift_data, 1);

        /*开始加速到最快为止*/
        if (speed_up) {
            speed_now = motor_speed_up(speed_now, motor_data);
        } else {
            speed_now = motor_speed_down(speed_now, motor_data);
        }

        /*与全对齐模式相比，电机不对齐的话，得手动计算加热到下一行的锁存的延时时间*/
        int heat_delay_clkcnt = motor_data[0] + motor_data[1] + motor_data[2] + motor_data[3] - heat_clkcnt;
        heat_data[0].nxt_trigger_type = TPC_HEAT_TRIGGER_BY_LATCH | heat_delay_clkcnt;
        heat_data[0].channels[0] = TPC_HEAT_ENABLE | heat_clkcnt;
        heat_data[0].channels[1] = TPC_HEAT_ENABLE | heat_clkcnt;

        write_sync_mode_data(shift_data, heat_data, motor_data, shift_size, heat_size, motor_size);

        /*打印到一半开始减速*/
        if (cnt < 16)
            speed_up = 0;
    }

    free(shift_data);
    free(heat_data);
    free(motor_data);

    tpc_motor_data_stream_end(MOTOR_ID);
    tpc_sync_mode_data_stream_end();

    tpc_motor_stop(MOTOR_ID, 0);
    tpc_sync_mode_stop(0);

    tpc_motor_deinit(0);
    tpc_sync_mode_deinit();
}


void tpc_sync_mode_example(void)
{

    /*自动模式分两种，shift heat lat motor 自动对齐。 shift heat lat 自动对齐。电机自己动，可按照需求自行手动与打印对齐*/
    tpc_init();

    tpc_sync_mode_with_motor_example();

    msleep(1*1000);

    tpc_sync_mode_none_motor_example();

}
