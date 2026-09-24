/*
 * Copyright (C) 2020 Ingenic Semiconductor Co., Ltd.
 *
 * GC2053 mipi driver
 *
 */
#include <driver/clk.h>
#include <driver/gpio.h>
#include <driver/gpio_regulator.h>
#include <driver/i2c.h>
#include <driver/camera_isp.h>
#include <common.h>
#include <os.h>
#include <soc/camera_sensor.h>
#include <include_bin.h>
#include <driver/regulator.h>
#include "radar_types.h"

#define I2C_ADDR                        0x37

#define GC2053_SUPPORT_SCLK             37125000

#if (CHEETAH_CONFIG == cheetah_128_256_config)
#define GC2053_DEVICE_WIDTH             1040         //256
#elif (CHEETAH_CONFIG == cheetah_128_512_config)
#define GC2053_DEVICE_WIDTH             2040        //506
#endif

#define GC2053_DEVICE_HEIGHT            128
#define SENSOR_OUTPUT_MAX_FPS           30
#define SENSOR_OUTPUT_MIN_FPS           5

#define GC2053_CHIP_ID_H                (0x20)
#define GC2053_CHIP_ID_L                (0x53)
#define GC2053_REG_CHIP_ID_HIGH         0xF0
#define GC2053_REG_CHIP_ID_LOW          0xF1

#define GC2053_REG_END                  0xffff
#define GC2053_REG_DELAY                0xfffe



struct regval_list {
    unsigned short reg_num;
    unsigned char value;
};

struct again_lut {
    unsigned int index;
    unsigned char regb4;
    unsigned char regb3;
    unsigned char regb8;
    unsigned char regb9;
    unsigned int gain;
};

static int vic_bus_num = CONFIG_X2000_SENSOR_CHEETAH_INDEX;     /* 0(vic0) */
static int power_gpio = CONFIG_X2000_SENSOR_CHEETAH_GPIO_POWER; /* GPIO_PC(21) */
static int pwdn_gpio = CONFIG_X2000_SENSOR_CHEETAH_GPIO_PWDN;  /* GPIO_PB(18) */
static int reset_gpio = CONFIG_X2000_SENSOR_CHEETAH_GPIO_RESET; /* GPIO_PB(12) */
static int bus_num = CONFIG_X2000_SENSOR_CHEETAH_I2C_BUSNUM;    /* i2c5 */
static char *regulator_name = CONFIG_X2000_SENSOR_CHEETAH_POWER_REGULATOR_NAME;
static struct regulator *cheetah_regulator = NULL;

static struct i2c_device *sensor_i2c_dev;
static struct sensor_attr cheetah_sensor_attr;

static struct again_lut gc2053_again_lut[] = {
    {0, 0x00, 0x00,0x01,0x00, 0},
    {1, 0x00, 0x10,0x01,0x0c, 13726},
    {2, 0x00, 0x20,0x01,0x1b, 31176},
    {3, 0x00, 0x30,0x01,0x2c, 44067},
    {4, 0x00, 0x40,0x01,0x3f, 64793},
    {5, 0x00, 0x50,0x02,0x16, 78620},
    {6, 0x00, 0x60,0x02,0x35, 96179},
    {7, 0x00, 0x70,0x03,0x16, 109137},
    {8, 0x00, 0x80,0x04,0x02, 132535},
    {9, 0x00, 0x90,0x04,0x31, 146065},
    {10, 0x00, 0xa0,0x05,0x32, 163565},
    {11, 0x00, 0xb0,0x06,0x35, 176745},
    {12, 0x00, 0xc0,0x08,0x04, 195116},
    {13, 0x00, 0x5a,0x09,0x19, 208558},
    {14, 0x00, 0x83,0x0b,0x0f, 229100},
    {15, 0x00, 0x93,0x0d,0x12, 242508},
    {16, 0x00, 0x84,0x10,0x00, 262416},
    {17, 0x00, 0x94,0x12,0x3a, 275706},
    {18, 0x01, 0x2c,0x1a,0x02, 292248},
    {19, 0x01, 0x3c,0x1b,0x20, 305567},
    {20, 0x00, 0x8c,0x20,0x0f, 324958},
    {21, 0x00, 0x9c,0x26,0x07, 338276},
    {22, 0x02, 0x64,0x36,0x21, 358918},
    {23, 0x02, 0x74,0x37,0x3a, 372262},
    {24, 0x00, 0xc6,0x3d,0x02, 392095},
    {25, 0x00, 0xdc,0x3f,0x3f, 415409},
    {26, 0x02, 0x85,0x3f,0x3f, 421076},
    {27, 0x02, 0x95,0x3f,0x3f, 440355},
    {28, 0x00, 0xce,0x3f,0x3f, 444858},
};

