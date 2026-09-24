#include "clk.h"
#include "soc/cpm.h"
#include "common.h"
#include "spinlock.h"

#define NOALLOC      .type = CLK_TYPE_NOALLOC
#define GATE0(x)     .type = CLK_TYPE_GATE0,        .type_value = x
#define GATE1(x)     .type = CLK_TYPE_GATE1,        .type_value = x
#define CPCCR()      .type = CLK_TYPE_CPCCR,        .type_value = 0
#define CGU()        .type = CLK_TYPE_CGU,          .type_value = 0
#define I2S_CGU()    .type = CLK_TYPE_I2S_CGU,      .type_value = 0
#define I2S_CHILD()  .type = CLK_TYPE_I2S_CHILD,    .type_value = 0
#define PLL(x)       .type = CLK_TYPE_PLL,          .type_value = x
#define PWC(x)       .type = CLK_TYPE_PWC,          .type_value = x
#define PARENT(P)    .parent = CLK_ID_##P

#define DEF_CLK(N, x...)                        \
    [CLK_ID_##N] = { .id = CLK_ID_##N, .name = CLK_NAME_##N, x}

static struct clk clk_srcs[] = {
    [CLK_ID_EXT] = { .id = CLK_ID_EXT, .name = "root"},

    DEF_CLK(EXT0,        NOALLOC),
    DEF_CLK(EXT1,        NOALLOC),
    DEF_CLK(OTGPHY,      NOALLOC),

    DEF_CLK(APLL,       PLL(CPM_CPAPCR)),
    DEF_CLK(MPLL,       PLL(CPM_CPMPCR)),
    DEF_CLK(EPLL,       PLL(CPM_CPEPCR)),

    DEF_CLK(SCLK_A,     CPCCR()),
    DEF_CLK(CCLK,       CPCCR()),
    DEF_CLK(L2CLK,      CPCCR()),
    DEF_CLK(H0CLK,      CPCCR()),
    DEF_CLK(H2CLK,      CPCCR()),
    DEF_CLK(PCLK,       CPCCR()),

    DEF_CLK(GATE_DDR,   GATE0(31), PARENT(H0CLK)),
/*  DEF_CLK(GATE_IPU,  GATE0(30), PARENT(DIV_IPU)), */
    DEF_CLK(GATE_AHB0,  GATE0(29), PARENT(H0CLK)),
    DEF_CLK(GATE_APB0,  GATE0(28), PARENT(PCLK)),
    DEF_CLK(GATE_RTC,   GATE0(27), PARENT(EXT0)),
    DEF_CLK(GATE_SSI1,  GATE0(26), PARENT(PCLK)),
    DEF_CLK(GATE_RSA,   GATE0(25), PARENT(EXT1)),
    DEF_CLK(GATE_AES,   GATE0(24), PARENT(EXT1)),
    DEF_CLK(GATE_LCD,   GATE0(23), PARENT(EXT1)),
    DEF_CLK(GATE_CIM,   GATE0(22), PARENT(EXT1)),
    DEF_CLK(GATE_PDMA,  GATE0(21), PARENT(EXT1)),
    DEF_CLK(GATE_OST,   GATE0(20), PARENT(EXT1)),
    DEF_CLK(GATE_SSI0,  GATE0(19), PARENT(EXT1)),
    DEF_CLK(GATE_TCU,   GATE0(18), PARENT(EXT1)),
    DEF_CLK(GATE_DTRNG, GATE0(17), PARENT(EXT1)),
    DEF_CLK(GATE_UART2, GATE0(16), PARENT(EXT1)),
    DEF_CLK(GATE_UART1, GATE0(15), PARENT(EXT1)),
    DEF_CLK(GATE_UART0, GATE0(14), PARENT(EXT1)),
    DEF_CLK(GATE_SADC,  GATE0(13), PARENT(EXT1)),
    DEF_CLK(GATE_HELIX, GATE0(12), PARENT(EXT1)),
    DEF_CLK(GATE_AUDIO, GATE0(11), PARENT(EXT1)),
    DEF_CLK(GATE_I2C3,  GATE0(10), PARENT(PCLK)),
    DEF_CLK(GATE_I2C2,  GATE0(9),  PARENT(PCLK)),
    DEF_CLK(GATE_I2C1,  GATE0(8),  PARENT(PCLK)),
    DEF_CLK(GATE_I2C0,  GATE0(7),  PARENT(PCLK)),
    DEF_CLK(GATE_SCC,   GATE0(6),  PARENT(EXT1)),
    DEF_CLK(GATE_MSC1,  GATE0(5),  PARENT(EXT1)),
    DEF_CLK(GATE_MSC0,  GATE0(4),  PARENT(EXT1)),
    DEF_CLK(GATE_OTG,   GATE0(3),  PARENT(EXT1)),
    DEF_CLK(GATE_SFC,   GATE0(2),  PARENT(EXT1)),
    DEF_CLK(GATE_EFUSE, GATE0(1),  PARENT(EXT1)),
    DEF_CLK(GATE_NEMC,  GATE0(0),  PARENT(EXT1)),

    DEF_CLK(GATE_AR,       GATE1(30), PARENT(EXT1)),
    DEF_CLK(GATE_MIPI_DSI, GATE1(29), PARENT(EXT1)),
    DEF_CLK(GATE_MIPI_CSI, GATE1(28), PARENT(EXT1)),
    DEF_CLK(GATE_INTC,     GATE1(26), PARENT(EXT1)),
    DEF_CLK(GATE_MSC2,     GATE1(25), PARENT(EXT1)),
    DEF_CLK(GATE_GMAC1,    GATE1(24), PARENT(EXT1)),
    DEF_CLK(GATE_GMAC0,    GATE1(23), PARENT(EXT1)),
    DEF_CLK(GATE_UART9,    GATE1(22), PARENT(EXT1)),
    DEF_CLK(GATE_UART8,    GATE1(21), PARENT(EXT1)),
    DEF_CLK(GATE_UART7,    GATE1(20), PARENT(EXT1)),
    DEF_CLK(GATE_UART6,    GATE1(19), PARENT(EXT1)),
    DEF_CLK(GATE_UART5,    GATE1(18), PARENT(EXT1)),
    DEF_CLK(GATE_UART4,    GATE1(17), PARENT(EXT1)),
    DEF_CLK(GATE_UART3,    GATE1(16), PARENT(EXT1)),
    DEF_CLK(GATE_SPDIF,    GATE1(14), PARENT(EXT1)),
    DEF_CLK(GATE_DMIC,     GATE1(13), PARENT(EXT1)),
    DEF_CLK(GATE_PCM,      GATE1(12), PARENT(EXT1)),
    DEF_CLK(GATE_I2S3,     GATE1(11), PARENT(EXT1)),
    DEF_CLK(GATE_I2S2,     GATE1(10), PARENT(EXT1)),
    DEF_CLK(GATE_I2S1,     GATE1(9),  PARENT(EXT1)),
    DEF_CLK(GATE_I2S0,     GATE1(8),  PARENT(EXT1)),
    DEF_CLK(GATE_ROT,      GATE1(7),  PARENT(EXT1)),
    DEF_CLK(GATE_HASH,     GATE1(6),  PARENT(EXT1)),
    DEF_CLK(GATE_PWM,      GATE1(5),  PARENT(EXT1)),
    DEF_CLK(GATE_FELIX,    GATE1(4),  PARENT(EXT1)),
    DEF_CLK(GATE_ISP1,     GATE1(3),  PARENT(EXT1)),
    DEF_CLK(GATE_ISP0,     GATE1(2),  PARENT(EXT1)),
    DEF_CLK(GATE_I2C5,     GATE1(1),  PARENT(PCLK)),
    DEF_CLK(GATE_I2C4,     GATE1(0),  PARENT(PCLK)),

    DEF_CLK(CGU_DDR,       CGU()),
    DEF_CLK(CGU_MACPHY,    CGU()),
    DEF_CLK(CGU_MACTXPHY,  CGU()),
    DEF_CLK(CGU_MACTXPHY1, CGU()),
    DEF_CLK(CGU_MACPTP,    CGU()),
    DEF_CLK(CGU_LPC,       CGU()),
    DEF_CLK(CGU_MSC0,      CGU()),
    DEF_CLK(CGU_MSC1,      CGU()),
    DEF_CLK(CGU_MSC2,      CGU()),
    DEF_CLK(CGU_SFC,       CGU()),
    DEF_CLK(CGU_SSI,       CGU()),
    DEF_CLK(CGU_CIM,       CGU()),
    DEF_CLK(CGU_PWM,       CGU()),
    DEF_CLK(CGU_ISP,       CGU()),
    DEF_CLK(CGU_RSA,       CGU()),

    DEF_CLK(CGU_I2S0,      I2S_CGU()),
    DEF_CLK(CGU_I2S1,      I2S_CGU()),
    DEF_CLK(CGU_I2S2,      I2S_CGU()),
    DEF_CLK(CGU_I2S3,      I2S_CGU()),
    DEF_CLK(I2S_DMIC,      I2S_CHILD()),
    DEF_CLK(I2S_PCM,       I2S_CHILD()),
    DEF_CLK(I2S_SPDIF,     I2S_CHILD()),
    DEF_CLK(AUDIO_RAM,     I2S_CHILD())
};

void init_ext_pll(struct clk *clk);
unsigned long pll_get_rate(struct clk *clk);

void init_gate_clk(struct clk *clk);
int cpm_gate_enable(struct clk *clk, int on);

void init_cpccr_clk(struct clk *clk);
unsigned long cpccr_get_rate(struct clk *clk);

void init_cgu_clk(struct clk *clk);
int cgu_enable(struct clk *clk,int on);
unsigned long cgu_get_rate(struct clk *clk);
struct clk * cgu_get_parent(struct clk *clk);
int cgu_set_rate(struct clk *clk, unsigned long rate);
int cgu_set_parent(struct clk *clk, struct clk *parent);

void init_i2s_cgu_clk(struct clk *clk);
int i2s_cgu_enable(struct clk *clk, int on);
unsigned long i2s_cgu_get_rate(struct clk *clk);
struct clk *i2s_cgu_get_parent(struct clk *clk);
int i2s_cgu_set_rate(struct clk *clk, unsigned long rate);
int i2s_cgu_set_parent(struct clk *clk, struct clk *parent);

void init_i2s_child_clk(struct clk *clk);
int i2s_child_enable(struct clk *clk, int on);
int i2s_child_set_rate(struct clk *clk, unsigned long rate);
unsigned long i2s_child_get_rate(struct clk *clk);
struct clk * i2s_child_get_parent(struct clk *clk);
int i2s_child_set_parent(struct clk *clk, struct clk *parent);

struct clk_ops clkops[CLK_TYPE_NUMS] = {
    [CLK_TYPE_NOALLOC] = {
        .init = init_ext_pll,
    },
    [CLK_TYPE_PLL] = {
        .init = init_ext_pll,
        .get_rate = pll_get_rate,
    },
    [CLK_TYPE_CPCCR] = {
        .init = init_cpccr_clk,
        .get_rate = cpccr_get_rate,
    },
    [CLK_TYPE_CGU] = {
        .init = init_cgu_clk,
        .enable = cgu_enable,
        .get_rate = cgu_get_rate,
        .set_rate = cgu_set_rate,
        .get_parent = cgu_get_parent,
        .set_parent = cgu_set_parent
    },
    [CLK_TYPE_I2S_CGU] = {
        .init = init_i2s_cgu_clk,
        .enable = i2s_cgu_enable,
        .get_rate = i2s_cgu_get_rate,
        .set_rate = i2s_cgu_set_rate,
        .get_parent = i2s_cgu_get_parent,
        .set_parent = i2s_cgu_set_parent
    },
    [CLK_TYPE_I2S_CHILD] = {
        .init = init_i2s_child_clk,
        .enable = i2s_child_enable,
        .get_rate = i2s_child_get_rate,
        .set_rate = i2s_child_set_rate,
        .get_parent = i2s_child_get_parent,
        .set_parent = i2s_child_set_parent
    },
    [CLK_TYPE_GATE0] = {
        .init = init_gate_clk,
        .enable = cpm_gate_enable
    },
    [CLK_TYPE_GATE1] = {
        .init = init_gate_clk,
        .enable = cpm_gate_enable
    },
};

int get_clk_sources_size(void)
{
    return ARRAY_SIZE(clk_srcs);
}

struct clk *get_clk_from_id(int clk_id)
{
    return &clk_srcs[clk_id];
}