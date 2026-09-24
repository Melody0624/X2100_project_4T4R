#include <driver/gpio.h>
#include <driver/gpio_i2c.h>
#include <errno.h>
#include <os.h>
#include <common.h>

#define I2C_TIMEOUT_US  (100*1000)
#define I2C_TIMEOUT     -ETIMEDOUT
#define I2C_NO_ACK      -ENXIO

struct gpio_i2c_drv {
    int retries;
    unsigned int time_ns;
    int is_malloc;
    char name[10];
    int is_enable;
    int i2c_bus_id;
    int gpio_scl;
    int gpio_sda;
    unsigned int clk_rate;
    struct i2c_bus i2c_bus;
};

static struct gpio_i2c_drv i2c_drv[4] = {
    {
        #ifdef CONFIG_GPIO_I2C0
        .is_enable = 1,
        .i2c_bus_id = CONFIG_GPIO_I2C0_ID,
        .gpio_scl = CONFIG_GPIO_I2C0_SCL,
        .gpio_sda = CONFIG_GPIO_I2C0_SDA,
        .clk_rate = CONFIG_GPIO_I2C0_CLK_RATE
        #endif
    },
    {
        #ifdef CONFIG_GPIO_I2C1
        .is_enable = 1,
        .i2c_bus_id = CONFIG_GPIO_I2C1_ID,
        .gpio_scl = CONFIG_GPIO_I2C1_SCL,
        .gpio_sda = CONFIG_GPIO_I2C1_SDA,
        .clk_rate = CONFIG_GPIO_I2C1_CLK_RATE
        #endif
    },
    {
        #ifdef CONFIG_GPIO_I2C2
        .is_enable = 1,
        .i2c_bus_id = CONFIG_GPIO_I2C2_ID,
        .gpio_scl = CONFIG_GPIO_I2C2_SCL,
        .gpio_sda = CONFIG_GPIO_I2C2_SDA,
        .clk_rate = CONFIG_GPIO_I2C2_CLK_RATE
        #endif
    },
    {
        #ifdef CONFIG_GPIO_I2C3
        .is_enable = 1,
        .i2c_bus_id = CONFIG_GPIO_I2C3_ID,
        .gpio_scl = CONFIG_GPIO_I2C3_SCL,
        .gpio_sda = CONFIG_GPIO_I2C3_SDA,
        .clk_rate = CONFIG_GPIO_I2C3_CLK_RATE
        #endif
    }
};

static inline void set_sda(struct gpio_i2c_drv* drv, int val)
{
    if (val)
        gpio_direction_input(drv->gpio_sda);
    else
        gpio_direction_output(drv->gpio_sda, 0);
}

static inline void set_scl(struct gpio_i2c_drv* drv, int val)
{
    if (val)
        gpio_direction_input(drv->gpio_scl);
    else
        gpio_direction_output(drv->gpio_scl, 0);
}

static inline int get_sda(struct gpio_i2c_drv* drv)
{
    return gpio_get_value(drv->gpio_sda);
}

static inline int get_scl(struct gpio_i2c_drv* drv)
{
    return gpio_get_value(drv->gpio_scl);
}

static inline void sda_low(struct gpio_i2c_drv* drv)
{
    unsigned int ns = (drv->time_ns + 1) / 2;

    os_enter_critical();
    unsigned int start = systick_get_time_ns();
    set_sda(drv, 0);
    os_exit_critical();

    while (systick_get_time_ns() - start < ns);
}

static inline void sda_high(struct gpio_i2c_drv* drv)
{
    unsigned int ns = (drv->time_ns + 1) / 2;

    os_enter_critical();
    unsigned int start = systick_get_time_ns();
    set_sda(drv, 1);
    os_exit_critical();

    while (systick_get_time_ns() - start < ns);
}

static inline void scl_low(struct gpio_i2c_drv* drv)
{
    unsigned int ns = (drv->time_ns) / 2;

    os_enter_critical();
    unsigned int start = systick_get_time_ns();
    set_scl(drv, 0);
    os_exit_critical();

    while (systick_get_time_ns() - start < ns);
}

static int scl_high(struct gpio_i2c_drv* drv)
{
    int timeout = systick_get_time_us();

    set_scl(drv, 1);
    while (!get_scl(drv)) {
        if ((systick_get_time_us() - timeout) > I2C_TIMEOUT_US) {
            if (get_scl(drv))
                break;
            return I2C_TIMEOUT;
        }
    }
    ndelay(drv->time_ns);

    return 0;
}

