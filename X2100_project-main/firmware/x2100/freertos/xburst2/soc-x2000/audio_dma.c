#include <driver/cache.h>
#include <driver/irq.h>
#include <driver/clk.h>
#include "common.h"
#include "spinlock.h"
#include "audio_regs.h"
#include "audio_dma.h"

struct dmadesc {
    unsigned long cmd;
    unsigned long addr;
    unsigned long next_desc_off;
    unsigned long trans_count;
};

static DEFINE_SPINLOCK(lock);

static struct clk *audio_clk;
static struct clk *audio_ram_clk;

static void (*callbacks[10])(void *data);
static void *datas[10];

static inline void set_src_dev_slot(int id, int slot_id)
{
    int reg = 0;
    int start = 0;
    static char array[][3] = {
        [0] = {BST0_OFF, DEV0_SUR},
        [1] = {BST0_OFF, DEV1_SUR},
        [2] = {BST0_OFF, DEV2_SUR},
        [3] = {BST0_OFF, DEV3_SUR},
        [4] = {BST0_OFF, DEV4_SUR},
        [5] = {BST0_OFF, DEV5_SUR},
        [6] = {BST1_OFF, DEV6_SUR},
        [7] = {BST1_OFF, DEV7_SUR},
        [8] = {BST1_OFF, DEV8_SUR},
        [9] = {BST1_OFF, DEV9_SUR},
        [10] = {BST1_OFF, DEV10_SUR},
        [11] = {BST2_OFF, DEV11_SUR},
        [12] = {BST2_OFF, DEV12_SUR},
        [13] = {BST2_OFF, DEV13_SUR},
        [14] = {BST2_OFF, DEV14_SUR},
    };

    reg = IBUS_OFF + array[id][0];
    start = array[id][1];
    int end = array[id][2];

    audio_set_bit(reg, start, end, slot_id);
}

static inline void set_tar_dev_slot(int id, int slot_id)
{
    static char array[][3] = {
        [0] = {BTT0_OFF, DEV0_TAR},
        [1] = {BTT0_OFF, DEV1_TAR},
        [2] = {BTT0_OFF, DEV2_TAR},
        [3] = {BTT0_OFF, DEV3_TAR},
        [4] = {BTT0_OFF, DEV4_TAR},
        [5] = {BTT0_OFF, DEV5_TAR},
        [6] = {BTT1_OFF, DEV6_TAR},
        [7] = {BTT1_OFF, DEV7_TAR},
        [8] = {BTT1_OFF, DEV8_TAR},
        [9] = {BTT1_OFF, DEV9_TAR},
        [10] = {BTT1_OFF, DEV10_TAR},
        [11] = {BTT1_OFF, DEV11_TAR},
    };

    int reg = IBUS_OFF + array[id][0];
    int start = array[id][1];
    int end = array[id][2];

    audio_set_bit(reg, start, end, slot_id);
}

static unsigned int dma_stat;

enum audio_dev_id audio_requst_src_dma_dev(void)
{
    int i;
    enum audio_dev_id dev_id = 0;
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

    for (i = Dev_src_dma0; i <= Dev_src_dma4; i++) {
        if (!(dma_stat & (1 << i))) {
            dev_id = i;
            dma_stat |= (1 << i);
            break;
        }
    }

    spin_unlock_irqrestore(&lock, flags);

    return dev_id;
}

#ifdef DEBUG
static inline void aic_dma_dump_regs(void)
{
    int id;
    for (id = 0; id < 10; id ++ ) {
        printf("DBA(%d): 0x%x\n", id, audio_read_reg(DBA(id)));
        printf("DTC(%d): 0x%x\n", id, audio_read_reg(DTC(id)));
        printf("DCR(%d): 0x%x\n", id, audio_read_reg(DCR(id)));
        printf("DSR(%d): 0x%x\n", id, audio_read_reg(DSR(id)));
        printf("DCM(%d): 0x%x\n", id, audio_read_reg(DCM(id)));
        printf("DDA(%d): 0x%x\n", id, audio_read_reg(DDA(id)));

        printf("FAS(%d): 0x%x\n", id, audio_read_reg(FAS(id)));
        printf("FCR(%d): 0x%x\n", id, audio_read_reg(FCR(id)));
        printf("FFR(%d): 0x%x\n", id, audio_read_reg(FFR(id)));
        printf("FSR(%d): 0x%x\n", id, audio_read_reg(FSR(id)));

    }

    printf("DGRR: 0x%x\n", audio_read_reg(DGRR));
    printf("DGER: 0x%x\n", audio_read_reg(DGER));
    printf("AEER: 0x%x\n", audio_read_reg(AEER));
    printf("AESR: 0x%x\n", audio_read_reg(AESR));
    printf("AIPR: 0x%x\n", audio_read_reg(AIPR));
}
#endif

