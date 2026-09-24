#ifndef _SOC_CLK_H_
#define _SOC_CLK_H_

#include <driver/clk.h>

struct clk;
struct cpm_pwc;

struct clk_ops {
    int (*enable)(struct clk *,int);
    void (*init)(struct clk *clk);
    struct clk* (*get_parent)(struct clk *);
    int (*set_parent)(struct clk *,struct clk *);
    unsigned long (*get_rate)(struct clk *);
    int (*set_rate)(struct clk *,unsigned long);
    int (*set_round_rate)(struct clk *,unsigned long);
};

enum CLK_TYPE {
    CLK_TYPE_NOALLOC,
    CLK_TYPE_GATE0,
    CLK_TYPE_GATE1,
    CLK_TYPE_CGU,
    CLK_TYPE_I2S_CGU,
    CLK_TYPE_I2S_CHILD,
    CLK_TYPE_CPCCR,
    CLK_TYPE_PLL,
    CLK_TYPE_PWC,

    CLK_TYPE_NUMS,
};

struct clk {
    const char *name;
    unsigned long rate;
    unsigned char id;
    unsigned char type;
    unsigned char type_value;
    unsigned char parent;
    unsigned char count;
    unsigned char is_init_enabled;
};


enum {
    CLK_ID_EXT     = 0,
    CLK_ID_EXT0,
    #define CLK_NAME_EXT0       "ext0"
    CLK_ID_EXT1,
    #define CLK_NAME_EXT1       "ext1"
    CLK_ID_OTGPHY,
    #define CLK_NAME_OTGPHY	     "otg_phy"

    CLK_ID_PLL,
    #define CLK_NAME_APLL        "apll"
    CLK_ID_APLL,
    #define CLK_NAME_MPLL        "mpll"
    CLK_ID_MPLL,
    #define CLK_NAME_EPLL        "epll"
    CLK_ID_EPLL,

    CLK_ID_CPPCR,
    CLK_ID_SCLK_A,
    #define CLK_NAME_SCLK_A      "sclk_a"
    CLK_ID_CCLK,
    #define CLK_NAME_CCLK        "cclk"
    CLK_ID_L2CLK,
    #define CLK_NAME_L2CLK       "l2clk"
    CLK_ID_H0CLK,
    #define CLK_NAME_H0CLK       "h0clk"
    CLK_ID_H2CLK,
    #define CLK_NAME_H2CLK       "h2clk"
    CLK_ID_PCLK,
    #define CLK_NAME_PCLK        "pclk"

    CLK_ID_CGU,
    CLK_ID_CGU_DDR,
    #define CLK_NAME_CGU_DDR        "cgu_ddr"

    CLK_ID_CGU_MACPHY,
    #define CLK_NAME_CGU_MACPHY     "cgu_macphy"

    CLK_ID_CGU_MACTXPHY,
    #define CLK_NAME_CGU_MACTXPHY   "cgu_mactxphy0"

    CLK_ID_CGU_MACTXPHY1,
    #define CLK_NAME_CGU_MACTXPHY1  "cgu_mactxphy1"

    CLK_ID_CGU_MACPTP,
    #define CLK_NAME_CGU_MACPTP     "cgu_macptp"

    CLK_ID_CGU_LPC,
    #define CLK_NAME_CGU_LPC        "cgu_lcd"

    CLK_ID_CGU_MSC0,
    #define CLK_NAME_CGU_MSC0       "cgu_msc0"

    CLK_ID_CGU_MSC1,
    #define CLK_NAME_CGU_MSC1       "cgu_msc1"

    CLK_ID_CGU_MSC2,
    #define CLK_NAME_CGU_MSC2       "cgu_msc2"

    CLK_ID_CGU_SFC,
    #define CLK_NAME_CGU_SFC        "cgu_sfc"

    CLK_ID_CGU_SSI,
    #define CLK_NAME_CGU_SSI        "cgu_ssi"

    CLK_ID_CGU_CIM,
    #define CLK_NAME_CGU_CIM        "cgu_cim"

    CLK_ID_CGU_PWM,
    #define CLK_NAME_CGU_PWM        "cgu_pwm"

    CLK_ID_CGU_ISP,
    #define CLK_NAME_CGU_ISP        "cgu_isp"

    CLK_ID_CGU_RSA,
    #define CLK_NAME_CGU_RSA        "cgu_rsa"