static void i2c_start(struct gpio_i2c_drv* drv)
{
    set_sda(drv,0);
    ndelay(drv->time_ns);
    scl_low(drv);
}

static void i2c_restart(struct gpio_i2c_drv* drv)
{
    sda_high(drv);
    scl_high(drv);
    set_sda(drv, 0);
    ndelay(drv->time_ns);
    scl_low(drv);
}

static void i2c_stop(struct gpio_i2c_drv* drv)
{
    sda_low(drv);
    scl_high(drv);
    set_sda(drv, 1);
    ndelay(drv->time_ns);
}

static int acknak(struct gpio_i2c_drv* drv, int ack)
{
    if (ack)
        sda_low(drv);

    if (scl_high(drv) < 0) {
        debug("i2c_bus%d gpio_i2c_read: ack/nak timeout\n",drv->i2c_bus_id);
        return I2C_TIMEOUT;
    }
    scl_low(drv);

    return 0;
}

static int i2c_send_byte(uint8_t txd, struct gpio_i2c_drv* drv)
{
    int ack, i;
    int data = txd;

    for(i = 0; i < 8; i++) {
        if (txd & 0x80)
            sda_high(drv);
        else
            sda_low(drv);
        txd <<= 1;

        if (scl_high(drv) < 0) {
            debug("i2c_bus%d i2c_send_byte: 0x%02x, timeout at bit %d \n",drv->i2c_bus_id, data, 7 - i);
            return I2C_TIMEOUT;
        }
        scl_low(drv);
    }

    sda_high(drv);
    if (scl_high(drv) < 0) {
        debug("i2c_bus%d i2c_send_byte: 0x%02x, timeout at ack \n",drv->i2c_bus_id, data);
        return I2C_TIMEOUT;
    }

    ack = get_sda(drv);
    scl_low(drv);
    return ack ? I2C_NO_ACK : 0;
}

static int gpio_i2c_write(struct gpio_i2c_drv* drv, struct i2c_msg *m)
{
    int retval;
    int wrcount = 0;
    int len = m->len;
    unsigned short nak_ok = m->flags & I2C_M_IGNORE_NAK;
    unsigned char *buf = m->buf;

    while (wrcount < len) {
        retval = i2c_send_byte(*buf++, drv);
        if (retval == I2C_TIMEOUT) {
            debug("i2c_bus%d gpio_i2c_write: error %d\n", drv->i2c_bus_id, retval);
            return I2C_TIMEOUT;
        } else if ((retval == I2C_NO_ACK) && !nak_ok) {
            debug("i2c_bus%d gpio_i2c_write: NAK bailout.\n", drv->i2c_bus_id);
            return I2C_NO_ACK;
        }
        wrcount++;
    }

    return wrcount;
}


static int i2c_read_byte(struct gpio_i2c_drv* drv)
{
    int i;
    int receive = 0;

    sda_high(drv);
    for(i = 0; i < 8; i++) {
        if (scl_high(drv) < 0) {
            debug("i2c_bus%d i2c_read_byte: timeout at bit %d\n",drv->i2c_bus_id, i);
            return I2C_TIMEOUT;
        }
        receive <<= 1;
        if(get_sda(drv))
            receive |= 0x01;

        scl_low(drv);
        if (i != 7)
            ndelay(drv->time_ns / 2);
    }

    return receive;
}

static int gpio_i2c_read(struct gpio_i2c_drv* drv, struct i2c_msg *m)
{
    int inval = 0;
    int rdcount = 0;
    int len = m->len;
    unsigned char *buf = m->buf;

    while (rdcount < len) {
        inval = i2c_read_byte(drv);
        if (inval < 0)
            break;

        *buf++ = inval;
        rdcount++;

        if (!(m->flags & I2C_M_NO_RD_ACK)) {
            inval = acknak(drv, rdcount < len);
            if (inval < 0)
                return inval;
        }
    }

    return rdcount;
}

static int gpio_i2c_try_address(struct gpio_i2c_drv* drv, unsigned char addr, int retries)
{
    int i,ret = 0;

    for (i = 0; i <= retries; i++) {
        ret = i2c_send_byte(addr, drv);
        if (ret == 0 || i == retries)
            break;

        i2c_stop(drv);
        ndelay(drv->time_ns);
        i2c_start(drv);
    }
    if (i && (ret != I2C_NO_ACK))
        debug("i2c_bus%d used %d tries to %s client at 0x%02x: %s\n", drv->i2c_bus_id, i + 1,
            addr & 1 ? "read from" : "write to", addr >> 1,
            ret == 0 ? "success" : "failed, timeout?");

    return ret;
}


