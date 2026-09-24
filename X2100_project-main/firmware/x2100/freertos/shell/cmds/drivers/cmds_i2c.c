#include <shell.h>
#include <driver/i2c.h>
#include <string.h>
#include <fcntl.h>
#include <dfs_device.h>
#include <os.h>

static void cmd_func_i2c_detect_help(char *cmd)
{
    shell_printf("Usage: \t%s <bus_num>\n", cmd);
    shell_printf("\tI2C bus_num unit: Doc\n");
    shell_printf("Example:\n");
    shell_printf("\t%s 0\n", cmd);
}

static void cmd_func_i2c_write_reg_help(char *cmd)
{
    shell_printf("Usage: \t%s <bus_num> <dev_addr> <reg_addr> <data0> [data....]\n", cmd);
    shell_printf("\tI2C bus_num unit: Doc\n");
    shell_printf("\tI2C addr and data unit: Hex\n");
    shell_printf("Example:\n");
    shell_printf("\t%s 0 0x40 0xf0 0x11 0x22 0x33\n", cmd);
}

static void cmd_func_i2c_read_reg_help(char *cmd)
{
    shell_printf("Usage: \t%s <bus_num> <dev_addr> <reg_addr> <size> \n", cmd);
    shell_printf("\tI2C bus_num and size unit: Doc\n");
    shell_printf("\tI2C addr unit: Hex\n");
    shell_printf("Example:\n");
    shell_printf("\t%s 0 0x40 0xf0 2\n", cmd);
}


static void cmd_func_i2c_read_help(char *cmd)
{
    shell_printf("Usage: \t%s <bus_num> <dev_addr> <size>\n", cmd);
    shell_printf("\tI2C bus_num and size unit: Doc\n");
    shell_printf("\tI2C addr unit: Hex\n");
    shell_printf("Example:\n");
    shell_printf("\t%s 0 0x40 2\n", cmd);
}

static void cmd_func_i2c_write_help(char *cmd)
{
    shell_printf("Usage: \t%s <bus_num> <dev_addr> <data0> [data....]\n", cmd);
    shell_printf("\tI2C bus_num unit: Doc\n");
    shell_printf("\tI2C addr and data unit: Hex\n");
    shell_printf("Example:\n");
    shell_printf("\t%s 0 0x40 0x11 0x22 0x33\n", cmd);
}

void cmd_func_i2c_detect(struct cmd_arg *arg, int argc, char **argv)
{
    int i;
    int ret;
    unsigned int bus_num;
    struct i2c_device *i2c;

    if (argc != 2)
        goto i2c_detect_err;

    ret = sscanf(argv[1], "%d", &bus_num);
    if (ret != 1 || bus_num < 0)
        goto i2c_detect_err;

    i = 0;

    // 探测总线是否存在
    i2c = i2c_register(bus_num, 0xffff, I2C_ADDR_BIT_7, "i2c_cmd_device");
    if (i2c == NULL) {
        shell_printf("i2c bus %d may not registered\n", bus_num);
        return ;
    }
    i2c_unregister(i2c);

    shell_printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
    while (i < 128) {
        if (i % 16 == 0)
            shell_printf("\n%02x: ", i);

        i2c = i2c_register(bus_num, i, I2C_ADDR_BIT_7, "i2c_cmd_device");
        if (i2c == NULL) {
            /*
             * Address Busy
             * Device Address been Registered
             */
            shell_printf("UU ");
        } else {
            ret = i2c_detect_device(i2c);
            if (ret == 0) {
                /* Address ACK */
                shell_printf("%02x ", i);
            } else {
                /* Address NACK  */
                shell_printf("-- ");
            }

            i2c_unregister(i2c);
        }
        i++;
    }
    shell_printf("\n");

    return;
i2c_detect_err:
    cmd_func_i2c_detect_help(argv[0]);
}