    CLK_ID_I2S_CGU,
    CLK_ID_CGU_I2S0,
    #define CLK_NAME_CGU_I2S0       "cgu_i2s0"

    CLK_ID_CGU_I2S1,
    #define CLK_NAME_CGU_I2S1       "cgu_i2s1"

    CLK_ID_CGU_I2S2,
    #define CLK_NAME_CGU_I2S2       "cgu_i2s2"

    CLK_ID_CGU_I2S3,
    #define CLK_NAME_CGU_I2S3       "cgu_i2s3"

    CLK_ID_I2S_CHILD,
    CLK_ID_AUDIO_RAM,
    #define CLK_NAME_AUDIO_RAM      "audio_ram"

    CLK_ID_I2S_SPDIF,
    #define CLK_NAME_I2S_SPDIF      "i2s_spdif"

    CLK_ID_I2S_DMIC,
    #define CLK_NAME_I2S_DMIC       "i2s_dmic"

    CLK_ID_I2S_PCM,
    #define CLK_NAME_I2S_PCM        "i2s_pcm"

    CLK_ID_GATE,
    CLK_ID_GATE_DDR,
    #define CLK_NAME_GATE_DDR "gate_ddr"

    CLK_ID_GATE_IPU,
    #define CLK_NAME_GATE_IPU "gate_ipu"

    CLK_ID_GATE_AHB0,
    #define CLK_NAME_GATE_AHB0 "gate_ahb0"

    CLK_ID_GATE_APB0,
    #define CLK_NAME_GATE_APB0 "gate_apb0"

    CLK_ID_GATE_RTC,
    #define CLK_NAME_GATE_RTC "gate_rtc"

    CLK_ID_GATE_SSI1,
    #define CLK_NAME_GATE_SSI1 "gate_ssi1"

    CLK_ID_GATE_RSA,
    #define CLK_NAME_GATE_RSA "gate_rsa"

    CLK_ID_GATE_AES,
    #define CLK_NAME_GATE_AES "gate_aes"

    CLK_ID_GATE_LCD,
    #define CLK_NAME_GATE_LCD "gate_lcd"

    CLK_ID_GATE_CIM,
    #define CLK_NAME_GATE_CIM "gate_cim"

    CLK_ID_GATE_PDMA,
    #define CLK_NAME_GATE_PDMA "gate_pdma"

    CLK_ID_GATE_OST,
    #define CLK_NAME_GATE_OST "gate_ost"

    CLK_ID_GATE_SSI0,
    #define CLK_NAME_GATE_SSI0 "gate_ssi0"

    CLK_ID_GATE_TCU,
    #define CLK_NAME_GATE_TCU "gate_tcu"

    CLK_ID_GATE_DTRNG,
    #define CLK_NAME_GATE_DTRNG "gate_dtrng"

    CLK_ID_GATE_UART2,
    #define CLK_NAME_GATE_UART2 "gate_uart2"

    CLK_ID_GATE_UART1,
    #define CLK_NAME_GATE_UART1 "gate_uart1"

    CLK_ID_GATE_UART0,
    #define CLK_NAME_GATE_UART0 "gate_uart0"

    CLK_ID_GATE_SADC,
    #define CLK_NAME_GATE_SADC "gate_sadc"

    CLK_ID_GATE_HELIX,
    #define CLK_NAME_GATE_HELIX "gate_helix"

    CLK_ID_GATE_AUDIO,
    #define CLK_NAME_GATE_AUDIO "gate_audio"

    CLK_ID_GATE_I2C3,
    #define CLK_NAME_GATE_I2C3 "gate_i2c3"

    CLK_ID_GATE_I2C2,
    #define CLK_NAME_GATE_I2C2 "gate_i2c2"

    CLK_ID_GATE_I2C1,
    #define CLK_NAME_GATE_I2C1 "gate_i2c1"

    CLK_ID_GATE_I2C0,
    #define CLK_NAME_GATE_I2C0 "gate_i2c0"

    CLK_ID_GATE_SCC,
    #define CLK_NAME_GATE_SCC "gate_scc"

    CLK_ID_GATE_MSC1,
    #define CLK_NAME_GATE_MSC1 "gate_msc1"

    CLK_ID_GATE_MSC0,
    #define CLK_NAME_GATE_MSC0 "gate_msc0"

    CLK_ID_GATE_OTG,
    #define CLK_NAME_GATE_OTG "otg"

    CLK_ID_GATE_SFC,
    #define CLK_NAME_GATE_SFC "gate_sfc"

