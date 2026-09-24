#include <shell.h>
#include <stdlib.h>
#include <string.h>
#include <soc/gpio.h>
#include <driver/gpio.h>

#define GPIO_FUNC_MASK      0x01ff
#define GPIO_PULL_MASK      0xe000

#define X1600_GPIO_CNT  (GPIO_PD(5) - GPIO_PA(0) + 1)

struct func_info {
    const char *func_name;
    const char *func_notes;
    const unsigned int func_value;
};

static const struct func_info gpiofunc[] = {
    {"FUNC_0", "GPIO as function 0", GPIO_FUNC_0},       //00000
    {"FUNC_1", "GPIO as function 1", GPIO_FUNC_1},       //00001
    {"FUNC_2", "GPIO as function 2", GPIO_FUNC_2},       //00010
    {"FUNC_3", "GPIO as function 3", GPIO_FUNC_3},       //00011
    {"OUTPUT0", "GPIO output low  level", GPIO_OUTPUT0}, //00100
    {"OUTPUT1", "GPIO output high level", GPIO_OUTPUT1}, //00101
    {"INPUT", "GPIO as input", GPIO_INPUT},              //00110

    {"INT_LO", "Low  Level trigger interrupt", GPIO_INT_LO}, //01000
    {"INT_HI", "High Level trigger interrupt", GPIO_INT_HI}, //01001
    {"INT_FE", "Fall Edge trigger interrupt", GPIO_INT_FE},  //01010
    {"INT_RE", "Rise Edge trigger interrupt", GPIO_INT_RE},  //01011
    {"INT_FE_RE", "Port is dual-edge(both rise and fall edge) triggered interrupt input", GPIO_INT_RE_FE},  //11011
    {"INT_LO_M", "Low  Level trigger interrupt, Interrupt is masked", GPIO_INT_MASK_LO},  // 01100
    {"INT_HI_M",  "High Level trigger interrupt, Interrupt is masked", GPIO_INT_MASK_HI}, // 01101
    {"INT_FE_M",  "Fall Edge trigger interrupt, Interrupt is masked", GPIO_INT_MASK_FE},  // 01110
    {"INT_RE_M", "Rise Edge trigger interrupt, Interrupt is masked", GPIO_INT_MASK_RE},   // 01111
    {"INT_FE_RE_M", "Port is dual-edge(both rise and fall edge) triggered interrupt input. Interrupt is masked", GPIO_INT_MASK_RE_FE},  //11111

    {"PULL_HIZ", "no pull", GPIO_PULL_HIZ},
    {"PULL", "pull", GPIO_PULL},
};

static int gpio_get_count(void)
{
    return X1600_GPIO_CNT;
}

static void func_des_print(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(gpiofunc); i++)
        shell_printf("\t%s:\t\t%s\n", gpiofunc[i].func_name, gpiofunc[i].func_notes);
}

static inline const struct func_info* get_gpio_func_info(enum gpio_function func)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(gpiofunc); i++) {
        if (func == gpiofunc[i].func_value)
            return &gpiofunc[i];
    }

    return NULL;
}

static void func_print(int gpio)
{
    char buf[10];
    enum gpio_function func;
    const struct func_info* info;

    shell_printf("\t%s  ", gpio_to_str(gpio, buf, sizeof(buf)));

    func = gpio_get_func(gpio);

    info = get_gpio_func_info(func & GPIO_FUNC_MASK);
    if(info != NULL)
        shell_printf("%s", info->func_name);

    info = get_gpio_func_info(func & GPIO_PULL_MASK);
    if(info != NULL)
        shell_printf("| %s", info->func_name);

    shell_printf("\n");
}

static void get_func_help(char *cmd)
{
    shell_printf("Usage:  %s [GPIO]\n", cmd);
    shell_printf("\tGPIO: PXn   X(A,B,C,D)  n(0~31)\n");
    shell_printf("Example 1:\n");
    shell_printf("\t%s\n\n", cmd);
    shell_printf("Example 2:\n");
    shell_printf("\t%s PA8\n", cmd);
}

