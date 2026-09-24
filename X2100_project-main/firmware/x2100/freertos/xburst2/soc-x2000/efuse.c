#include <common.h>
#include <soc/base.h>
#include <soc/gpio.h>
#include <driver/gpio.h>
#include <driver/clk.h>
#include <assert.h>
#include <driver/hrtimer.h>
#include <os.h>
#include <wake_lock.h>
#include <driver/efuse.h>
#include <driver/gpio_pin.h>

#define EFUSE_CTRL      0x0
#define EFUSE_CFG       0x4
#define EFUSE_STATE     0x8
#define EFUSE_DATA(n)      (0xC+(n)*4)

/* efuse state bit */
#define EFUSE_STATE_NKU_PRT         (1 << 23)
#define EFUSE_STATE_USERKEY1_PRT    (1 << 22)
#define EFUSE_STATE_USERKEY0_PRT    (1 << 21)
#define EFUSE_STATE_CHIPKEY_PRT     (1 << 20)
#define EFUSE_STATE_HIDEBLK_PRT     (1 << 19)
#define EFUSE_STATE_SOCINFO_PRT     (1 << 18)
#define EFUSE_STATE_TRIM2_PRT       (1 << 17)
#define EFUSE_STATE_TRIM1_PRT       (1 << 16)
#define EFUSE_STATE_TRIM0_PRT       (1 << 15)
#define EFUSE_STATE_CUSTID2_PRT     (1 << 14)
#define EFUSE_STATE_CUSTID1_PRT     (1 << 13)
#define EFUSE_STATE_CUSTID0_PRT     (1 << 12)
#define EFUSE_STATE_CHIPID_PRT      (1 << 11)
#define EFUSE_STATE_SECBOOT_PRT     (1 << 10)

#define EFUSTATE_WR_DONE    1, 1
#define EFUSTATE_RD_DONE    0, 0

#define     CHIP_ID_ADDR            (0)
#define     CUSTOMER_ID0_ADDR       (CHIP_ID_ADDR + CHIP_ID_SIZE)
#define     CUSTOMER_ID1_ADDR       (CUSTOMER_ID0_ADDR + CUSTOMER_ID_SIZE0)
#define     CUSTOMER_ID2_ADDR       (CUSTOMER_ID1_ADDR + CUSTOMER_ID_SIZE1)
#define     TRIM_DATA0_ADDR         (CUSTOMER_ID2_ADDR + CUSTOMER_ID_SIZE2)
#define     TRIM_DATA1_ADDR         (TRIM_DATA0_ADDR + TRIM_DATA_SIZE0)
#define     TRIM_DATA2_ADDR         (TRIM_DATA1_ADDR + TRIM_DATA_SIZE1)
#define     SOC_INFO_ADDR           (TRIM_DATA2_ADDR + TRIM_DATA_SIZE2)
#define     PROGRAM_PROTECT_ADDR    (SOC_INFO_ADDR + SOC_INFO_SIZE)
#define     HIDE_BLOCK_ADDR         (PROGRAM_PROTECT_ADDR + PROGRAM_PROTECT_SIZE)
#define     CHIP_KEY_ADDR           (HIDE_BLOCK_ADDR + HIDE_BLOCK_SIZE)
#define     USER_KEY0_ADDR          (CHIP_KEY_ADDR + CHIP_KEY_SIZE)
#define     USER_KEY1_ADDR          (USER_KEY0_ADDR + USER_KEY_SIZE0)
#define     NKU_ADDR                (USER_KEY1_ADDR + USER_KEY_SIZE1)

#define EFUSE_TIMEOUT_US  (200*1000)

#ifdef CONFIG_X2000_EFUSE_VDDQ
static struct gpio_pin GPIO_EFUSE_POWER = {CONFIG_X2000_EFUSE_VDDQ};
#else
static struct gpio_pin GPIO_EFUSE_POWER = {-1,0};
#endif

#define EFUSE_ADDR(reg) (io_addr(EFUSE_IOBASE + reg))

static inline void efuse_write_reg(unsigned int reg, unsigned int value)
{
    *EFUSE_ADDR(reg) = value;
}

static inline unsigned int efuse_read_reg(unsigned int reg)
{
    return *EFUSE_ADDR(reg);
}

static inline void efuse_set_bit(unsigned int reg, int start, int end, unsigned int val)
{
    set_bit_field_v(EFUSE_ADDR(reg), start, end, val);
}

static inline unsigned int efuse_get_bit(unsigned int reg, int start, int end)
{
    return get_bit_field_v(EFUSE_ADDR(reg), start, end);
}

static DEFINE_MUTEX(lock);

static struct wake_lock w_lock;

struct seg_info {
    unsigned int seg_start; /* 段在efuse中的起始字节 */
    unsigned int seg_size;  /* 段大小，单位Byte */
    unsigned int prt_bit;   /* 段对应的保护位 */
};