/*
 * Interface    : MIPI - 2lane
 * MCLK         : 24Mhz,
 * MIPI_clcok   : 594MHz
 * resolution   : 1920*1080
 * FrameRate    : 30fps
 */
static struct regval_list gc2053_1920_1080_30fps_mipi_init_regs[] = {
    /* system */
    {0xfe,0x80},
    {0xfe,0x80},
    {0xfe,0x80},
    {0xfe,0x00},
    {0xf2,0x00},
    {0xf3,0x00},
    {0xf4,0x36},
    {0xf5,0xc0},
    {0xf6,0x44},
    {0xf7,0x01},
    {0xf8,0x63},
    {0xf9,0x40},
    {0xfc,0x8e},

    /* CISCTL & ANALOG */
    {0xfe,0x00},
    {0x87,0x18},
    {0xee,0x30},
    {0xd0,0xb7},
    {0x03,0x04},
    {0x04,0x60},    // shutter time
    {0x05,0x04},
    {0x06,0x4c},
    {0x07,0x00},
    {0x08,0x11},
    {0x09,0x00},
    {0x0a,0x02},
    {0x0b,0x00},
    {0x0c,0x02},
    {0x0d,0x04},
    {0x0e,0x40},
    {0x12,0xe2},
    {0x13,0x16},
    {0x17,0x81}, /* bit[1:0] Horizontal mirror */
    {0x19,0x0a},
    {0x21,0x1c},
    {0x28,0x0a},
    {0x29,0x24},
    {0x2b,0x04},
    {0x32,0xf8},
    {0x37,0x03},
    {0x39,0x15},
    {0x43,0x07},
    {0x44,0x40},
    {0x46,0x0b},
    {0x4b,0x20},
    {0x4e,0x08},
    {0x55,0x20},
    {0x66,0x05},
    {0x67,0x05},
    {0x77,0x01},
    {0x78,0x00},
    {0x7c,0x93},
    {0x8c,0x12},
    {0x8d,0x92},
    {0x90,0x01},
    {0x9d,0x10},
    {0xce,0x7c},
    {0xd2,0x41},
    {0xd3,0xdc},
    {0xe6,0x50},

    /* gain */
    {0xb6,0xc0},
    {0xb0,0x70},
    {0xb1,0x01},
    {0xb2,0x00},
    {0xb3,0x00},
    {0xb4,0x00},
    {0xb8,0x01},
    {0xb9,0x00},

    /* blk */
    {0x26,0x30},
    {0xfe,0x01},
    {0x40,0x23},
    {0x55,0x07},
    {0x60,0x40},
    {0xfe,0x04},
    {0x14,0x78},
    {0x15,0x78},
    {0x16,0x78},
    {0x17,0x78},

    /* window */
    {0xfe,0x01},
    {0x92,0x00},
    {0x94,0x03},
    {0x95,0x04},
    {0x96,0x38},
    {0x97,0x07},
    {0x98,0x80},

    /* drop frame */
    //{0xfe,0x01},
    {0x87,0x50},  /* Page1: reg[0x87]:bit[3:0],出图的前N帧丢弃 */

    /* ISP */
    {0xfe,0x01},
    {0x01,0x05},
    {0x02,0x89},
    {0x04,0x01},
    {0x07,0xa6},
    {0x08,0xa9},
    {0x09,0xa8},
    {0x0a,0xa7},
    {0x0b,0xff},
    {0x0c,0xff},
    {0x0f,0x00},
    {0x50,0x1c},
    {0x89,0x03},