    CLK_ID_GATE_EFUSE,
    #define CLK_NAME_GATE_EFUSE "gate_efuse"

    CLK_ID_GATE_NEMC,
    #define CLK_NAME_GATE_NEMC "gate_nemc"

    CLK_ID_GATE_AR,
    #define CLK_NAME_GATE_AR "gate_ar"

    CLK_ID_GATE_MIPI_DSI,
    #define CLK_NAME_GATE_MIPI_DSI "gate_mipi_dsi"

    CLK_ID_GATE_MIPI_CSI,
    #define CLK_NAME_GATE_MIPI_CSI "gate_mipi_csi"

    CLK_ID_GATE_INTC,
    #define CLK_NAME_GATE_INTC "gate_intc"

    CLK_ID_GATE_MSC2,
    #define CLK_NAME_GATE_MSC2 "gate_msc2"

    CLK_ID_GATE_GMAC1,
    #define CLK_NAME_GATE_GMAC1 "gate_gmac1"

    CLK_ID_GATE_GMAC0,
    #define CLK_NAME_GATE_GMAC0 "gate_gmac0"

    CLK_ID_GATE_UART9,
    #define CLK_NAME_GATE_UART9 "gate_uart9"

    CLK_ID_GATE_UART8,
    #define CLK_NAME_GATE_UART8 "gate_uart8"

    CLK_ID_GATE_UART7,
    #define CLK_NAME_GATE_UART7 "gate_uart7"

    CLK_ID_GATE_UART6,
    #define CLK_NAME_GATE_UART6 "gate_uart6"

    CLK_ID_GATE_UART5,
    #define CLK_NAME_GATE_UART5 "gate_uart5"

    CLK_ID_GATE_UART4,
    #define CLK_NAME_GATE_UART4 "gate_uart4"

    CLK_ID_GATE_UART3,
    #define CLK_NAME_GATE_UART3 "gate_uart3"

    CLK_ID_GATE_SPDIF,
    #define CLK_NAME_GATE_SPDIF "gate_spdif"

    CLK_ID_GATE_DMIC,
    #define CLK_NAME_GATE_DMIC "gate_dmic"

    CLK_ID_GATE_PCM,
    #define CLK_NAME_GATE_PCM "gate_pcm"

    CLK_ID_GATE_I2S3,
    #define CLK_NAME_GATE_I2S3 "gate_i2s3"

    CLK_ID_GATE_I2S2,
    #define CLK_NAME_GATE_I2S2 "gate_i2s2"

    CLK_ID_GATE_I2S1,
    #define CLK_NAME_GATE_I2S1 "gate_i2s1"

    CLK_ID_GATE_I2S0,
    #define CLK_NAME_GATE_I2S0 "gate_i2s0"

    CLK_ID_GATE_ROT,
    #define CLK_NAME_GATE_ROT "gate_rot"

    CLK_ID_GATE_HASH,
    #define CLK_NAME_GATE_HASH "gate_hash"

    CLK_ID_GATE_PWM,
    #define CLK_NAME_GATE_PWM "gate_pwm"

    CLK_ID_GATE_FELIX,
    #define CLK_NAME_GATE_FELIX "gate_felix"

    CLK_ID_GATE_ISP1,
    #define CLK_NAME_GATE_ISP1 "gate_isp1"

    CLK_ID_GATE_ISP0,
    #define CLK_NAME_GATE_ISP0 "gate_isp0"

    CLK_ID_GATE_I2C5,
    #define CLK_NAME_GATE_I2C5 "gate_i2c5"

    CLK_ID_GATE_I2C4,
    #define CLK_NAME_GATE_I2C4 "gate_i2c4"

    CLK_ID_NUM,
};

struct freq_udelay_jiffy {
    unsigned int max_num;
    unsigned int cpufreq;
    unsigned int udelay_val;
    unsigned int loops_per_jiffy;
};

int get_clk_sources_size(void);
struct clk *get_clk_from_id(int clk_id);

void init_cgu_clk(struct clk *clk);
void init_cpccr_clk(struct clk *clk);
void init_ext_pll(struct clk *clk);
void init_gate_clk(struct clk *clk);
void cpm_pwc_init(void);
void init_pwc_clk(struct clk *clk);
int cpm_pwc_enable_ctrl(struct clk *clk,int on);
void cpm_pwc_suspend(void);
void cpm_pwc_resume(void);

#endif /* _SOC_CLK_H_ */
