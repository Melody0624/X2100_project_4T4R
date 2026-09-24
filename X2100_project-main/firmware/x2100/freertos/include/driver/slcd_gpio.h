#ifndef SLCD_GPIO_H_
#define SLCD_GPIO_H_

#define DATA_WIDTH     24

struct slcd_svae_func
{
    enum gpio_function wr;
    enum gpio_function rd;
    enum gpio_function dc;
    enum gpio_function cs;

    enum gpio_function data_pin[DATA_WIDTH];
};

struct slcd_bus_info
{
    int wr_gpio;
    int rd_gpio;
    int dc_gpio;
    int cs_gpio;

    int data_width;
    int cmd_width;

    int data_pin[DATA_WIDTH];

    struct slcd_svae_func slcd_func;
};

void slcd_gpio_save(struct slcd_bus_info* info);
void slcd_gpio_restore(struct slcd_bus_info* info);
void slcd_gpio_send_cmd(struct slcd_bus_info* info, unsigned int cmd);
void slcd_gpio_send_data(struct slcd_bus_info* info, unsigned int data);
unsigned int slcd_gpio_receive_data(struct slcd_bus_info* info);

#endif