    {0xfe,0x04},
    {0x28,0x86},
    {0x29,0x86},
    {0x2a,0x86},
    {0x2b,0x68},
    {0x2c,0x68},
    {0x2d,0x68},
    {0x2e,0x68},
    {0x2f,0x68},
    {0x30,0x4f},
    {0x31,0x68},
    {0x32,0x67},
    {0x33,0x66},
    {0x34,0x66},
    {0x35,0x66},
    {0x36,0x66},
    {0x37,0x66},
    {0x38,0x62},
    {0x39,0x62},
    {0x3a,0x62},
    {0x3b,0x62},
    {0x3c,0x62},
    {0x3d,0x62},
    {0x3e,0x62},
    {0x3f,0x62},

    /* DVP & MIPI */
    {0xfe,0x01},
    {0x9a,0x06},
    {0xfe,0x00},
    {0x7b,0x2a},
    {0x23,0x2d},
    {0xfe,0x03},
    {0x01,0x27},
    {0x02,0x5f},
    {0x03,0xb6},
    {0x12,0x80},
    {0x13,0x07},
    {0x15,0x12},
    {0xfe,0x00},
    {0x3e,0x00},
    {GC2053_REG_END, 0x0},
};
static struct regval_list gc2053_regs_stream_on[] = {
    {0xfe,0x00},
    {0x3e,0x91},
    {GC2053_REG_END, 0x0}, /* END MARKER */
};

static struct regval_list gc2053_regs_stream_off[] = {
    {0xfe,0x00},
    {0x3e,0x00},
    {GC2053_REG_END, 0x0}, /* END MARKER */
};

static int gc2053_write(struct i2c_device *i2c, unsigned char reg, unsigned char value)
{
    unsigned char buf[2] = {reg, value};
    struct i2c_msg msg = {
        .flags  = 0,
        .len    = 2,
        .buf    = buf,
    };

    return 0;

    int ret = i2c_transfer(i2c, &msg, 1);
    if (ret > 0)
        ret = 0;

    return ret;
}

static int gc2053_read(struct i2c_device *i2c, unsigned char reg, unsigned char *value)
{
    // struct i2c_msg msg[2] = {
    //     [0] = {
    //         .flags  = 0,
    //         .len    = 1,
    //         .buf    = &reg,
    //     },
    //     [1] = {
    //         .flags  = I2C_M_RD,
    //         .len    = 1,
    //         .buf    = value,
    //     }
    // };

    // int ret = i2c_transfer(i2c, msg, 2);
    // if (ret > 0)
    //     ret = 0;

    return 0;
}


static int gc2053_write_array(struct i2c_device *i2c, struct regval_list *vals)
{
    // int ret;
    // while (vals->reg_num != GC2053_REG_END) {
    //     if (vals->reg_num == GC2053_REG_DELAY) {
    //             msleep(vals->value);
    //     } else {
    //         ret = gc2053_write(i2c, vals->reg_num, vals->value);
    //         if (ret < 0)
    //             return ret;
    //     }
    //     /* printf("vals->reg_num:%x, vals->value:%x\n",vals->reg_num, vals->value); */
    //     vals++;
    // }
    return 0;
}

static inline int gc2053_read_array(struct i2c_device *i2c, struct regval_list *vals)
{
    // int ret;
    // unsigned char val;

    // while (vals->reg_num != GC2053_REG_END) {
    //     if (vals->reg_num == GC2053_REG_DELAY) {
    //         msleep(vals->value);
    //     } else {
    //         ret = gc2053_read(i2c, vals->reg_num, &val);
    //         if (ret < 0)
    //             return ret;
    //     }
    //     printf("vals->reg_num:%x, vals->value:%x\n", vals->reg_num, val);
    //     vals++;
    // }

    return 0;
}

