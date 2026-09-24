#include <common.h>
#include <driver/i2c.h>
#include <os.h>

/**
 * 具体设备的写函数,需要根据具体的设备去实现
 * i2c: 从设备句柄
 * reg: 从设备具体的寄存器地址
 * buf: 发送缓冲区
 * len: 发送数据的个数
 */
int xx_reg_write(struct i2c_device *i2c, unsigned short reg, char *buf, int len)
{
    char tbuf[128];

    /* 如果需要访问具体的寄存器需要将reg和buf内容进行拼接,将拼接好的内容一起传给i2c_message */
    tbuf[0] = reg;
    if (buf) {
        memcpy((void *)(tbuf+1), (void *)buf, len);
        len++;
    }

    struct i2c_msg m = {
        .buf = tbuf,    /* tbuf为拼接后的内容 */
        .len  = len,      /* 发送数据的个数 */
        .flags = I2C_M_WR,
    };
    return i2c_transfer(i2c, &m, 1);
}

/*＊
 * 具体设备的读函数,需要根据具体的设备去实现
 * i2c: 从设备句柄
 * reg: 从设备具体的寄存器地址
 * buf: 接收缓冲区
 * len: 接收数据的个数
 */
int xx_reg_read(struct i2c_device *i2c, unsigned short reg, char *buf, int len)
{
    /* i2c 读操作前需要先进行写操作 */
    xx_reg_write(i2c, reg, NULL, 1);

    struct i2c_msg m = {
        .buf = buf,
        .len  = len,
        .flags = I2C_M_RD,/* 发送读操作标志 */
    };
    return i2c_transfer(i2c, &m, 1);
}

void i2c_test(void *data)
{
    int ret = 0;
    char tbuf[1] = {0x12};
    char rbuf[1024] = {0};

    struct i2c_device *i2c = i2c_register(1, 0x40, I2C_ADDR_BIT_7, "i2c");/* 0x40是从设备地址 */
    if (i2c == NULL) {
        printf("i2c register error! may be exist the same address on the same bus!\n");
        return;
    }

    /* 具体设备的读写函数需要自己实现，这里只给出简单的示例 */
    ret = xx_reg_write(i2c, 0xf0, tbuf, 1);/* 0xf0是i2c外设具体的寄存器地址 */
    if (ret < 0)
        printf("i2c write error %d\n", ret);
    ret = xx_reg_read(i2c, 0xf0, rbuf, 1);
    if (ret < 0)
        printf("i2c write error %d\n", ret);

    printf("i2c: rx_buf = %x\n", rbuf[0]);

    i2c_unregister(i2c);
}

void test_main(void)
{
    thread_create("i2c_test", 2048, i2c_test, NULL);
}