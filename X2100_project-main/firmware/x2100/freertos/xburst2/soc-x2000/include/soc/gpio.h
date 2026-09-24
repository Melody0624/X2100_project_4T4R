#ifndef _SOC_GPIO_H_
#define _SOC_GPIO_H_

#define GPIO_PORT_OFF    0x100
#define GPIO_SHADOW_OFF  0x700

#define PXPIN      0x00   /* PIN Level Register */
#define PXINT      0x10   /* Port Interrupt Register */
#define PXINTS     0x14   /* Port Interrupt Set Register */
#define PXINTC     0x18   /* Port Interrupt Clear Register */
#define PXMSK      0x20   /* Port Interrupt Mask Reg */
#define PXMSKS     0x24   /* Port Interrupt Mask Set Reg */
#define PXMSKC     0x28   /* Port Interrupt Mask Clear Reg */
#define PXPAT1     0x30   /* Port Pattern 1 Set Reg. */
#define PXPAT1S    0x34   /* Port Pattern 1 Set Reg. */
#define PXPAT1C    0x38   /* Port Pattern 1 Clear Reg. */
#define PXPAT0     0x40   /* Port Pattern 0 Register */
#define PXPAT0S    0x44   /* Port Pattern 0 Set Register */
#define PXPAT0C    0x48   /* Port Pattern 0 Clear Register */
#define PXFLG      0x50   /* Port Flag Register */
#define PXFLGC     0x58   /* Port Flag clear Register */

#define PXDEG      0x70   /* Port Dual Edge Register */
#define PXDEGS     0x74   /* Port Dual Edge Set Register */
#define PXDEGC     0x78   /* Port Dual Edge Clear Register */

#define PXPU       0x80   /* Port PULL-UP State Register */
#define PXPUS      0x84   /* Port PULL-UP State Set Register */
#define PXPUC      0x88   /* Port PULL-UP State Clear Register */
#define PXPD       0x90   /* Port PULL-DOWN State Register */
#define PXPDS      0x94   /* Port PULL-DOWN State Set Register */
#define PXPDC      0x98   /* Port PULL-DOWN State Clear Register */

#define PXDS0	   0xA0   /* PORT Drive Strength State Register0*/
#define PXDS0S	   0xA4   /* PORT Drive Strength State set Register0*/
#define PXDS0C	   0xA8   /* PORT Drive Strength State clear Register0*/
#define PXDS1	   0xB0   /* PORT Drive Strength State Register1*/
#define PXDS1S	   0xB4   /* PORT Drive Strength State set Register1*/
#define PXDS1C	   0xB8   /* PORT Drive Strength State clear Register1*/
#define PXDS2	   0xC0   /* PORT Drive Strength State Register2*/
#define PXDS2S	   0xC4   /* PORT Drive Strength State set Register2*/
#define PXDS2C	   0xC8   /* PORT Drive Strength State clear Register2*/
#define PXSR	   0xD0   /* PORT Slew Rate Register */
#define PXSRS	   0xD4   /* PORT Slew Rate Register Set */
#define PXSRC	   0xD8   /* PORT Slew Rate Register Clear */
#define PXSMT	   0xE0   /* PORT Schmitt Trigger Register */
#define PXSMTS	   0xE4   /* PORT Schmitt Trigger Register Set */
#define PXSMTC	   0xE8   /* PORT Schmitt Trigger Register Clear */

#define PZGID2LD   0xF0   /* GPIOZ Group ID to load */

#define SHADOW 5

enum gpio_function {
    GPIO_FUNC_0             = 0x0100,  //0000, GPIO as function 0 / device 0
    GPIO_FUNC_1             = 0x0101,  //0001, GPIO as function 1 / device 1
    GPIO_FUNC_2             = 0x0102,  //0010, GPIO as function 2 / device 2
    GPIO_FUNC_3             = 0x0103,  //0011, GPIO as function 3 / device 3
    GPIO_OUTPUT0            = 0x0104,  //0100, GPIO output low  level
    GPIO_OUTPUT1            = 0x0105,  //0101, GPIO output high level
    GPIO_INPUT              = 0x0106,  //0110, GPIO as input.7 also.
    GPIO_INT_LO             = 0x0108,  //1000, Low  Level trigger interrupt
    GPIO_INT_HI             = 0x0109,  //1001, High Level trigger interrupt
    GPIO_INT_FE             = 0x010a,  //1010, Fall Edge trigger interrupt
    GPIO_INT_RE             = 0x010b,  //1011, Rise Edge trigger interrupt
    GPIO_INT_RE_FE          = 0x011b,  //11011, Dual Edge(both rise and fall) trigger interrupt

    GPIO_INT_MASK_LO        = 0x010c,  //1100, Port is low level triggered interrupt input. Interrupt is masked.
    GPIO_INT_MASK_HI        = 0x010d,  //1101, Port is high level triggered interrupt input. Interrupt is masked.
    GPIO_INT_MASK_FE        = 0x010e,  //1110, Port is fall edge triggered interrupt input. Interrupt is masked.
    GPIO_INT_MASK_RE        = 0x010f,  //1111, Port is rise edge triggered interrupt input. Interrupt is masked.
    GPIO_INT_MASK_RE_FE     = 0x011f,  //11111, Port is dual edge(both rise and fall edge) triggered interrupt input. Interrupt is masked.

    GPIO_PULL_HIZ           = 0x8000,    //no pull
    GPIO_PULL_UP            = 0xa000,    //pull high
    GPIO_PULL_DOWN          = 0xc000,    //pull low
    GPIO_PULL_BUSHOLD       = 0xe000,
};

enum gpio_port {
    GPIO_PORT_A = 0,
    GPIO_PORT_B,
    GPIO_PORT_C,
    GPIO_PORT_D,
    GPIO_PORT_E,
    GPIO_PORT_F,

    /* this must be last */
    GPIO_NR_PORTS,
};

#define GPIO_PA(n) (0 * 32 + (n))
#define GPIO_PB(n) (1 * 32 + (n))
#define GPIO_PC(n) (2 * 32 + (n))
#define GPIO_PD(n) (3 * 32 + (n))
#define GPIO_PE(n) (4 * 32 + (n))
#define GPIO_PF(n) (5 * 32 + (n))

static inline int gpio_to_port(int gpio)
{
    return ((gpio) / 32);
}

static inline int gpio_to_pin(int gpio)
{
    return ((gpio) % 32);
}

static inline int soc_gpio_is_valid(int gpio)
{
    return gpio >= 0;
}

void gpio_port_set_func(enum gpio_port port, unsigned int pins, enum gpio_function func);

void gpio_set_glitch_filter(int gpio, unsigned char period);
void gpio_port_set_glitch_filter(enum gpio_port port, unsigned long pins, unsigned char period);

void gpio_voltage_init(void);

void soc_gpio_set_driver_strength(int gpio, int value);
int soc_gpio_get_driver_strength(int gpio);
void soc_gpio_set_slew_rate(int gpio, int value);
int soc_gpio_get_slew_rate(int gpio);
void soc_gpio_set_schmitt(int gpio, int value);
int soc_gpio_get_schmitt(int gpio);

#endif /* _SOC_GPIO_H_ */