static void set_func_help(char *cmd)
{
    shell_printf("Usage:  %s <GPIO> <FUNCTION>\n", cmd);
    shell_printf("\tGPIO: PXn   X(A,B,C,D)  n(0~31)\n");
    shell_printf("\tFUNCTION:\n");

    func_des_print();

    shell_printf("Example:\n");
    shell_printf("\t%s PA8 OUTPUT0\n", cmd);
}

static void get_value_help(char *cmd)
{
    shell_printf("Usage:  %s <GPIO>\n", cmd);
    shell_printf("\tGPIO: PXn  X(A,B,C,D)  n(0~31)\n");
    shell_printf("Example:\n");
    shell_printf("\t%s PA8\n", cmd);
}

static void set_value_help(char *cmd)
{
    shell_printf("Usage:  %s <GPIO> <value>\n", cmd);
    shell_printf("\tGPIO: PXn value  X(A,B,C,D)  n(0~31)\n");
    shell_printf("\tvalue:\n\t    1   (high level)\n\t    0   (low  level)\n");
    shell_printf("Example:\n");
    shell_printf("\t%s PA8 0\n", cmd);
}

void cmd_gpio_set_value(struct cmd_arg *arg, int argc, char **argv)
{
    int ret;
    int gpio, value;

    if (argc != 3)
        goto set_value_err;

    gpio = str_to_gpio(argv[1]);
    if (gpio < 0)
        goto set_value_err;

    ret = sscanf(argv[2], "%d", &value);
    if (ret != 1 || value < 0)
        goto set_value_err;

    gpio_set_value(gpio, value);
    return;

set_value_err:
    set_value_help(argv[0]);
}

void cmd_gpio_get_value(struct cmd_arg *arg, int argc, char **argv)
{
    int gpio;

    if(argc != 2)
        goto get_value_err;

    gpio = str_to_gpio(argv[1]);
    if (gpio < 0)
        goto get_value_err;

    shell_printf("%s value is %s\n", argv[1], gpio_get_value(gpio) ? "high level" : "low level");
    return;

get_value_err:
    get_value_help(argv[0]);
}

void cmd_gpio_set_func(struct cmd_arg *arg, int argc, char **argv)
{
    int i;
    int gpio;

    if (argc != 3)
        goto set_func_err;

    gpio = str_to_gpio(argv[1]);
    if (gpio < 0)
        goto set_func_err;

    for (i = 0; i < ARRAY_SIZE(gpiofunc); i++) {
        if (strcmp(argv[2], gpiofunc[i].func_name) == 0) {
            gpio_set_func(gpio, gpiofunc[i].func_value);
            return;
        }
    }

set_func_err:
    set_func_help(argv[0]);
}

void cmd_gpio_get_func_des(struct cmd_arg *arg, int argc, char **argv)
{
    shell_printf("\tPort Description\n");

    func_des_print();
}

void cmd_gpio_get_func(struct cmd_arg *arg, int argc, char **argv)
{
    int cnt;
    int gpio;

    if (argc != 2 && argc != 1)
        goto get_func_err;

    shell_printf("\tNOTE:You can use the [gpio_get_func_des] to view Port Description.\n");
    shell_printf("\tGPIO  Function\n");

    if (argc == 1) {
        cnt = gpio_get_count();

        for (gpio = 0; gpio < cnt; gpio++)
            func_print(gpio);

    } else {
        gpio = str_to_gpio(argv[1]);
        func_print(gpio);
    }

    return;

get_func_err:
    get_func_help(argv[0]);
}

void cmd_gpio_init(void)
{
    shell_cmd_register(cmd_gpio_get_value, "gpio_get_value", NULL, "get IO level ");
    shell_cmd_register(cmd_gpio_set_value, "gpio_set_value", NULL, "Set IO level");
    shell_cmd_register(cmd_gpio_set_func, "gpio_set_func",  NULL, "Set IO function");
    shell_cmd_register(cmd_gpio_get_func, "gpio_get_func",  NULL, "Get IO function");
    shell_cmd_register(cmd_gpio_get_func_des, "gpio_get_func_des",  NULL, "Get IO function description");
}
