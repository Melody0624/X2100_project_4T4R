#include <soc/cpm.h>
#include "clk.h"
#include "spinlock.h"
#include "error-base.h"
#include "common.h"

static DEFINE_SPINLOCK(i2s_child_lock);

#define audio_cs    30,30
#define spdif_cs    3,4
#define pcm_cs      1,2
#define dmic_cs     0,0

struct i2s_child_clk {
    unsigned char src[4];
    unsigned char cs[2];
};

#define index(id) ((id) - CLK_ID_AUDIO_RAM)

static struct i2s_child_clk i2s_child_clks[] = {
    [index(CLK_ID_AUDIO_RAM)]   = {{CLK_ID_H0CLK,    CLK_ID_EXT1,     CLK_ID_EXT,      CLK_ID_EXT     }, {audio_cs}},
    [index(CLK_ID_I2S_DMIC)]    = {{CLK_ID_EXT1,     CLK_ID_CGU_I2S3, CLK_ID_EXT,      CLK_ID_EXT     }, {dmic_cs }},
    [index(CLK_ID_I2S_PCM)]     = {{CLK_ID_CGU_I2S0, CLK_ID_CGU_I2S1, CLK_ID_CGU_I2S2, CLK_ID_CGU_I2S3}, {pcm_cs  }},
    [index(CLK_ID_I2S_SPDIF)]   = {{CLK_ID_CGU_I2S0, CLK_ID_CGU_I2S1, CLK_ID_CGU_I2S2, CLK_ID_CGU_I2S3}, {spdif_cs}},
};

static inline void cpm_set_xcdr(unsigned long xcdr)
{
    cpm_outl(xcdr, CPM_AUDIOCR);
}

static inline unsigned long cpm_get_xcdr(void)
{
    return cpm_inl(CPM_AUDIOCR);
}

static inline struct clk* to_parent_clk(unsigned long xcdr, int id)
{
    unsigned char *cs = i2s_child_clks[id].cs;
    int n = get_bit_field(&xcdr, cs[0], cs[1]);
    int parent = i2s_child_clks[id].src[n];

    return get_clk_from_id(parent);
}

static void cpm_set_parent(int id, int parent)
{
    unsigned char *cs = i2s_child_clks[id].cs;
    unsigned long xcdr = cpm_get_xcdr();

    /* 设置时钟源 */
    set_bit_field(&xcdr, cs[0], cs[1], parent);
    cpm_set_xcdr(xcdr);
}

int i2s_child_set_parent(struct clk *clk, struct clk *parent)
{
    int i;
    unsigned long flags;
    int id = index(clk->id);

    for (i = 0; i < 4; i++) {
        if (i2s_child_clks[id].src[i] == parent->id)
            break;
    }
    if (i >= 4)
        return -EINVAL;

    spin_lock_irqsave(&i2s_child_lock, flags);

    cpm_set_parent(id, i);
    clk->parent = parent->id;

    spin_unlock_irqrestore(&i2s_child_lock, flags);

    return 0;
}

struct clk * i2s_child_get_parent(struct clk *clk)
{
    int id = index(clk->id);
    unsigned long xcdr = cpm_get_xcdr();
    return to_parent_clk(xcdr, id);
}

int i2s_child_enable(struct clk *clk, int on)
{
    return 0;
}

unsigned long i2s_child_get_rate(struct clk *clk)
{
    struct clk *parent = get_clk_from_id(clk->parent);

    return parent->rate;
}

int i2s_child_set_rate(struct clk *clk, unsigned long rate)
{
    struct clk *parent = get_clk_from_id(clk->parent);
    printf("i2s_child: %s can't be set rate, you should set parent clk %s\n", clk->name, parent->name);
    return -1;
}

void init_i2s_child_clk(struct clk *clk)
{
    struct clk *parent = i2s_child_get_parent(clk);
    if (parent)
        clk->parent = parent->id;

    clk->rate = i2s_child_get_rate(clk);
}