enum audio_dev_id audio_requst_tar_dma_dev(void)
{
    int i;
    enum audio_dev_id dev_id = 0;
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

    /* dma5 为 dmic 专用, 所以这里从dma6 到 dma9
     */
    for (i = Dev_tar_dma6; i <= Dev_tar_dma9; i++) {
        if (!(dma_stat & (1 << i))) {
            dev_id = i;
            dma_stat |= (1 << i);
            break;
        }
    }

    spin_unlock_irqrestore(&lock, flags);

    return dev_id;
}

void audio_release_dma_dev(enum audio_dev_id dev_id)
{
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

    dma_stat &= ~(1 << dev_id);

    spin_unlock_irqrestore(&lock, flags);
}

void audio_connect_dev(enum audio_dev_id src_id, enum audio_dev_id tar_id)
{
    int slot_id = dev_to_slot_id(src_id);

    set_src_dev_slot(src_id, slot_id);

    set_tar_dev_slot(tar_id - Dev_tar_start, slot_id);

    audio_write_reg(BTSET, BIT(slot_id));
}

void audio_disconnect_dev(enum audio_dev_id src_id, enum audio_dev_id tar_id)
{
    int slot_id = dev_to_slot_id(src_id);

    set_src_dev_slot(src_id, 0);

    set_tar_dev_slot(tar_id - Dev_tar_start, 0);

    audio_write_reg(BTCLR, BIT(slot_id));
}

static inline int to_tsz(int unit_size)
{
    if (unit_size == 1*4) return 0;
    if (unit_size == 4*4) return 1;
    if (unit_size == 8*4) return 2;
    if (unit_size == 16*4) return 3;
    if (unit_size == 32*4) return 4;
    return 0;
}

static inline int to_ss(int data_bits)
{
    if (data_bits == 8) return 0;
    if (data_bits == 12) return 1;
    if (data_bits == 13) return 2;
    if (data_bits == 16) return 3;
    if (data_bits == 18) return 4;
    if (data_bits == 20) return 5;
    if (data_bits == 24) return 6;
    if (data_bits == 32) return 7;
    return 3;
}

static inline int is_packed(int data_bits)
{
    if (data_bits == 8) return 1;
    if (data_bits == 12) return 1;
    if (data_bits == 13) return 1;
    if (data_bits == 16) return 1;
    if (data_bits == 32) return 1;
    return 0;
}

void audio_dma_desc_init(
    struct audio_dma_desc *desc,
    void *buf, int buf_size, int unit_size,
    struct audio_dma_desc *next)
{
    unsigned long cmd = 0;
    set_bit_field(&cmd, D_BAI, 1);
    set_bit_field(&cmd, D_TSZ, to_tsz(unit_size));
    set_bit_field(&cmd, D_LTE, 0);
    set_bit_field(&cmd, D_LINK, 1);

    desc->cmd = cmd;
    desc->next_desc = virt_to_phys(next);
    desc->dma_addr = virt_to_phys(buf);
    desc->trans_count = buf_size / unit_size;

    flush_dcache_force((unsigned long)(desc), sizeof(*desc));
}

void audio_dma_config(enum audio_dev_id dev_id, int channels, int data_bits, int unit_size)
{
    int dma_id = dev_to_dma_id(dev_id);
    int is_capture = dma_id >= 5;

    audio_set_bit(DCM(dma_id), NDES, 0);

    unsigned long ffr = 0;
    set_bit_field(&ffr, FTH, unit_size / 4);
    set_bit_field(&ffr, TUR_ROR_E, 0);
    set_bit_field(&ffr, TFS_RFS_E, 0);
    set_bit_field(&ffr, FIFO_TD, is_capture);
    audio_write_reg(FFR(dma_id), ffr);

    unsigned long dfcr = 0;
    set_bit_field(&dfcr, CHNUM, channels - 1);
    set_bit_field(&dfcr, SS, to_ss(data_bits));
    set_bit_field(&dfcr, PACK_EN, is_packed(data_bits));
    set_bit_field(&dfcr, ENABLE, 1);
    audio_write_reg(DFCR(dma_id), dfcr);

}