static int gc2053_detect(struct i2c_device *i2c)
{
    unsigned char h = 1;
    unsigned char l = 1;
    int ret;

    ret = gc2053_read(i2c, GC2053_REG_CHIP_ID_HIGH, &h);
    if (ret < 0)
        return ret;

    if (h != GC2053_CHIP_ID_H) {
        printf("gc2053 read chip id high failed:0x%x != 0x%x\n", h, GC2053_CHIP_ID_H);
        return -ENODEV;
    }

    ret = gc2053_read(i2c, GC2053_REG_CHIP_ID_LOW, &l);
    if (ret < 0)
        return ret;

    if (l != GC2053_CHIP_ID_L) {
        printf("gc2053 read chip id low failed:0x%x != 0x%x\n", l, GC2053_CHIP_ID_L);
        return -ENODEV;
    }

    //printf("gc2053 get chip id = %02x%02x\n", h, l);

    return 0;
}

static int is_inited = 0;

static int init_res(void)
{
    int ret;
    char gpio_str[10];

    if (is_inited)
        return 0;

    if (power_gpio != -1) {
        ret = gpio_request(power_gpio,"gc2053_power");
        if (ret) {
            printf("gc2053: failed to request power pin: %s\n", gpio_to_str(power_gpio, gpio_str, sizeof(gpio_str)));
            goto err_power_gpio;
        }
    }

    if (strcmp(regulator_name, "-1") && strlen(regulator_name)) {
        cheetah_regulator = regulator_get(regulator_name);
        if (!cheetah_regulator) {
            printf("regulator_get error!\n");
            ret = -1;
            goto err_power_gpio;
        }
    }

    if (reset_gpio != -1) {
        ret = gpio_request(reset_gpio,"gc2053_reset");
        if (ret) {
            printf("gc2053: failed to request reset pin: %s\n", gpio_to_str(reset_gpio, gpio_str, sizeof(gpio_str)));
            goto err_reset_gpio;
        }
    }

    if (pwdn_gpio != -1) {
        ret = gpio_request(pwdn_gpio,"gc2053_pwdn");
        if (ret) {
            printf("gc2053: failed to request pwdn pin: %s\n", gpio_to_str(pwdn_gpio, gpio_str, sizeof(gpio_str)));
            goto err_pwdn_gpio;
        }
    }

    sensor_i2c_dev = i2c_register(bus_num, I2C_ADDR, I2C_ADDR_BIT_7, "gc2053_i2c");
    if (sensor_i2c_dev == NULL) {
        printf("cheetah: failed to request i2c bus: %d\n", bus_num);
        // goto err_i2c_register;
    }

    is_inited = 1;

    return 0;

// err_i2c_register:
    if (pwdn_gpio != -1)
        gpio_release(pwdn_gpio);
err_pwdn_gpio:
    if (reset_gpio != -1)
        gpio_release(reset_gpio);
err_reset_gpio:
    if (power_gpio != -1)
        gpio_release(power_gpio);
err_power_gpio:
    return ret;
}

static void deinit_res(void)
{
    if (!is_inited)
        return;

    is_inited = 0;

    i2c_unregister(sensor_i2c_dev);

    if (reset_gpio != -1)
        gpio_release(reset_gpio);

    if (pwdn_gpio != -1)
        gpio_release(pwdn_gpio);

    if (power_gpio != -1)
        gpio_release(power_gpio);

    if (cheetah_regulator)
        regulator_disable(cheetah_regulator);
}

static void cheetah_power_off(void)
{
    if (power_gpio != -1)
        gpio_direction_output(power_gpio, 0);

    if (cheetah_regulator)
        regulator_disable(cheetah_regulator);

    if (reset_gpio != -1)
        gpio_direction_output(reset_gpio, 0);

    if (pwdn_gpio != -1)
        gpio_direction_output(pwdn_gpio, 0);

    camera_disable_sensor_mclk(vic_bus_num);
}

