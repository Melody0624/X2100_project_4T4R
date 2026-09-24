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

#include "hamming.c"

#define EFUSE_CTRL      0x0
#define EFUSE_CFG       0x4
#define EFUSE_STATE     0x8
#define EFUSE_DATA(n)      (0xC+(n)*4)

#define EFUSE_RIR_RF                 (0)
#define EFUSE_RIR_DATA               (1)
#define EFUSE_RIR_ADDR               (2)
#define EFUSE_RIR_DISABLE            (15)

// efuse ctrl bits
#define EFUSE_CTRL_ADDR              (21)
#define EFUSE_CTRL_ADDR_MASK         (0x3f)
#define EFUSE_CTRL_LEN               (16)
#define EFUSE_CTRL_LEN_MASK          (7)
#define EFUSE_CTRL_PGEN              (1 << 15)
#define EFUSE_CTRL_RWL               (1 << 11)
#define EFUSE_CTRL_MR                (1 << 10)
#define EFUSE_CTRL_PS                (1 << 9)
#define EFUSE_CTRL_PD                (1 << 8)
#define EFUSE_CTRL_WREN              (1 << 1)
#define EFUSE_CTRL_RDEN              (1 << 0)

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

#define     CHIP_ID_ADDR            (0x0)
#define     CUSTOMER_ID0_ADDR       (0x11)
#define     CUSTOMER_ID1_ADDR       (0x22)
#define     CUSTOMER_ID2_ADDR       (0x3f)
#define     TRIM_DATA0_ADDR         (0x5c)
#define     TRIM_DATA1_ADDR         (0x61)
#define     TRIM_DATA2_ADDR         (0x66)
#define     SOC_INFO_ADDR           (0x6b)
#define     PROGRAM_PROTECT_ADDR    (0x70)
#define     HIDE_BLOCK_ADDR         (0x74)
#define     CHIP_KEY_ADDR           (0x78)
#define     USER_KEY0_ADDR          (0x9a)
#define     USER_KEY1_ADDR          (0xbc)
#define     NKU_ADDR                (0xde)

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

enum verify_mode {
    NONE = 0,
    DOUBLE,
    HAMMING,
};

struct seg_info {
    unsigned int seg_start; /* 段在efuse中的起始字节 */
    unsigned int seg_size;  /* 段大小，单位Byte */
    enum verify_mode mode;  /* 段信息的编码方式 */
    unsigned int seg_extra_size;   /* 每段中使用hamming编码时额外空间的大小，单位Byte */
    unsigned int prt_bit;   /* 段对应的保护位 */
};

static struct seg_info segments[]  = {
    [CHIP_ID] =         {CHIP_ID_ADDR, CHIP_ID_SIZE, HAMMING, 1, EFUSE_STATE_CHIPID_PRT},
    [CUSTOMER_ID0] =    {CUSTOMER_ID0_ADDR, CUSTOMER_ID_SIZE0, HAMMING, 1, EFUSE_STATE_CUSTID0_PRT},
    [CUSTOMER_ID1] =    {CUSTOMER_ID1_ADDR, CUSTOMER_ID_SIZE1, HAMMING, 2, EFUSE_STATE_CUSTID1_PRT},
    [CUSTOMER_ID2] =    {CUSTOMER_ID2_ADDR, CUSTOMER_ID_SIZE2, HAMMING, 2, EFUSE_STATE_CUSTID2_PRT},
    [TRIM_DATA0] =      {TRIM_DATA0_ADDR, TRIM_DATA_SIZE0, HAMMING, 1, EFUSE_STATE_TRIM0_PRT},
    [TRIM_DATA1] =      {TRIM_DATA1_ADDR, TRIM_DATA_SIZE1, HAMMING, 1, EFUSE_STATE_TRIM1_PRT},
    [TRIM_DATA2] =      {TRIM_DATA2_ADDR, TRIM_DATA_SIZE2, NONE, 1, EFUSE_STATE_TRIM2_PRT},
    [SOC_INFO] =        {SOC_INFO_ADDR, SOC_INFO_SIZE, DOUBLE, 2, EFUSE_STATE_SOCINFO_PRT},
    [PROGRAM_PROTECT] = {PROGRAM_PROTECT_ADDR, PROGRAM_PROTECT_SIZE, DOUBLE, 2},
    [HIDE_BLOCK] =      {HIDE_BLOCK_ADDR, HIDE_BLOCK_SIZE, HAMMING, 2, EFUSE_STATE_HIDEBLK_PRT},
    [CHIP_KEY] =        {CHIP_KEY_ADDR, CHIP_KEY_SIZE, HAMMING, 2},
    [USER_KEY0] =       {USER_KEY0_ADDR, USER_KEY_SIZE0, HAMMING, 2},
    [USER_KEY1] =       {USER_KEY1_ADDR, USER_KEY_SIZE1, HAMMING, 2},
    [NKU] =             {NKU_ADDR, NKU_SIZE, HAMMING, 2},
};