void cmd_func_i2c_write_reg(struct cmd_arg *arg, int argc, char **argv)
{
    int i, ret, data;
    unsigned int len;
    char *rbuf = NULL;
    struct i2c_msg write_m;
    unsigned int bus_num;
    unsigned int reg_addr;
    unsigned int dev_addr;
    struct i2c_device *i2c;

    if (argc < 5)
        goto i2c_write_reg_err;

    len = argc - 4;
    rbuf = malloc(len + 1);
    if (rbuf == NULL) {
        shell_printf("%s: malloc failure\n", __func__);
        return;
    }

    ret = sscanf(argv[1], "%d", &bus_num);
    if (ret != 1 || bus_num < 0)
        goto i2c_write_reg_err;

    ret = sscanf(argv[2], "%x", &dev_addr);
    if (ret != 1)
        goto i2c_write_reg_err;

    ret = sscanf(argv[3], "%x", &reg_addr);
    if (ret != 1)
        goto i2c_write_reg_err;

    rbuf[0] = reg_addr;
    i = 4;
    while (i < argc) {
        ret = sscanf(argv[i], "%x", &data);
        if (ret != 1) {
            goto i2c_write_reg_err;
        }

        rbuf[i - 3] = data;
        i++;
    }

    i2c = i2c_register(bus_num, dev_addr, I2C_ADDR_BIT_7, "i2c_cmd_device");
    if (i2c == NULL) {
        free(rbuf);
        shell_printf("i2c device register failure \n");
        return;
    }

    memset(&write_m, 0, sizeof(write_m));
    write_m.buf = rbuf;
    write_m.len = len+1;
    write_m.flags = I2C_M_WR;

    ret = i2c_transfer(i2c, &write_m, 1);
    if (ret > 0) {
        shell_printf("i2c cmd to Slave device write success\n");
    } else {
        shell_printf("i2c cmd to Slave device write failure %d\n", ret);
    }

    i2c_unregister(i2c);
    free(rbuf);
    return;

i2c_write_reg_err:
    if (rbuf)
        free(rbuf);
    cmd_func_i2c_write_reg_help(argv[0]);
}


void cmd_func_i2c_read_reg(struct cmd_arg *arg, int argc, char **argv)
{
    int i, ret;
    char *rbuf = NULL;
    unsigned int len;
    unsigned char data;
    unsigned int bus_num;
    unsigned int reg_addr;
    unsigned int dev_addr;
    struct i2c_device *i2c;
    struct i2c_msg read_m[2];

    if (argc != 5)
        goto i2c_read_reg_err;

    ret = sscanf(argv[1], "%d", &bus_num);
    if (ret != 1 || bus_num < 0)
        goto i2c_read_reg_err;

    ret = sscanf(argv[2], "%x", &dev_addr);
    if (ret != 1)
        goto i2c_read_reg_err;

    ret = sscanf(argv[3], "%x", &reg_addr);
    if (ret != 1)
        goto i2c_read_reg_err;

    ret = sscanf(argv[4], "%d", &len);
    if (ret != 1 || len <= 0)
        goto i2c_read_reg_err;


    rbuf = malloc(len);
    if (rbuf == NULL) {
        shell_printf("%s: malloc spance failure\n", __func__);
        return;
    }

    i2c = i2c_register(bus_num, dev_addr, I2C_ADDR_BIT_7, "i2c_cmd_device");
    if (i2c == NULL) {
        free(rbuf);
        shell_printf("i2c device register failure \n");
        return;
    }

    memset(read_m, 0, sizeof(read_m));
    data = reg_addr;
    read_m[0].flags = I2C_M_WR;
    read_m[0].len = 1;
    read_m[0].buf = &data;

    read_m[1].flags = I2C_M_RD;
    read_m[1].len = len;
    read_m[1].buf = rbuf;

    ret = i2c_transfer(i2c, read_m, 2);
    if (ret == 2) {
        shell_printf("i2c cmd to Slave device read success\n");

        for(i = 0; i < len; i++)
            shell_printf("Data%d: 0x%x \n", i, rbuf[i]);

    } else {
        shell_printf("i2c cmd to Slave device read failure %d\n", ret);
    }

    i2c_unregister(i2c);
    free(rbuf);
    return;

i2c_read_reg_err:
    cmd_func_i2c_read_reg_help(argv[0]);
}

