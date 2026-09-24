#include <stdlib.h>
#include <shell.h>
#include <driver/spi.h>
#include <driver/gpio.h>
#include <stdio.h>
#include <string.h>
#include <driver/cache.h>

extern int shell_printf(const char *__restrict fmt, ...);
static void cmd_func_spi_help(char *cmd)
{
    shell_printf("Usage:%s <BUS_NUM> <CS_GPIO> <CS_VAILD_LEVEL> <PHA> <POL> <DATA0> [DATA....] \n", cmd);
    shell_printf("\tSPI transfer data unit: unsigned char, Hex\n");
    shell_printf("Example:\n");
    shell_printf("\t%s SPI0 PC00 1 PHA0 POL0 0x10 0x20\n", cmd);
}

static int data_process(char *data)
{
    int ret;
    int temp;

    ret = sscanf(data, "%x", &temp);
    if(ret != 1) {
        return -1;
    }

    return temp;
}

void cmd_func_spi_read_write(struct cmd_arg *arg, int argc, char **argv)
{
    int i, id;
    int len = argc - 6;
    u8 rx_buf[64];
    u8 tx_buf[64];
    int value, level, pha, pol;
    struct spi_config_data config;
    struct spi_message msg;
    struct spi_device *spi;


    if (argc < 7 || len > 64) {
        shell_printf("too little argc\n");
        goto cmd_spi_error;
    }

    /* id */
    value = sscanf(argv[1], "SPI%d", &id);
    if (value != 1) {
        shell_printf("BUS_NUM  = SPI0 or SPI1\n");
         goto cmd_spi_error;
    }

    config.id = id;

    /* GPIO address */
    value = str_to_gpio(argv[2]);
    if (value < 0) {
        shell_printf("CS_GPIO format error!!\n");
        goto cmd_spi_error;
    }
    config.cs_pin = value;

       /* spi vaile level */
    value = sscanf(argv[3], "%d", &level);
    if (value != 1) {
        shell_printf("VAILD LEVEL = 0 or 1\n");
        goto cmd_spi_error;
    }

    if (level)
        config.cs_valid_level = Spi_valid_high;
    else
        config.cs_valid_level = Spi_valid_low;

    /* spi pha */
    value = sscanf(argv[4], "PHA%d", &pha);
     if (value != 1) {
         shell_printf("PHA = PHA0 or PHA1\n");
        goto cmd_spi_error;
     }

    if ((pha == 1) || (pha == 0)) {
        config.spi_pha = pha;
    } else {
        shell_printf("PHA = PHA0 or PHA1\n");
        goto cmd_spi_error;
    }

    /* spi pol */
    value = sscanf(argv[5], "POL%d", &pol);
    if(value != 1) {
        shell_printf("POL = POL0 or POL1\n");
        goto cmd_spi_error;
    }

    if((pol == 1) || (pol == 0)) {
        config.spi_pol = pol;
    } else {
        shell_printf("POL = POL0 or POL1\n");
        goto cmd_spi_error;
    }

    /* others param */
    config.clk_rate = 1*100 * 1000;
    config.tx_endian = Spi_endian_msb_first;
    config.rx_endian = Spi_endian_msb_first;
    config.bits_per_word = 8;
    config.loop_mode = 0;

    /* data */
    for(i = 0; i < len; i++) {
        value = data_process(argv[i + 6]);
        if(value < 0 || value > 0xFF) {
            shell_printf("data format error!!\n");
            goto cmd_spi_error;
        }

        tx_buf[i] = value;
    }

    msg.tx_buf = tx_buf;
    msg.rx_buf = rx_buf;
    msg.tlen = len;
    msg.rlen = len;
    msg.use_dma = 0;
    msg.cs_change = 1;

    spi = spi_register(&config);

    spi_transfer(spi, &msg, 1);

     for(i = 0; i < len; i++) {
         shell_printf("rx_buf[%d] = %2x\n", i, rx_buf[i]);
     }

    spi_unregister(spi);

return;

cmd_spi_error:
    cmd_func_spi_help(argv[0]);
}

void cmd_spi_init(void)
{
    shell_cmd_register(cmd_func_spi_read_write,"spi_read_write", NULL, "spi transfer");
}