static struct seg_info segments[]  = {
    [CHIP_ID] =         {CHIP_ID_ADDR, CHIP_ID_SIZE, EFUSE_STATE_CHIPID_PRT},
    [CUSTOMER_ID0] =    {CUSTOMER_ID0_ADDR, CUSTOMER_ID_SIZE0, EFUSE_STATE_CUSTID0_PRT},
    [CUSTOMER_ID1] =    {CUSTOMER_ID1_ADDR, CUSTOMER_ID_SIZE1, EFUSE_STATE_CUSTID1_PRT},
    [CUSTOMER_ID2] =    {CUSTOMER_ID2_ADDR, CUSTOMER_ID_SIZE2, EFUSE_STATE_CUSTID2_PRT},
    [TRIM_DATA0] =      {TRIM_DATA0_ADDR, TRIM_DATA_SIZE0, EFUSE_STATE_TRIM0_PRT},
    [TRIM_DATA1] =      {TRIM_DATA1_ADDR, TRIM_DATA_SIZE1, EFUSE_STATE_TRIM1_PRT},
    [TRIM_DATA2] =      {TRIM_DATA2_ADDR, TRIM_DATA_SIZE2, EFUSE_STATE_TRIM2_PRT},
    [SOC_INFO] =        {SOC_INFO_ADDR, SOC_INFO_SIZE, EFUSE_STATE_SOCINFO_PRT},
    [PROGRAM_PROTECT] = {PROGRAM_PROTECT_ADDR, PROGRAM_PROTECT_SIZE},
    [HIDE_BLOCK] =      {HIDE_BLOCK_ADDR, HIDE_BLOCK_SIZE, EFUSE_STATE_HIDEBLK_PRT},
    [CHIP_KEY] =        {CHIP_KEY_ADDR, CHIP_KEY_SIZE},
    [USER_KEY0] =       {USER_KEY0_ADDR, USER_KEY_SIZE0},
    [USER_KEY1] =       {USER_KEY1_ADDR, USER_KEY_SIZE1},
    [NKU] =             {NKU_ADDR, NKU_SIZE},
};

static void jz_efuse_enable_read(unsigned int addr)
{
    unsigned int val;
    efuse_write_reg(EFUSE_STATE, 0);
    val = addr << 21;
    efuse_write_reg(EFUSE_CTRL, val);
    val |= 1;
    efuse_write_reg(EFUSE_CTRL, val);
}

static unsigned int jz_efuse_read_word(unsigned int start)
{
    unsigned int addr = start / 4;

    jz_efuse_enable_read(addr);

    while (!efuse_get_bit(EFUSE_STATE, EFUSTATE_RD_DONE));

    return efuse_read_reg(EFUSE_DATA(0));
}

int soc_efuse_read_segment(enum segment_id seg_id, unsigned char *buf, int len)
{
    unsigned int data = 0;
    int start;

    if (seg_id < CHIP_ID || seg_id > NKU)
        panic("EFUSE: segment num should be (0 ~ 13)\n");

    mutex_lock(&lock);

    wake_lock(&w_lock);

    start = segments[seg_id].seg_start;

    if (start % 4) {
        int offset = start % 4;
        int n = 4 - offset;

        data = jz_efuse_read_word(start);

        char *tmp_buf = (void *)&data;

        if (len < n)
            n = len;

        memcpy(buf, tmp_buf + offset, n);

        start += n;
        buf += n;
        len -= n;
    }

    while (len >= 4) {
        data = jz_efuse_read_word(start);

        char *tmp_buf = (void *)&data;

        memcpy(buf, tmp_buf, 4);

        start += 4;
        len -= 4;
        buf += 4;
    }

    if (len) {
        data = jz_efuse_read_word(start);

        char *tmp_buf = (void *)&data;

        memcpy(buf, tmp_buf, len);
    }

    efuse_write_reg(EFUSE_STATE, 0);

    wake_unlock(&w_lock);

    mutex_unlock(&lock);

    return 0;
}

int soc_efuse_read(enum segment_id seg_id, unsigned char *buf, int start, int size)
{
    unsigned char save_buf[104] = {0};

    int len = segments[seg_id].seg_size;

    if ((start + size) > len)
        panic("EFUSE: operate segment %d data length size should <= %d Byte", seg_id, len);

    soc_efuse_read_segment(seg_id, save_buf, len);

    for (int i = start; i < (start + size); i++)
        *buf++ = save_buf[i];

    return 0;
}

static void jz_efuse_write_word(unsigned int start, unsigned int data)
{
    unsigned int val = 0;

    unsigned int addr = start / 4;

    efuse_write_reg(EFUSE_DATA(0), data);

    val = addr << 21 | ((1 - 1) << 16) |1 << 15;
    val |= 1 << 9;          // pass 1.8V power to internal for program
    val &= ~(1 << 8);       // power up
    efuse_write_reg(EFUSE_CTRL, val);

    gpio_pin_output_enable(GPIO_EFUSE_POWER);

    unsigned int time = systick_get_time_us();

    udelay(10);
    val |= 2;
    efuse_write_reg(EFUSE_CTRL, val);

    while (!efuse_get_bit(EFUSE_STATE, EFUSTATE_WR_DONE)) {
        if ((systick_get_time_us() - time) > EFUSE_TIMEOUT_US) {
            printf("EFUSE write word timeout\n");
            break;
        }
    }

    gpio_pin_output_disable(GPIO_EFUSE_POWER);

    efuse_write_reg(EFUSE_CTRL, 0);
    efuse_write_reg(EFUSE_STATE, 0);

    val = 1 << 8;      // power down
    efuse_write_reg(EFUSE_CTRL, val);
}