void cmd_func_i2c_write(struct cmd_arg *arg, int argc, char **argv)
{
    int i, ret;
    int data;
    char *rbuf = NULL;
    unsigned int len;
    struct i2c_msg write_m;
    unsigned int bus_num;
    unsigned int dev_addr;
    struct i2c_device *i2c;

    if (argc < 4)
        goto i2c_write_reg_err;

    len = argc - 3;
    rbuf = malloc(len);
    if (rbuf == NULL) {
        shell_printf("malloc spance failure\n");
        return;
    }

    ret = sscanf(argv[1], "%d", &bus_num);
    if (ret != 1 || bus_num < 0)
        goto i2c_write_reg_err;

    ret = sscanf(argv[2], "%x", &dev_addr);
    if (ret != 1)
        goto i2c_write_reg_err;

    i = 3;
    while (i < argc) {
        ret = sscanf(argv[i], "%x", &data);
        if (ret != 1)
            goto i2c_write_reg_err;

        rbuf[i - 3] = data;
        i++;
    }

    i2c = i2c_register(bus_num, dev_addr, I2C_ADDR_BIT_7, "i2c_cmd_device");
    if (i2c == NULL) {
        free(rbuf);
        shell_printf("i2c device register failure\n");
        return;
    }

    memset(&write_m, 0, sizeof(write_m));
    write_m.buf = rbuf;
    write_m.len = len;
    write_m.flags = I2C_M_WR;

    ret = i2c_transfer(i2c, &write_m, 1);
    if (ret > 0) {
        shell_printf("i2c cmd to Slave device write success\n");
    } else {
        shell_printf("i2c cmd to Slave device write failure %d\n", ret);
    }

    i2c_unregister(i2c);
    free(rbuf);
    return;

i2c_write_reg_err:
    if (rbuf)
        free(rbuf);
    cmd_func_i2c_write_help(argv[0]);
}

void cmd_func_i2c_read(struct cmd_arg *arg, int argc, char **argv)
{
    int i, ret;
    unsigned int len;
    char *rbuf = NULL;
    struct i2c_msg read_m;
    unsigned int bus_num;
    unsigned int dev_addr;
    struct i2c_device *i2c;

    if (argc != 4)
        goto i2c_read_data_err;

    ret = sscanf(argv[1], "%d", &bus_num);
    if (ret != 1 || bus_num < 0)
        goto i2c_read_data_err;

    ret = sscanf(argv[2], "%x", &dev_addr);
    if (ret != 1)
        goto i2c_read_data_err;

    ret = sscanf(argv[3], "%d", &len);
    if (ret != 1 || len < 0)
        goto i2c_read_data_err;

    rbuf = malloc(len);
    if (rbuf == NULL) {
        shell_printf("%s: malloc spance failure\n", __func__);
        return;
    }

    i2c = i2c_register(bus_num, dev_addr, I2C_ADDR_BIT_7, "i2c_cmd_device");
    if (i2c == NULL) {
        free(rbuf);
        shell_printf("i2c device register failure\n");
        return;
    }

    memset(&read_m, 0, sizeof(read_m));
    read_m.len = len;
    read_m.flags = I2C_M_RD;
    read_m.buf = rbuf;

    ret = i2c_transfer(i2c, &read_m, 1);
    if (ret > 0) {
        shell_printf("i2c cmd to Slave device read success\n");

        for (i = 0; i < len; i++)
            shell_printf("Data %d: 0x%x \n", i, rbuf[i]);

    } else {
        shell_printf("i2c cmd to Slave device read failure %d\n", ret);
    }

    i2c_unregister(i2c);
    free(rbuf);
    return;

i2c_read_data_err:
    cmd_func_i2c_read_help(argv[0]);
}

void cmd_i2c_init(void)
{
    shell_cmd_register(cmd_func_i2c_detect, "i2c_detect", NULL, "detect bus_num");
    shell_cmd_register(cmd_func_i2c_read_reg, "i2c_read_reg", NULL, "read bus_num data");
    shell_cmd_register(cmd_func_i2c_write_reg, "i2c_write_reg", NULL, "write bus_num data");
    shell_cmd_register(cmd_func_i2c_read, "i2c_read", NULL, "read device reg data");
    shell_cmd_register(cmd_func_i2c_write, "i2c_write", NULL, "write device reg data");
}