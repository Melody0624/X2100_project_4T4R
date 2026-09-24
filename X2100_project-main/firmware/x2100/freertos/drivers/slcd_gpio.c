#include <driver/gpio.h>
#include <common.h>
#include <driver/slcd_gpio.h>

void slcd_gpio_save(struct slcd_bus_info* info)
{
    int i, max_len;

    assert(info);
    assert(gpio_is_valid(info->dc_gpio));
    assert(gpio_is_valid(info->cs_gpio));

    max_len = info->data_width > info->cmd_width ? info->data_width : info->cmd_width;

    for (i = 0; i < max_len; i++)
        assert(gpio_is_valid(info->data_pin[i]));

    info->slcd_func.dc = gpio_get_func(info->dc_gpio);
    info->slcd_func.rd = gpio_get_func(info->rd_gpio);
    info->slcd_func.wr = gpio_get_func(info->wr_gpio);
    info->slcd_func.cs = gpio_get_func(info->cs_gpio);

    for (i = 0; i < max_len; i++)
        info->slcd_func.data_pin[i] = gpio_get_func(info->data_pin[i]);

    gpio_direction_output(info->cs_gpio, 1);

    gpio_direction_output(info->dc_gpio, 0);
    gpio_direction_output(info->rd_gpio, 1);
    gpio_direction_output(info->wr_gpio, 1);

    for (i = 0; i < max_len; i++)
        gpio_direction_output(info->data_pin[i], 0);

    gpio_direction_output(info->cs_gpio, 0);
}

void slcd_gpio_restore(struct slcd_bus_info* info)
{
    int i, max_len;

    assert(info);

    max_len = info->data_width > info->cmd_width ? info->data_width : info->cmd_width;

    gpio_direction_output(info->cs_gpio, 1);

    gpio_set_func(info->dc_gpio, info->slcd_func.dc);
    gpio_set_func(info->wr_gpio, info->slcd_func.wr);
    gpio_set_func(info->rd_gpio, info->slcd_func.rd);

    for (i = 0; i < max_len; i++)
        gpio_set_func(info->data_pin[i], info->slcd_func.data_pin[i]);

    gpio_set_func(info->cs_gpio, info->slcd_func.cs);
}

void slcd_gpio_send_cmd(struct slcd_bus_info* info, unsigned int cmd)
{
    int i;

    assert(info);

    if (!gpio_is_valid(info->wr_gpio))
        panic("%s: slcd wr gpio %d define error\n", __func__, info->wr_gpio);

    gpio_direction_output(info->dc_gpio, 0);
    gpio_direction_output(info->wr_gpio, 0);

    for (i = 0; i < info->cmd_width; i++)
        gpio_direction_output(info->data_pin[i], (cmd >> i) & 0x01);

    gpio_direction_output(info->wr_gpio, 1);
}


void slcd_gpio_send_data(struct slcd_bus_info* info, unsigned int data)
{
    int i;

    assert(info);

    if (!gpio_is_valid(info->wr_gpio))
        panic("%s: slcd wr gpio %d define error\n", __func__, info->wr_gpio);

    gpio_direction_output(info->dc_gpio, 1);
    gpio_direction_output(info->wr_gpio, 0);

    for (i = 0; i < info->data_width; i++)
        gpio_direction_output(info->data_pin[i], (data >> i) & 0x01);

    gpio_direction_output(info->wr_gpio, 1);
}

unsigned int slcd_gpio_receive_data(struct slcd_bus_info* info)
{
    int i;
    unsigned int value = 0;

    assert(info);

    if (!gpio_is_valid(info->rd_gpio))
        panic("%s: slcd rd gpio %d define error\n", __func__, info->rd_gpio);

    gpio_direction_output(info->dc_gpio, 1);
    gpio_direction_output(info->rd_gpio, 0);
    for (i = 0; i < info->data_width; i++) {
        gpio_direction_input(info->data_pin[i]);
        value = gpio_get_value(info->data_pin[i]) << i | value;
    }

    gpio_direction_output(info->rd_gpio, 1);

    return value;
}