static int gpio_i2c_set_addressing(struct gpio_i2c_drv* drv, enum i2c_addr_type addr_bit, unsigned short addr, int flag)
{
    int retries;
    unsigned short nak_ok = flag & I2C_M_IGNORE_NAK;
    int ret = 0;
    unsigned char  send_addr = 0;

    retries = nak_ok ? 0 : drv->retries;

    if (addr_bit == I2C_ADDR_BIT_10) {
        send_addr = ((addr >> 8) << 1) | 0xF0;
        ret = gpio_i2c_try_address(drv, send_addr, retries);
        if ((ret == I2C_NO_ACK) && !nak_ok) {
            debug("i2c_bus%d device addr 0x%02x noack at extended address code\n",drv->i2c_bus_id, addr);
            return I2C_NO_ACK;
        } else if (ret == I2C_TIMEOUT) {
            debug("i2c_bus%d device addr 0x%02x timeout at extended address code\n",drv->i2c_bus_id, addr);
            return I2C_TIMEOUT;
        }

        ret = i2c_send_byte(addr & 0xFF, drv);
        if ((ret == I2C_NO_ACK) && !nak_ok) {
            debug("i2c_bus%d device addr 0x%02x noack at 2nd address code\n",drv->i2c_bus_id, addr);
            return I2C_NO_ACK;
        } else if (ret == I2C_TIMEOUT) {
            debug("i2c_bus%d device addr 0x%02x timeout at 2nd address code\n",drv->i2c_bus_id, addr);
            return I2C_TIMEOUT;
        }

        if (flag & I2C_M_RD) {
            i2c_restart(drv);
            send_addr |= 0x01;
            ret = gpio_i2c_try_address(drv, send_addr, retries);
            if ((ret == I2C_NO_ACK) && !nak_ok) {
                debug("i2c_bus%d device addr 0x%02x noack at repeated address code\n",drv->i2c_bus_id, addr);
                return I2C_NO_ACK;
            } else if (ret == I2C_TIMEOUT) {
                debug("i2c_bus%d device addr 0x%02x timeout at repeated address code\n",drv->i2c_bus_id, addr);
                return I2C_TIMEOUT;
            }
        }
    } else {
        send_addr = addr << 1;
        if (flag & I2C_M_RD)
            send_addr |= 0x01;
        if (flag & I2C_M_REV_DIR_ADDR)
            send_addr ^= 0x01;
        ret = gpio_i2c_try_address(drv, send_addr, retries);
        if ((ret == I2C_NO_ACK) && !nak_ok) {
            debug("i2c_bus%d device addr 0x%02x noack at  address code\n",drv->i2c_bus_id, addr);
            return I2C_NO_ACK;
        } else if (ret == I2C_TIMEOUT) {
            debug("i2c_bus%d device addr 0x%02x timeout at  address code\n",drv->i2c_bus_id, addr);
            return I2C_TIMEOUT;
        }
    }

    return 0;
}

static struct i2c_device *ops_gpio_i2c_register(struct i2c_bus *bus, int i2c_bus_num, unsigned short addr, enum i2c_addr_type addr_bit, char *name)
{
    struct i2c_device *i2c = malloc(sizeof(struct i2c_device));

    assert(i2c);

    return i2c;
}

static int ops_gpio_i2c_transfer(struct i2c_bus *i2c_bus, struct i2c_device *i2c, struct i2c_msg *msg, int count)
{
    int i;
    int ret = 0;
    struct i2c_msg *m;
    unsigned short nak_ok;
    struct gpio_i2c_drv *drv = container_of(i2c_bus, struct gpio_i2c_drv, i2c_bus);

    i2c_start(drv);

    for (i = 0; i < count; i++) {
        m = msg + i;
        nak_ok = m->flags & I2C_M_IGNORE_NAK;

        if (!(m->flags & I2C_M_NOSTART)) {
            if (i)
                i2c_restart(drv);

            ret = gpio_i2c_set_addressing(drv, i2c->addr_bit,i2c->addr, m->flags);
            if ((ret == I2C_NO_ACK) && !nak_ok) {
                printf("NAK from i2c_bus%d device addr 0x%02x msg %d\n", drv->i2c_bus_id, i2c->addr,i);
                goto bailout;
            } else if (ret == I2C_TIMEOUT) {
                printf("i2c_bus%d device addr 0x%02x set addr timeout at msg %d\n", drv->i2c_bus_id, i2c->addr,i);
                goto bailout;
            }
        }

        if (m->flags & I2C_M_RD) {
            ret = gpio_i2c_read(drv, m);
            if (ret < m->len) {
                if (ret >= 0) {
                    printf("i2c_bus%d device addr 0x%02x transfer error!", drv->i2c_bus_id, i2c->addr);
                    ret = -EIO;
                }
                goto bailout;
            }
        } else {
            ret = gpio_i2c_write(drv, m);
            if (ret < m->len) {
                if (ret >= 0) {
                    printf("i2c_bus%d device addr 0x%02x transfer error!", drv->i2c_bus_id, i2c->addr);
                    ret = -EIO;
                }
                goto bailout;
            }
        }
    }
    ret = count;

bailout:
    i2c_stop(drv);

    return ret;
}

