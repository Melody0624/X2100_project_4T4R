#include <driver/gpio.h>

#define I2S3_PORT GPIO_PORT_A
#define I2S3_FUNC GPIO_FUNC_2

#define I2S3_TX_MCLK 0
#define I2S3_TX_LRCK 2
#define I2S3_TX_DATA0 3
#define I2S3_TX_DATA1 4
#define I2S3_TX_DATA2 5
#define I2S3_TX_DATA3 6
#define I2S3_TX_BCLK 16

#define I2S2_PORT GPIO_PORT_A
#define I2S2_FUNC GPIO_FUNC_2

#define I2S2_RX_MCLK 7
#define I2S2_RX_LRCK 9
#define I2S2_RX_DATA0 10
#define I2S2_RX_DATA1 11
#define I2S2_RX_DATA2 12
#define I2S2_RX_DATA3 13
#define I2S2_RX_BCLK 17

#define I2S1_PORT GPIO_PORT_C
#define I2S1_FUNC GPIO_FUNC_2

#define I2S1_RX_MCLK 1
#define I2S1_RX_BCLK 2
#define I2S1_RX_LRCK 3
#define I2S1_RX_DATA 4

#define I2S1_TX_BCLK 5
#define I2S1_TX_LRCK 6
#define I2S1_TX_DATA 7
#define I2S1_TX_MCLK 8

#define PCM_PORT GPIO_PORT_D
#define PCM_FUNC GPIO_FUNC_2

#define PCM_CLK 2
#define PCM_DO 3
#define PCM_DI 4
#define PCM_SYNC 5

#define to_gpio(port, pin) ((port) * 32 + (pin))

struct pin_name {
    int pin;
    const char *name;
};

static int aic_request_gpio(unsigned int *flags, int port, int pin, int func, const char *name)
{
    if (*flags & BIT(pin))
        return 0;

    int gpio = to_gpio(port, pin);
    int ret = gpio_request(gpio, name);
    if (ret) {
        char str[10];
        printf("aic: failed to request gpio: %s as %s\n",
            gpio_to_str(gpio, str, sizeof(str)), name);
        return ret;
    }

    gpio_set_func(gpio, func);

    *flags |= BIT(pin);

    return 0;
}

static int aic_request_gpio_array(unsigned int *flags, int port, struct pin_name *pins, int N, int func)
{
    int i = 0;
    unsigned int tmp_flags = 0;

    for (i = 0; i < N; i++) {
        int pin = pins[i].pin;
        const char *name = pins[i].name;
        int gpio = to_gpio(port, pin);

        if (*flags & BIT(pin))
            continue;

        int ret = gpio_request(gpio, name);
        if (ret) {
            char str[10];
            printf("aic: failed to request gpio: %s as %s\n",
                gpio_to_str(gpio, str, sizeof(str)), name);
            return ret;
        }

        tmp_flags |= BIT(pin);
    }

    if (tmp_flags) {
        gpio_port_set_func(port, tmp_flags, func);
        *flags |= tmp_flags;
    }

    return 0;
}

static unsigned int i2s1_flag;
static unsigned int i2s2_flag;
static unsigned int i2s3_flag;
static unsigned int pcm_flag;

static int aic_rx_mclk_gpio_request(int id)
{
    if (id == 0 || id == 4)
        return 0;

    if (id == 1)
        return aic_request_gpio(
            &i2s1_flag, I2S1_PORT, I2S1_RX_MCLK, I2S1_FUNC, "i2s1-rx-mclk");

    if (id == 2)
        return aic_request_gpio
            (&i2s2_flag, I2S2_PORT, I2S2_RX_MCLK, I2S2_FUNC, "i2s2-rx-mclk");

    printf("aic: aic%d no rx mclk\n", id);

    return -1;
}

static int aic_tx_mclk_gpio_request(int id)
{
    if (id == 0 || id == 4)
        return 0;

    if (id == 1)
        return aic_request_gpio(
            &i2s1_flag, I2S1_PORT, I2S1_TX_MCLK, I2S1_FUNC, "i2s1-tx-mclk");

    if (id == 3) {
        return aic_request_gpio(
            &i2s3_flag, I2S3_PORT, I2S3_TX_MCLK, I2S3_FUNC, "i2s3-tx-mclk");
    }


    printf("aic: aic%d no tx mclk\n", id);

    return -1;
}


