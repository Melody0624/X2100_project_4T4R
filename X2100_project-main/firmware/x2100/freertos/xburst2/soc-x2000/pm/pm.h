


struct sleep_param {
	unsigned int pdt;
	unsigned int dpd;
	unsigned int dlp;
	unsigned int autorefresh;
	unsigned int cpu_div;
	unsigned int uart_base;
	unsigned int gpio;
	unsigned int gpio_level;
	unsigned int cpccr;
};


/*
 *
 *	|-----------------------|
 *	|			|
 *	|			|
 *	|	RESUME_SP	|
 *	|			|
 *	|-----------------------|
 *	|			|
 *	|  	  PARAM		|
 *	|			|
 *	|-----------------------|
 *	|			|
 *	|			|
 *	|	SLEEP_TEXT      |
 *	|			|
 *	|			|
 *	|-----------------------|
 *	|			|
 *	|			|
 *	|	RESUME_TEXT	|
 *	|			|
 *	|			|
 *	|-----------------------|
 *	|			|
*	|  RESUME_BOOTU_TEXTP	|
 *	|			|
 *	|-----------------------|     <------------ TCSM_START 0xb2400000
 *
 */


#define SLEEP_MEMORY_START		0xb2400000
#define SLEEP_MEMORY_END		0xb2407ff8
#define SLEEP_RESUME_SP			SLEEP_MEMORY_END
#define SLEEP_RESUME_BOOTUP_TEXT	SLEEP_MEMORY_START

#define SLEEP_CPU_RESUME_BOOTUP_TEXT	SLEEP_RESUME_BOOTUP_TEXT
#define SLEEP_CPU_RESUME_BOOTUP_LEN	64 // 16 instructions
#define SLEEP_CPU_RESUME_TEXT		(SLEEP_CPU_RESUME_BOOTUP_TEXT + SLEEP_CPU_RESUME_BOOTUP_LEN)
#define SLEEP_CPU_RESUME_LEN		(4096)
#define SLEEP_CPU_SLEEP_TEXT		(SLEEP_CPU_RESUME_TEXT + SLEEP_CPU_RESUME_LEN)
#define SLEEP_CPU_SLEEP_LEN		(3072)
#define SLEEP_CPU_PARAM_ADDR		(SLEEP_CPU_SLEEP_TEXT + SLEEP_CPU_SLEEP_LEN)
#define SLEEP_CPU_PARAM_SIZE		(sizeof(struct sleep_param))
#define SLEEP_CPU_RESUME_SP		SLEEP_RESUME_SP

#define sleep_param ((struct sleep_param *)SLEEP_CPU_PARAM_ADDR)


extern long long save_goto(unsigned int func);
extern int restore_goto(unsigned int func);


#define reg_ddr_phy(x)   (*(volatile unsigned int *)(0xb3011000 + ((x) << 2)))

#define rtc_read_reg(reg) ({ \
	while (!((*(volatile unsigned int *)0xb0003000 >> 7) & 0x1) ); \
	*(volatile unsigned int *)reg;\
})

#define rtc_write_reg(reg, val) do{ \
	while (!((*(volatile unsigned int *)0xb0003000 >> 7) & 0x1) ); \
	*(volatile unsigned int *)0xb000303c = 0xa55a; \
	while (!((*(volatile unsigned int *)0xb000303c >> 31) & 0x1) ); \
	while (!((*(volatile unsigned int *)0xb0003000 >> 7) & 0x1) ); \
	*(volatile unsigned int *)reg = val; \
	while (!((*(volatile unsigned int *)0xb0003000 >> 7) & 0x1) ); \
}while(0)

#define INTC_ICMR0  0x04
#define INTC_ICMR1  0x24

#define readl(addr)     (*(volatile unsigned int *)(addr))
#define writel(b, addr) (*(volatile unsigned int *)(addr)) = (b)

#define intc_readl(o)		readl(0xb2300000 + (o))
#define intc_writel(b, o)	writel(b, 0xb2300000 + (o))

#define ext_intc_readl(o)		readl(0xb0001000 + (o))
#define ext_intc_writel(b, o)	writel(b, 0xb0001000 + (o))

/*************************************** debug ***************************************/

#ifdef PRINT_DEBUG

#define OFF_TDR         (0x00)
#define OFF_LCR         (0x0C)
#define OFF_LSR         (0x14)
#define LSR_TDRQ        (1 << 5)
#define LSR_TEMT        (1 << 6)

#define U_IOBASE (UART0_IOBASE + 0x1000*CONFIG_UART_CONSOLE_UART_ID + 0xa0000000)

#define TCSM_PCHAR(x)                                                   \
	*((volatile unsigned int*)(U_IOBASE+OFF_TDR)) = x;              \
while ((*((volatile unsigned int*)(U_IOBASE + OFF_LSR)) & (LSR_TDRQ | LSR_TEMT)) != (LSR_TDRQ | LSR_TEMT))
#else
#define TCSM_PCHAR(x)
#endif

#define TCSM_DELAY(x)                                           \
	do{                                                     \
		register unsigned int i = x;                    \
		while(i--)                                      \
		__asm__ volatile("nop\n\t"              \
				);        \
	}while(0)                                               \

static inline void serial_put_hex(unsigned int x) {
	int i;
	unsigned int d;
	for(i = 7;i >= 0;i--) {
		d = (x  >> (i * 4)) & 0xf;
		if(d < 10) d += '0';
		else d += 'A' - 10;
		TCSM_PCHAR(d);
	}
	TCSM_PCHAR('\r');
	TCSM_PCHAR('\n');
}

/*************************************************************************************/