static void jz_efuse_enable_read(unsigned int addr)
{
    unsigned int val;
    efuse_write_reg(EFUSE_STATE, 0);
    val = addr << EFUSE_CTRL_ADDR | (1 - 1) << EFUSE_CTRL_LEN;
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

static void rir_w(unsigned int addr, unsigned int value)
{
    unsigned int val;

    efuse_write_reg(EFUSE_DATA(0), value);
    efuse_write_reg(EFUSE_CTRL, 0);

    val =  addr << EFUSE_CTRL_ADDR | (1 - 1) << EFUSE_CTRL_LEN;
    efuse_write_reg(EFUSE_CTRL, val);

    val = efuse_read_reg(EFUSE_CTRL);
    val &= ~EFUSE_CTRL_PD;
    efuse_write_reg(EFUSE_CTRL, val);

    val = efuse_read_reg(EFUSE_CTRL);
    val |= EFUSE_CTRL_PS | EFUSE_CTRL_RWL;
    efuse_write_reg(EFUSE_CTRL, val);

    val = efuse_read_reg(EFUSE_CTRL);
    val |= EFUSE_CTRL_PGEN;
    efuse_write_reg(EFUSE_CTRL, val);

    /* connect VDDQ pin from 1.8V. */
    gpio_pin_output_enable(GPIO_EFUSE_POWER);

    /* wait write done status */
    uint64_t time = systick_get_time_us();

    /* Write EFUSE enable */
    val = efuse_read_reg(EFUSE_CTRL);
    val |= EFUSE_CTRL_WREN;
    efuse_write_reg(EFUSE_CTRL, val);

    /* Wait write EFUSE */
    while (!efuse_get_bit(EFUSE_STATE, EFUSTATE_WR_DONE)) {
        if (systick_get_time_us() - time > EFUSE_TIMEOUT_US) {
            printf("EFUSE write word timeout\n");
            break;
        }
    }
    /* Disconnect VDDQ pin from 1.8V. */
    gpio_pin_output_disable(GPIO_EFUSE_POWER);

    efuse_write_reg(EFUSE_CTRL, 0);
    efuse_write_reg(EFUSE_CTRL, EFUSE_CTRL_PD);
}

static void rir_r(void)
{
    unsigned int val;

    efuse_write_reg(EFUSE_CTRL, 0);
    efuse_write_reg(EFUSE_DATA(0), 0);
    efuse_write_reg(EFUSE_DATA(1), 0);

    /* set rir read address and data length */
    val =  0x1f << EFUSE_CTRL_ADDR | (2 - 1) << EFUSE_CTRL_LEN;
    efuse_write_reg(EFUSE_CTRL, val);

    val = efuse_read_reg(EFUSE_CTRL);
    val &= ~EFUSE_CTRL_PD;
    efuse_write_reg(EFUSE_CTRL, val);

    val = efuse_read_reg(EFUSE_CTRL);
    val &= ~(EFUSE_CTRL_PS);
    efuse_write_reg(EFUSE_CTRL, val);

    val = efuse_read_reg(EFUSE_CTRL);
    val |= (EFUSE_CTRL_RWL);
    efuse_write_reg(EFUSE_CTRL, val);

    val = efuse_read_reg(EFUSE_CTRL);
    val |= EFUSE_CTRL_RDEN;
    efuse_write_reg(EFUSE_CTRL, val);

    /* wait read done status */
    while (!efuse_get_bit(EFUSE_STATE, EFUSTATE_RD_DONE));

    // printf("EFUSE: RIR0=0x%08x\n", efuse_read_reg(EFUSE_DATA(0)));
    // printf("EFUSE: RIR1=0x%08x\n", efuse_read_reg(EFUSE_DATA(1)));
}

static int rir_op(unsigned int value, unsigned int flag)
{
    unsigned int addr = 0, rf_addr = 0;
    unsigned int fb_disable = 0;
    unsigned int ret1, ret2;
    int rir_num = 0;

    if(value == 0)
        return -1;

    rir_r();
    ret1 = efuse_read_reg(EFUSE_DATA(0));
    ret2 = efuse_read_reg(EFUSE_DATA(1));

    if((ret1 & 0xFFFF) && (ret1 & (0xFFFF << 16)) &&
            (ret2 & 0xFFFF) && (ret2 & (0xFFFF << 16))) {
        if(flag == 1) {
            fb_disable = 0x1 << 31;
            rf_addr = 0x20;
            rir_w(rf_addr, fb_disable);
        }
        printf("EFUSE: not redundancy bits!\n");
        return -1;
    }

    if(((ret1 & (0xFFFF)) && (ret1 & (0xFFFF << 16)))) {
        addr = 0x20;
        if(ret2 & 0xFFFF) {
            value = value << 16;
            rir_num = 4;
        } else {
            rir_num = 3;
        }
    } else {
        addr = 0;
        if(ret1 & 0xFFFF) {
            value = value << 16;
            rir_num = 2;
        } else {
            rir_num = 1;
        }
    }

    if(flag == 1) {
        switch(rir_num) {
        case 2:
            fb_disable = 0x1 << 15;
            rf_addr = 0x0;
            break;
        case 3:
            fb_disable = 0x1 << 31;
            rf_addr = 0x0;
            break;
        case 4:
            fb_disable = 0x1 << 15;
            rf_addr = 0x20;
            break;
        default:
            printf("not rir %d!\n", rir_num);
            return -1;
        }

        rir_w(rf_addr, fb_disable);
    }

    rir_w(addr, value);

    return 0;
}

static int rir_check(unsigned int addr, uint32_t val)
{
    int rval, errbits;

    rir_r();
    rval = jz_efuse_read_word(addr);
    errbits = rval ^ val;
    return errbits;
}

static int rir_repair(int seg_id, unsigned int addr, unsigned int data)
{
    unsigned int errbits, rir_data, repair_result, repair_fail;
    unsigned int ebit, ret;
    if (segments[seg_id].mode != HAMMING)
        return 0;

    errbits = rir_check(addr, data);
    // printf("word_addr=%x, errbits=0x%08x\n", addr / 4, errbits);

    while((ebit = ffs(errbits)) > 0) {
        rir_data = 0x1 << EFUSE_RIR_RF;
        rir_data |= (data & ebit) << EFUSE_RIR_DATA;
        rir_data |= (addr / 4 + ((ebit + (addr % 4) * 8) << 6)) << EFUSE_RIR_ADDR;
        // rir_data &= 0 << EFUSE_RIR_DISABLE;

        ret = rir_op(rir_data, 0);
        if(ret) {
            printf("EFUSE: rir repair failed!\n");
            return -1;
        }

        do {
            repair_result = rir_check(addr, data);
            repair_fail = repair_result & (0x1 << ebit);
            if(repair_fail) {
                ret = rir_op(rir_data, 1);
                if(ret) {
                    printf("EFUSE: rir repair failed!\n");
                    return -1;
                }
            }
        } while(repair_fail);
        errbits &= 0 << ebit;
    }

    return 0;
}

// static void rir_disable_all(void)
// {
//     rir_r();
//     rir_w(0x0, (1 << 15));
//     rir_w(0x0, (1 << 31));
//     rir_w(0x20, (1 << 15));
//     rir_w(0x20, (1 << 31));
// }

int soc_efuse_read_segment(enum segment_id seg_id, unsigned char *buf, int len)
{
    unsigned int data = 0;
    int start;
    int ret;
    int hamming_bit_num = 0;
    int segment_len;
    unsigned int val[8] = {0};
    unsigned char *pbuf = (unsigned char *)val;
    unsigned int hamming_buf[8] = {0};

    if (seg_id < CHIP_ID || seg_id > NKU)
        panic("EFUSE: segment num should be (0 ~ 13)\n");

    mutex_lock(&lock);

    wake_lock(&w_lock);

    segment_len = len + segments[seg_id].seg_extra_size;

    start = segments[seg_id].seg_start;

    // redundancy read mode
    rir_r();

    if (start % 4) {
        int offset = start % 4;
        int n = 4 - offset;

        data = jz_efuse_read_word(start);

        char *tmp_buf = (void *)&data;

        if (segment_len < n)
            n = segment_len;

        memcpy(pbuf, tmp_buf + offset, n);

        start += n;
        pbuf += n;
        segment_len -= n;
    }

    while (segment_len >= 4) {
        data = jz_efuse_read_word(start);

        char *tmp_buf = (void *)&data;

        memcpy(pbuf, tmp_buf, 4);

        start += 4;
        segment_len -= 4;
        pbuf += 4;
    }

    if (segment_len) {
        data = jz_efuse_read_word(start);

        char *tmp_buf = (void *)&data;

        memcpy(pbuf, tmp_buf, segment_len);
    }

    switch (segments[seg_id].mode) {
    case HAMMING:
        hamming_bit_num = len * 8 + cal_k(len * 8);
        decode(val, hamming_bit_num, hamming_buf);
        memcpy(buf, (char *)hamming_buf, len);
        break;
    case DOUBLE:
        segment_len = len + segments[seg_id].seg_extra_size;
        ret = checkbit(val, val, 0, (segment_len % 2) * segment_len * 8 / 2, segment_len * 8 / 2);
        if (ret)
            printf("EFUSE: double verify failed! data maybe corrupted\n");
        pbuf = (unsigned char *)val;
        memcpy(buf, pbuf, len);
        if (segment_len % 2)
            buf[len - 1] &= 0x0f;
        break;
    case NONE:
    default:
        memcpy(buf, (unsigned char *)val, len);
        break;
    }

    efuse_write_reg(EFUSE_STATE, 0);
    efuse_write_reg(EFUSE_CTRL, EFUSE_CTRL_PD);

    wake_unlock(&w_lock);

    mutex_unlock(&lock);

    return 0;
}

int soc_efuse_read(enum segment_id seg_id, unsigned char *buf, int start, int size)
{
    unsigned char save_buf[32] = {0};

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

    val = addr << EFUSE_CTRL_ADDR | ((1 - 1) << EFUSE_CTRL_LEN) | EFUSE_CTRL_PGEN;
    val |= EFUSE_CTRL_PS;         // pass 1.8V power to internal for program
    val &= ~EFUSE_CTRL_PD;        // power up
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

    val = EFUSE_CTRL_PD;      // power down
    efuse_write_reg(EFUSE_CTRL, val);
}

int soc_efuse_write_segment(enum segment_id seg_id, unsigned char *buf, int len)
{
    int start, i;
    int segment_len;
    int value_bits;
    int ret;
    unsigned int data = 0;
    unsigned int val[8] = {0};
    unsigned char *pbuf = (unsigned char *)val;

    if (seg_id < CHIP_ID || seg_id > NKU)
        panic("EFUSE: segment num should be (0 ~ 13)\n");

    if (len != segments[seg_id].seg_size)
        panic("EFUSE: operate segment %d data length != %d Byte\n", seg_id, segments[seg_id].seg_size);

    segment_len = len + segments[seg_id].seg_extra_size;

    if (!gpio_pin_is_valid(GPIO_EFUSE_POWER))
        printf("EFUSE: efuse vddq gpio not set\n");

    switch (segments[seg_id].mode) {
    case HAMMING:
        encode((unsigned int *)buf, len * 8, val);
        break;
    case DOUBLE:
        value_bits = segment_len * 8 / 2;
        if (segment_len % 2) {
            if (buf[len - 1] & 0xf0) {
                printf("EFUSE: operate segment %d can only write %d bits\n", seg_id, value_bits);
                return -1;
            }
        }
        memcpy(pbuf, buf, len);

        // double the data
        for (i = 0; i < value_bits; i ++)
            if (pbuf[i / 8] & (1 << i % 8))
                pbuf[(i + value_bits) / 8] |= 1 << ((i + value_bits) % 8);
        break;
    case NONE:
    default:
        memcpy(pbuf, buf, len);
        break;
    }

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

        if (segment_len < n)
            n = segment_len;

        for (i = offset; i < 4; i++) {
            data |= (*pbuf << (i * 8));
            pbuf++;
        }

        jz_efuse_write_word(start, data);
        ret = rir_repair(seg_id, start, data);
        if (ret < 0) {
            printf("EFUSE: hamming verify failed!\n");
            return -1;
        }

        start += n;
        segment_len -= n;
    }

    while (segment_len >= 4) {
        data = 0;
        for (i = 0; i < 4; i++) {
            data |= (*pbuf << (i * 8));
            pbuf++;
        }

        jz_efuse_write_word(start, data);
        ret = rir_repair(seg_id, start, data);
        if (ret < 0) {
            printf("EFUSE: hamming verify failed!\n");
            return -1;
        }

        start += 4;
        segment_len -= 4;
    }

    if (segment_len) {
        data = 0;
        for (i = 0; i < segment_len; i++) {
            data |= (*pbuf << (i * 8));
            pbuf++;
        }

        jz_efuse_write_word(start, data);
        ret = rir_repair(seg_id, start, data);
        if (ret < 0) {
            printf("EFUSE: hamming verify failed!\n");
            return -1;
        }
    }

    efuse_write_reg(EFUSE_CTRL, 0);
    efuse_write_reg(EFUSE_STATE, 0);

    wake_unlock(&w_lock);

    mutex_unlock(&lock);

    return 0;

}

int soc_efuse_write(enum segment_id seg_id, unsigned char *buf, int start, int size)
{
    unsigned char save_buf[32] = {0};

    int i;
    int len = segments[seg_id].seg_size;

    if (seg_id != PROGRAM_PROTECT) {
        if (efuse_read_reg(EFUSE_STATE) & segments[seg_id].prt_bit) {
            panic("EFUSE: segment %d have been protected\n", seg_id);
        }
    }

    if ((start + size) > len)
        panic("EFUSE: operate segment %d data length size should <= %d Byte\n", seg_id, len);

    soc_efuse_read_segment(seg_id, save_buf, len);

    if (segments[seg_id].mode == HAMMING) {
        if (size != len)
            panic("EFUSE: operate segment %d using HAMMING CODE, write size must equal to segment size %d\n", seg_id, len);
        for (i = 0; i < len; i ++) {
            if (save_buf[i] != 0)
                panic("EFUSE: operate segment %d using HAMMING CODE, can only write once\n", seg_id);
        }
    } else {
        for (i = start; i < (start + size); i++)
            save_buf[i] = *buf++;
    }

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