int soc_efuse_write_segment(enum segment_id seg_id, unsigned char *buf, int len)
{
    unsigned int data = 0;
    int start;
    if (seg_id < CHIP_ID || seg_id > NKU)
        panic("EFUSE: segment num should be (0 ~ 13)\n");

    if (!gpio_pin_is_valid(GPIO_EFUSE_POWER))
        printf("EFUSE: efuse vddq gpio not set\n");

    mutex_lock(&lock);

    wake_lock(&w_lock);

    start = segments[seg_id].seg_start;

    if (start % 4) {
        /* 写操作每次要写一个字，由于每段的第一个字节可能不是字对齐的，
         * 就会出现该段与上一段处交叉在同一个字中的情况。
         * 这样在写该段的第一个字时，要将这个32位中不属于当该段的位清0(写0到efuse 相当于等于不操作)
        */
        data = 0;

        int offset = start % 4;
        int n = 4 - offset;

        if (len < n)
            n = len;

        for (int i = offset; i < 4; i++) {
            data |= (*buf << (i * 8));
            buf++;
        }

        jz_efuse_write_word(start, data);

        start += n;
        len -= n;
    }

    while (len >= 4) {
        data = 0;
        for (int i = 0; i < 4; i++) {
            data |= (*buf << (i * 8));
            buf++;
        }

        jz_efuse_write_word(start, data);

        start += 4;
        len -= 4;
    }

    if (len) {
        data = 0;
        for (int i = 0; i < len; i++) {
            data |= (*buf << (i * 8));
            buf++;
        }

        jz_efuse_write_word(start, data);
    }

    efuse_write_reg(EFUSE_CTRL, 0);
    efuse_write_reg(EFUSE_STATE, 0);

    wake_unlock(&w_lock);

    mutex_unlock(&lock);

    return 0;

}

int soc_efuse_write(enum segment_id seg_id, unsigned char *buf, int start, int size)
{
    unsigned char save_buf[104] = {0};

    if (seg_id != PROGRAM_PROTECT) {
        if (efuse_read_reg(EFUSE_STATE) & segments[seg_id].prt_bit) {
            panic("EFUSE: segment %d have been protected\n", seg_id);
        }
    }

    int len = segments[seg_id].seg_size;

    if ((start + size) > len)
        panic("EFUSE: operate segment %d data length size should <= %d Byte", seg_id, len);

    soc_efuse_read_segment(seg_id, save_buf, len);

    for (int i = start; i < (start + size); i++)
        save_buf[i] = *buf++;

    soc_efuse_write_segment(seg_id, save_buf, len);

    return 0;
}

void soc_efuse_init_driver(void)
{
    unsigned int tmp;
    struct clk *h2clk;
    int rd_strobe, wr_strobe;
    unsigned int rd_adj, wr_adj;
    unsigned long rate, ns;

    struct clk *efuse_clk = clk_get("gate_efuse");
    assert(efuse_clk);
    clk_enable(efuse_clk);

    h2clk = clk_get("h2clk");
    assert(h2clk);

    rate = clk_get_rate(h2clk);
    ns = 1000000000 / rate;

    if (gpio_pin_is_valid(GPIO_EFUSE_POWER)) {
        gpio_pin_request(GPIO_EFUSE_POWER, "efuse_vddq");
        gpio_pin_output_disable(GPIO_EFUSE_POWER);
    }

    wake_lock_init(&w_lock, "efuse_wake_lock");

    rd_adj = 4 / ns;
    if ((rd_adj + 1) * ns <= 4) {
        panic("EFUSE: get efuse cfg rd_adj fail!\n");
    }

    wr_adj = 4 / ns;
    if ((wr_adj + 1) * ns <= 4) {
        panic("EFUSE: get efuse cfg wr_adj fail!\n");
    }

    rd_strobe = 100 / ns - rd_adj - 29;
    if (rd_strobe < 0)
        rd_strobe = 0;
    if ((rd_adj + rd_strobe + 30) * ns <= 100) {
        panic("EFUSE: get efuse cfg rd_strobe fail!\n");
    }

    wr_strobe = 12000 / ns - wr_adj - 2999;
    if (wr_strobe < 0)
        wr_strobe = 0;
    tmp = (wr_adj + 3000 + wr_strobe) * ns;
    if (tmp > 13000 || tmp < 11000)
        panic("EFUSE: get efuse cfg wr_strobe fail!\n");

    tmp = rd_adj << 24 | rd_strobe << 16 | wr_adj << 12 | wr_strobe;

    efuse_write_reg(EFUSE_CFG, tmp);
}