static int cheetah_power_on(void)
{
    int ret;

    ret = init_res();
    if (ret)
        return ret;

    camera_enable_sensor_mclk(vic_bus_num, 24 * 1000 * 1000);

    if (power_gpio != -1)
        gpio_direction_output(power_gpio, 1);

    // if (cheetah_regulator)
    //     regulator_enable(cheetah_regulator);

    if (pwdn_gpio != -1)
        gpio_direction_output(pwdn_gpio, 1);

    if (reset_gpio != -1) {
        gpio_direction_output(reset_gpio, 0);
        msleep(1);
        gpio_direction_output(reset_gpio, 1);
        msleep(8);
    }

    msleep(1);

    //跳过探测
    // ret = gc2053_detect(sensor_i2c_dev);
    // if (ret) {
    //     printf("gc2053: failed to detect\n");
    //     goto err;
    // }


    //跳过sensor的初始化
    // ret = gc2053_write_array(sensor_i2c_dev, cheetah_sensor_attr.sensor_info.private_init_setting);
    // if (ret) {
    //     printf("gc2053: failed to init setting\n");
    //     goto err;
    // }

    return 0;

// err:
    cheetah_power_off();
    deinit_res();
    return ret;
}

static int gc2053_stream_on(void)
{
    int ret = gc2053_write_array(sensor_i2c_dev, gc2053_regs_stream_on);
    if (ret)
        printf("gc2053: failed to stream on\n");
    
    return ret;
}

static void gc2053_stream_off(void)
{
    int ret = gc2053_write_array(sensor_i2c_dev, gc2053_regs_stream_off);
    if (ret)
        printf("gc2053: failed to stream on\n");
}

static int gc2053_g_register(struct sensor_dbg_register *reg)
{
    unsigned char val = 0;
    int ret;

    ret = gc2053_read(sensor_i2c_dev, reg->reg & 0xff, &val);
    if (ret) {
        printf("gc2053: get register failed\n");
        return ret;
    }

    reg->val = val;
    reg->size = 1;

    return 0;
}

static int gc2053_s_register(struct sensor_dbg_register *reg)
{
    return gc2053_write(sensor_i2c_dev, reg->reg & 0xff, reg->val & 0xff);
}

static int gc2053_set_integration_time(int value)
{
    int ret = 0;
    unsigned int expo = value;

    ret = gc2053_write(sensor_i2c_dev, 0xfe, 0x00);
    ret += gc2053_write(sensor_i2c_dev, 0x04, (unsigned char)(expo & 0xff));
    ret += gc2053_write(sensor_i2c_dev, 0x03, (unsigned char)((expo >> 8) & 0x3f));
    if (ret < 0) {
        printf("gc2053: failed to set integration time\n");
        return ret;
    }

    return 0;
}

static unsigned int gc2053_alloc_again(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again)
{
    struct again_lut *lut = gc2053_again_lut;

    while (lut->gain <= cheetah_sensor_attr.sensor_info.max_again) {
        if (isp_gain <= 0) {
            *sensor_again = lut[0].index;
            return lut[0].gain;

        } else if (isp_gain < lut->gain) {
            *sensor_again = (lut - 1)->index;
            return (lut - 1)->gain;

        } else {
            if ((lut->gain == cheetah_sensor_attr.sensor_info.max_again) && (isp_gain >= lut->gain)) {
                *sensor_again = lut->index;
                return lut->gain;
            }
        }
        lut++;
    }

    return isp_gain;
}

static int gc2053_set_analog_gain(int value)
{
    int ret = 0;
    struct again_lut *val_lut = gc2053_again_lut;

    ret = gc2053_write(sensor_i2c_dev, 0xfe, 0x00);
    ret += gc2053_write(sensor_i2c_dev, 0xb4, val_lut[value].regb4);
    ret += gc2053_write(sensor_i2c_dev, 0xb3, val_lut[value].regb3);
    ret += gc2053_write(sensor_i2c_dev, 0xb8, val_lut[value].regb8);
    ret += gc2053_write(sensor_i2c_dev, 0xb9, val_lut[value].regb9);
    if (ret < 0) {
        printf("gc2053: failed to set analog gain\n");
        return ret;
    }

    return 0;
}

