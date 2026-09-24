#include <stdio.h>
#include <common.h>
#include <os.h>
#include <driver/cache.h>
#include <soc/tpc.h>
#include <ring_mem.h>

/*每行单个数据通道输出多少字节*/
#define SHIFT_LINES_BYTES 48
/*数据通道*/
#define SHIFT_CHANNELS     1

#define HEAT_CHANNELS      2

#define MOTOR_CHANNLES     4
#define MOTOR_REPEAT_STEP  4

#define MOTOR_ALIGN_STEP   4

#define MOTOR_ID  0

#define TPC_US_CLKCNT(us)      ((uint64_t)us*(DEFAULT_TPC_CLK_RATE / (1*1000*1000)))

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


static struct heat_data heat_buffer[32];
static unsigned char shift_buffer[SHIFT_LINES_BYTES * 32];

DEFINE_RING_MEM(heat_ringmem, heat_buffer);
DEFINE_RING_MEM(shift_ringmem, shift_buffer);



/*绘制虚线*/
void draw_shift_data(void *shift_data, int lines)
{
    int i = 0;
    char *data = shift_data;
    for(i = 0; i < SHIFT_LINES_BYTES * lines; i++) {
        data[i] = 0x55;
    }
}

static void set_heat_data(struct heat_data *heat_data, int lines, int heat_time)
{
    int i;
    for (i = 0; i < lines; i++) {
        heat_data[i].nxt_trigger_type = 0;
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


static volatile int need_stop = 0;

static unsigned char shift_one_lines[SHIFT_LINES_BYTES];
static struct heat_data heat_one_lines;

/*电机中断回调，该回调取决于 motor_cfg.align_step 参数
  若用的是pwm 模拟电机或者其他形式的电机。也可参考该流程。中断回调里面开始输出数据*/
void motor_irq_callback(void *data)
{
    if (need_stop)
        return;

    int ret = ring_mem_read(&shift_ringmem, &shift_one_lines, sizeof(shift_one_lines));
    if (!ret) {
        printf("no shift_data read\n");
        goto motor_err;
    }

    ret = tpc_shift_write_data(shift_one_lines, sizeof(shift_one_lines));
    if (!ret) {
        printf("shift dma can not start\n");
        goto motor_err;
    }

    tpc_shift_start();
    return;

motor_err:
    need_stop = 1;
}

static int write_nomal_mode_data(unsigned char *shift_data, void *heat_data, unsigned int *motor_data,
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

        ret = ring_mem_write(&heat_ringmem, heat_data, heat_w);
        heat_data += ret;
        heat_w -= ret;

        ret = ring_mem_write(&shift_ringmem, shift_data, shift_w);
        shift_data +=  ret;
        shift_w -= ret;

        if (!motor_w && !heat_w && !shift_w)
            break;
    }

    return 0;
}


static volatile int is_first_heat_finish = 1;
void heat_irq_callback(void *data)
{
    /*等第一次加热完毕之后再触发电机工作*/
    if (is_first_heat_finish) {
        tpc_motor_start(MOTOR_ID);
        is_first_heat_finish = 0;
    }
}

void shift_irq_callback(void *data)
{
    if (need_stop)
        return;

    tpc_latch_start();
}

void lat_irq_callback(void *data)
{
    if (need_stop)
        return;

    int ret = ring_mem_read(&heat_ringmem, &heat_one_lines, sizeof(heat_one_lines));
    if (!ret) {
        printf("no shift_data read\n");
        goto lat_err;
    }

    ret = tpc_heat_write_data((unsigned int *)&heat_one_lines, sizeof(heat_one_lines));
    if (!ret) {
        printf("heat dma can not start\n");
        goto lat_err;
    }

    tpc_heat_start();
    return;
lat_err:
    need_stop = 1;
}


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

/*该模式下， lat heat shift 模块每次只支持一行输出。*/
void tpc_shift_heat_lat_example(void)
{
    /*计算一行数据输出多久*/
    unsigned int tx_one_lines_shift_data_timeus =  SHIFT_LINES_BYTES /  (shift_cfg.out_clk_rate / (1 * 1000 * 1000));

    /*设置触发lat 等待多长时间输出 = 建立时间+ 保持时间 + 输出时间*/
    lat_cfg.wait_clkcnt_out = TPC_US_CLKCNT(tx_one_lines_shift_data_timeus);

    /*设置触发加热等待多久后输出 = shfit_data 输出需要的时间 + lat 有效时间*/
    heat_cfg.wait_clkcnt_out = lat_cfg.active_clkcnt + lat_cfg.wait_clkcnt_out;

    shift_cfg.irq_callback = shift_irq_callback;
    tpc_shift_init(&shift_cfg);

    lat_cfg.irq_callback = lat_irq_callback;
    tpc_latch_init(&lat_cfg);

    heat_cfg.irq_callback = heat_irq_callback;
    tpc_heat_init(&heat_cfg);

    motor_cfg_0.irq_callback = motor_irq_callback;
    tpc_motor_init(0, &motor_cfg_0);

    int shift_size;
    unsigned char *shift_data;

    int heat_size;
    struct heat_data *heat_data;

    int motor_size;
    unsigned int *motor_data;

    shift_size = SHIFT_LINES_BYTES * 32;
    shift_data = malloc(shift_size);

    heat_size =  sizeof(struct heat_data) * 32;
    heat_data = malloc(heat_size);

    motor_size = MOTOR_REPEAT_STEP * sizeof(int) * 32;
    motor_data = malloc(motor_size);


    draw_shift_data(shift_data, 32);

    set_heat_data(heat_data, 32, 10 * 1000);

    set_motor_time(motor_data, 32, 10 * 1000);

    /*  写 heat shift 到 ringmem 缓存， 写 motor 到 dma 缓存
        第一次的 motor 缓存必须写满， heat shift 的不能超过 ringmem 的大小
    */
    write_nomal_mode_data(shift_data, heat_data, motor_data, shift_size, heat_size, motor_size);

    /*开始打印流程*/
    motor_irq_callback(NULL);

    free(shift_data);
    free(motor_data);
    free(heat_data);

    /*------------------------------------------接下来一行一行写------------------------------------------------*/
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

        /*理论上 越往后加热时间越短，电机运行越快。一行的加热时长不可以超过电机走一行对应的时长*/
        heat_data[0].nxt_trigger_type = TPC_HEAT_TRIGGER_BY_LATCH | 0;
        heat_data[0].channels[0] = TPC_HEAT_ENABLE | heat_clkcnt;
        heat_data[0].channels[1] = TPC_HEAT_ENABLE | heat_clkcnt;


        /*开始加速到最快为止*/
        if (speed_up) {
            speed_now = motor_speed_up(speed_now, motor_data);
        } else {
            speed_now = motor_speed_down(speed_now, motor_data);
        }

        write_nomal_mode_data(shift_data, heat_data, motor_data, shift_size, heat_size, motor_size);

        /*打印到一半开始减速*/
        if (cnt < 16)
            speed_up = 0;

        if (need_stop)
            break;
    }

    /* 等待的打印机启动，打印机启动为第一次加热完成后 */
    while(is_first_heat_finish) {
        msleep(1);
    }

    tpc_motor_data_stream_end(MOTOR_ID);
    tpc_motor_stop(MOTOR_ID, 0);
    tpc_motor_deinit(MOTOR_ID);

    tpc_heat_deinit();
    tpc_shift_deinit();
    tpc_latch_deinit();
}


void tpc_nomal_mode_example(void)
{
    tpc_init();

    tpc_shift_heat_lat_example();
}