static void ops_gpio_i2c_unregister(struct i2c_bus *bus, struct i2c_device *i2c)
{
    free(i2c);
}

static int ops_gpio_i2c_detect_device(struct i2c_bus *i2c_bus, struct i2c_device *i2c)
{
    int ret;
    struct gpio_i2c_drv *drv = container_of(i2c_bus, struct gpio_i2c_drv, i2c_bus);

    i2c_start(drv);
    ret = gpio_i2c_set_addressing(drv, i2c->addr_bit,i2c->addr, I2C_M_RD);
    i2c_stop(drv);

    return ret;
}

static struct i2c_bus_ops i2c_bus_ops = {
    .ops_i2c_register = ops_gpio_i2c_register,
    .ops_i2c_unregister = ops_gpio_i2c_unregister,
    .ops_i2c_transfer = ops_gpio_i2c_transfer,
    .ops_i2c_detect_device = ops_gpio_i2c_detect_device,
};

static inline void gpio_init(int gpio, const char *name)
{
    assert(!gpio_request(gpio, name));
    gpio_direction_input(gpio);
}

static void gpio_i2c_init(struct gpio_i2c_drv *drv, int id)
{
    int ret;

    drv->retries = 3;
    drv->time_ns = 1e9 / drv->clk_rate / 2;

    if (gpio_is_valid(drv->gpio_scl))
        gpio_init(drv->gpio_scl, "i2c_scl");
    else
        panic("i2c_bus%d gpio_scl must specify pin\n", id);

    if (gpio_is_valid(drv->gpio_sda))
        gpio_init(drv->gpio_sda, "i2c_sda");
    else
        panic("i2c_bus%d gpio_sda must specify pin\n" ,id);

    sprintf(drv->name, "I2C%d", id);

    ret = i2c_bus_register(&drv->i2c_bus,
         id, drv->name, &i2c_bus_ops);
    assert(!ret);
}

void gpio_i2c_remove_bus(struct i2c_bus *bus)
{
    struct gpio_i2c_drv *drv = container_of(bus, struct gpio_i2c_drv, i2c_bus);
    i2c_bus_unregister(bus);

    gpio_release(drv->gpio_sda);
    gpio_release(drv->gpio_scl);

    if (drv->is_malloc == 1)
        free(drv);
}

struct i2c_bus* gpio_i2c_add_bus(struct gpio_i2c_bus_data *i2c)
{
    assert(i2c);
    struct gpio_i2c_drv *drv = malloc(sizeof(struct gpio_i2c_drv));
    assert(drv);

    drv->is_malloc = 1;
    drv->is_enable = 1;
    drv->i2c_bus_id = i2c->i2c_bus_id;
    drv->gpio_scl = i2c->gpio_scl;
    drv->gpio_sda = i2c->gpio_sda;
    drv->clk_rate = i2c->clk_rate;

    gpio_i2c_init(drv, drv->i2c_bus_id);

    return &drv->i2c_bus;
}

void gpio_i2c_init_driver(void)
{
    if (i2c_drv[0].is_enable)
        gpio_i2c_init(&i2c_drv[0], i2c_drv[0].i2c_bus_id);

    if (i2c_drv[1].is_enable)
        gpio_i2c_init(&i2c_drv[1], i2c_drv[1].i2c_bus_id);

    if (i2c_drv[2].is_enable)
        gpio_i2c_init(&i2c_drv[2], i2c_drv[2].i2c_bus_id);

    if (i2c_drv[3].is_enable)
        gpio_i2c_init(&i2c_drv[3], i2c_drv[3].i2c_bus_id);
}