static unsigned int gc2053_alloc_dgain(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_dgain)
{
    return isp_gain;
}

static int gc2053_set_digital_gain(int value)
{
    return 0;
}

static int gc2053_set_fps(int fps)
{
    struct sensor_info *sensor_info = &cheetah_sensor_attr.sensor_info;
    unsigned int pclk = GC2053_SUPPORT_SCLK;
    unsigned short hts, vts;
    unsigned short hb = 0;
    unsigned short vb = 0;
    unsigned int newformat;
    unsigned char tmp = 0;
    unsigned short win_high = 0;
    int ret = 0;

    newformat = (((fps >> 16) / (fps & 0xffff)) << 8) + ((((fps >> 16) % (fps & 0xffff)) << 8) / (fps & 0xffff));
    if (newformat > (SENSOR_OUTPUT_MAX_FPS << 8) || newformat <(SENSOR_OUTPUT_MIN_FPS) << 8)
        return -1;

    ret = gc2053_read(sensor_i2c_dev, 0x05, &tmp);
    hb = tmp;
    ret += gc2053_read(sensor_i2c_dev, 0x06, &tmp);
    if (ret < 0)
        return -1;

    hb = (hb << 8) + tmp;
    hts = hb << 2;

    ret = gc2053_read(sensor_i2c_dev, 0x0d, &tmp);
    win_high = tmp;
    ret += gc2053_read(sensor_i2c_dev, 0x0e, &tmp);
    if (ret < 0)
        return -1;

    win_high = (win_high << 8) + tmp;

    vts = pclk * (fps & 0xffff) / hts / ((fps & 0xffff0000) >> 16);
    vb = vts - win_high - 20;

    /* vb */
    ret = gc2053_write(sensor_i2c_dev, 0x08, (unsigned char)(vb & 0xff));
    ret += gc2053_write(sensor_i2c_dev, 0x07, (unsigned char)(vb >> 8) & 0x3f);
    if (ret < 0)
        return -1;

    sensor_info->fps = fps;
    sensor_info->total_height = vts;
    sensor_info->max_integration_time = vts - 4;

    return 0;
}


static struct sensor_attr cheetah_sensor_attr = {
    .device_name            = "sensor_cheetah",

    .dbus_type              = SENSOR_DATA_BUS_MIPI,
    .mipi = {
        .data_fmt           = MIPI_RAW12,
        .lanes              = 4,
        .clk_settle_time    = 130,  /* ns */
        .data_settle_time   = 170,  /* ns */
    },

    .isp_clk_rate           = 300 * 1000 * 1000,

    .sensor_info = {
        .private_init_setting   = gc2053_1920_1080_30fps_mipi_init_regs,

        .width                  = GC2053_DEVICE_WIDTH,
        .height                 = GC2053_DEVICE_HEIGHT,
        .fmt                    = SENSOR_PIXEL_FMT_SRGGB12_1X12,


        .fps                    = 30 << 16 | 1,

        .total_width            = GC2053_DEVICE_WIDTH,
        .total_height           = GC2053_DEVICE_HEIGHT,

        .min_integration_time   = 2,
        .max_integration_time   = 0x460-2,
        .max_again              = 305567,
        .max_dgain              = 0,
    },

    .ops = {
        .power_on               = cheetah_power_on,
        .power_off              = cheetah_power_off,
        .stream_on              = gc2053_stream_on,
        .stream_off             = gc2053_stream_off,

        .get_register           = gc2053_g_register,
        .set_register           = gc2053_s_register,

        .set_integration_time   = gc2053_set_integration_time,
        .alloc_again            = gc2053_alloc_again,
        .set_analog_gain        = gc2053_set_analog_gain,
        .alloc_dgain            = gc2053_alloc_dgain,
        .set_digital_gain       = gc2053_set_digital_gain,
        .set_fps                = gc2053_set_fps,
    },
    .isp_bin_data               = NULL,
};

void cheetah_sensor_init(void)
{
    camera_register_sensor(vic_bus_num, &cheetah_sensor_attr);
}