static struct pin_name i2s3_tx_pins[] = {
    {I2S3_TX_LRCK, "i2s3-tx-lrck"},
    {I2S3_TX_BCLK, "i2s3-tx-bclk"},
    {I2S3_TX_DATA0, "i2s3-tx-data0"},
    {I2S3_TX_DATA1, "i2s3-tx-data1"},
    {I2S3_TX_DATA2, "i2s3-tx-data2"},
    {I2S3_TX_DATA3, "i2s3-tx-data3"},
};

static struct pin_name i2s2_rx_pins[] = {
    {I2S2_RX_LRCK, "i2s2-rx-lrck"},
    {I2S2_RX_BCLK, "i2s2-rx-bclk"},
    {I2S2_RX_DATA0, "i2s2-rx-data0"},
    {I2S2_RX_DATA1, "i2s2-rx-data1"},
    {I2S2_RX_DATA2, "i2s2-rx-data2"},
    {I2S2_RX_DATA3, "i2s2-rx-data3"},
};

static struct pin_name i2s1_rx_pins[] = {
    {I2S1_RX_BCLK, "i2s1-rx-bclk"},
    {I2S1_RX_LRCK, "i2s1-rx-lrck"},
    {I2S1_RX_DATA, "i2s1-rx-data"},
};

static struct pin_name i2s1_tx_pins[] = {
    {I2S1_TX_BCLK, "i2s1-tx-bclk"},
    {I2S1_TX_LRCK, "i2s1-tx-lrck"},
    {I2S1_TX_DATA, "i2s1-tx-data"},
};

static struct pin_name pcm_clk_pins[] = {
    {PCM_CLK, "pcm-clk"},
    {PCM_SYNC, "pcm-sync"},
};

static struct pin_name pcm_rx_pins[] = {
    {PCM_DO, "pcm-di"},
};

static struct pin_name pcm_tx_pins[] = {
    {PCM_DI, "pcm-do"},
};

static int aic_rx_gpio_request(int id, int channels)
{
    if (id == 0)
        return 0;

    if (id == 1)
        return aic_request_gpio_array(
            &i2s1_flag, I2S1_PORT, i2s1_rx_pins, ARRAY_SIZE(i2s1_rx_pins), I2S1_FUNC);

    if (id == 2)
        return aic_request_gpio_array(
            &i2s2_flag, I2S2_PORT, i2s2_rx_pins, 2 + (channels+1)/2, I2S2_FUNC);

    if (id == 4) {
        int ret = aic_request_gpio_array(
            &pcm_flag, PCM_PORT, pcm_clk_pins, ARRAY_SIZE(pcm_clk_pins), PCM_FUNC);
        if (ret)
            return ret;
        return aic_request_gpio_array(
            &pcm_flag, PCM_PORT, pcm_rx_pins, ARRAY_SIZE(pcm_rx_pins), PCM_FUNC);
    }

    printf("aic: aic%d no rx pins\n", id);

    return -1;
}

static int aic_tx_gpio_request(int id, int channels)
{
    if (id == 0)
        return 0;

    if (id == 1)
        return aic_request_gpio_array(
            &i2s1_flag, I2S1_PORT, i2s1_tx_pins, ARRAY_SIZE(i2s1_tx_pins), I2S1_FUNC);

    if (id == 3) {
        return aic_request_gpio_array(
            &i2s3_flag, I2S3_PORT, i2s3_tx_pins, 2 + (channels+1)/2, I2S3_FUNC);
    }

    if (id == 4) {
        int ret = aic_request_gpio_array(
            &pcm_flag, PCM_PORT, pcm_clk_pins, ARRAY_SIZE(pcm_clk_pins), PCM_FUNC);
        if (ret)
            return ret;
        return aic_request_gpio_array(
            &pcm_flag, PCM_PORT, pcm_tx_pins, ARRAY_SIZE(pcm_tx_pins), PCM_FUNC);
    }

    printf("aic: aic%d no tx pins\n", id);

    return -1;
}