void audio_dma_set_callback(enum audio_dev_id dev_id,
     void (*dma_callback)(void *data), void *data)
{
    int dma_id = dev_to_dma_id(dev_id);

    callbacks[dma_id] = dma_callback;
    datas[dma_id] = data;
}

void audio_dma_start(enum audio_dev_id dev_id, struct audio_dma_desc *desc)
{
    int dma_id = dev_to_dma_id(dev_id);
    /* 清除此前的中断标志
     */
    audio_write_reg(DSR(dma_id), bit_field_mask(TT_INT) |
                                 bit_field_mask(LTT_INT) |
                                 bit_field_mask(TT) |
                                 bit_field_mask(LTT));

    audio_write_reg(DDA(dma_id), virt_to_phys(desc));

    /* 听说要回读一下
     */
    audio_read_reg(DDA(dma_id));

    // dma enable
    audio_set_bit(DCR(dma_id), CTE, 1);

    // fifo enable
    audio_set_bit(FCR(dma_id), FIFO_EN, 1);

    if (callbacks[dma_id])
        audio_set_bit(DCM(dma_id), TIE, 1);
    else
        audio_set_bit(DCM(dma_id), TIE, 0);

    audio_set_bit(DFCR(dma_id), ENABLE, 1);

    // aic_dma_dump_regs();
}

void audio_dma_stop(enum audio_dev_id dev_id)
{
    int dma_id = dev_to_dma_id(dev_id);

    audio_set_bit(DCM(dma_id), TIE, 0);

    // fifo disable
    audio_set_bit(FCR(dma_id), FIFO_EN, 0);

    // dma disable
    audio_write_reg(DCR(dma_id), 0);

    // 参考内核驱动,手册上并无描述
    int count = 0xfff;
    while (audio_get_bit(DSR(dma_id), RST_EN) && count--);

    // 参考内核驱动,手册上并无描述
    audio_write_reg(DCR(dma_id), bit_field_mask(DCR_RESET));
    udelay(10);

    audio_set_bit(DFCR(dma_id), ENABLE, 0);
}

void audio_dma_pause_release(enum audio_dev_id dev_id, struct audio_dma_desc *desc)
{
    int dma_id = dev_to_dma_id(dev_id);

    // dma enable
    audio_set_bit(DCR(dma_id), CTE, 1);;
}

void audio_dma_pause_push(enum audio_dev_id dev_id)
{
    int dma_id = dev_to_dma_id(dev_id);

    // dma disable
    audio_write_reg(DCR(dma_id), 0);
}

unsigned int audio_dma_get_current_addr(enum audio_dev_id dev_id)
{
    int dma_id = dev_to_dma_id(dev_id);

    return audio_read_reg(DBA(dma_id));
}

static void audio_dma_handler(int irq, void *data)
{
    unsigned int aipr = audio_read_reg(AIPR);

    int n = ffs(aipr);
    if (n) {
        n = n - 1;
        assert (callbacks[n]);
        callbacks[n](datas[n]);
    }
}

int audio_dma_init(void)
{
    audio_clk = clk_get("gate_audio");
    assert(!IS_ERR(audio_clk));

    clk_enable(audio_clk);

    audio_ram_clk = clk_get("audio_ram");
    assert(!IS_ERR(audio_ram_clk));

    clk_enable(audio_ram_clk);

    audio_write_reg(DGRR, bit_field_mask(DMA_RESET));

    /* 必须如此,用作fifo数据接收同步
     */
    audio_write_reg(BFCR2, bit_field_mask(0, 14));

    int i;
    for (i = 0; i < 10; i++) {
        /* 平均分配fifo大小, 总共4k */
        audio_write_reg(FAS(i), 384);
    }

    request_irq(IRQ_AUDIO, 0, audio_dma_handler, "audio-dma" , NULL);

    return 0;
}