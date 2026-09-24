
#include <driver/cache.h>
#include <cpu/cache.h>
#include <asm/mipsregs.h>
#include <soc/base.h>
#include <soc/ddr.h>
#include <soc/cpm.h>
#include <cpu/irqflags.h>
#include <stdio.h>
#include <os.h>
#include <driver/gpio.h>
#include <driver/gpio_pin.h>

#include "pm.h"

#define gpio_outl(val,port,reg)        outl(val,GPIO_IOBASE+(port*GPIO_PORT_OFF)+(reg))

#ifdef DEBUG_PM
static void x2000_pm_gate_check(void)
{
	unsigned int gate0 = cpm_inl(CPM_CLKGR0);
	unsigned int gate1 = cpm_inl(CPM_CLKGR1);
	int i;
	int x;

	printf("gate0 = 0x%08x\n", gate0);
	printf("gate1 = 0x%08x\n", gate1);
	for (i = 0; i < 32; i++) {
		x = (gate0 >> i) & 1;
		if (x == 0)
			printf("warning : bit[%d] in clk gate0 is enabled\n", i);
	}
	for (i = 0; i < 32; i++) {
		x = (gate1 >> i) & 1;
		if (x == 0)
			printf("warning : bit[%d] in clk gate1 is enabled\n", i);
	}

}
#endif

extern long long save_goto(unsigned int func);
extern int restore_goto(unsigned int func);

static inline void ddrp_auto_calibration(void)
{
	unsigned int reg_val = ddr_readl(DDRP_INNOPHY_TRAINING_CTRL);
	unsigned int timeout = 0xffffff;
	unsigned int wait_cal_done = DDRP_CALIB_DONE_HDQCFA | DDRP_CALIB_DONE_LDQCFA;

	reg_val &= ~(DDRP_TRAINING_CTRL_DSCSE_BP);
	reg_val |= DDRP_TRAINING_CTRL_DSACE_START;
	ddr_writel(reg_val, DDRP_INNOPHY_TRAINING_CTRL);

	while(!((ddr_readl(DDRP_INNOPHY_CALIB_DONE) & wait_cal_done) == wait_cal_done) && --timeout) {
		TCSM_PCHAR('t');
	}

	if(!timeout) {
		TCSM_PCHAR('f');
	}
	ddr_writel(0, DDRP_INNOPHY_TRAINING_CTRL);
}




static void load_func_to_tcsm(unsigned int *tcsm_addr,unsigned int *f_addr,unsigned int size)
{
	unsigned int instr;
	int offset;
	int i;
#ifdef DEBUG_PM
	printf("tcsm addr = %p %p size = %d\n",tcsm_addr,f_addr,size);
#endif
	for(i = 0;i < size / 4;i++) {
		instr = f_addr[i];
		if((instr >> 26) == 2){
			offset = instr & 0x3ffffff;
			offset = (offset << 2) - ((unsigned int)f_addr & 0xfffffff);
			if(offset > 0) {
				offset = ((unsigned int)tcsm_addr & 0xfffffff) + offset;
				instr = (2 << 26) | (offset >> 2);
			}
		}
		tcsm_addr[i] = instr;
	}
}

#ifdef X2000_IDLE_PD
static inline int soc_pm_idle(void)
{
	unsigned int lcr = cpm_inl(CPM_LCR);
	unsigned int opcr = cpm_inl(CPM_OPCR);

	lcr &=~ 0x3; // low power mode: IDLE
	lcr |= 2;
	cpm_outl(lcr, CPM_LCR);

	opcr &= ~ (1<<31);
	opcr |= (1 << 30);
	opcr &= ~(1 << 26);	//l2c power down
	opcr |= 1 << 2; // select RTC clk
	cpm_outl(opcr, CPM_OPCR);

	return 0;
}
#else
static inline int soc_pm_idle(void)
{
	unsigned int lcr = cpm_inl(CPM_LCR);
	unsigned int opcr = cpm_inl(CPM_OPCR);

	lcr &=~ 0x3; // low power mode: IDLE
	cpm_outl(lcr, CPM_LCR);

	opcr &= ~(1 << 30);
	opcr &= ~(1 << 31);
	cpm_outl(opcr, CPM_OPCR);

	return 0;
}
#endif

static int soc_pm_sleep(void)
{
	unsigned int lcr = cpm_inl(CPM_LCR);
	unsigned int opcr = cpm_inl(CPM_OPCR);

	lcr &=~ 0x3;
	lcr |= 1 << 0; // low power mode: SLEEP
	cpm_outl(lcr, CPM_LCR);

	opcr &= ~(1 << 31);
	opcr |= (1 << 30);
	opcr &= ~(1 << 26); //L2C power down
	opcr |= (1 << 21); // cpu 32k ram retention.
	opcr |= (1 << 3); // power down CPU
	opcr &= ~(1 << 4); // exclk disable;
	opcr |= (1 << 2); // select RTC clk
	opcr |= (1 << 22);
	opcr |= (1 << 20);
	cpm_outl(opcr, CPM_OPCR);

	return 0;
}

static int soc_post_wakeup(void)
{
	unsigned int lcr = cpm_inl(CPM_LCR);
	lcr &= ~0x3; // low power mode: IDLE
	cpm_outl(lcr, CPM_LCR);

	printf("post wakeup!\n");


	{
		/* after power down cpu by set PD in OPCR, resume cpu's frequency and L2C's freq */
		unsigned int val;

		/* change disable */
		val = cpm_inl(CPM_CPCCR);
		val &= ~(1 << 22);
		cpm_outl(val, CPM_CPCCR);

		/* resume cpu_div in CPCCR */
		val &= ~0xf;
		val |= sleep_param->cpu_div;
		cpm_outl(val, CPM_CPCCR);

		/* change enable */
		val |= (1 << 22);
		cpm_outl(val, CPM_CPCCR);

		while (cpm_inl(CPM_CPCSR) & 1);
	}
	return 0;
}

static noinline void cpu_resume_bootup(void)
{
	TCSM_PCHAR('X');
	/* set reset entry */
	*(volatile unsigned int *)0xb2200f00 = 0xbfc00000;

	__asm__ volatile(
		".set push	\n\t"
		".set mips32r2	\n\t"
		"move $29, %0	\n\t"
		"jr.hb   %1	\n\t"
		"nop		\n\t"
		".set pop	\n\t"
		:
		:"r" (SLEEP_CPU_RESUME_SP), "r"(SLEEP_CPU_RESUME_TEXT)
		:
		);

}

static noinline void cpu_resume(void)
{
	unsigned int ddrc_ctrl;

	TCSM_PCHAR('R');

	if (sleep_param->gpio >= 0) {
		/* resume normal voltage */
		gpio_outl(1 << (sleep_param->gpio % 32), sleep_param->gpio / 32,  sleep_param->gpio_level ? PXPAT0C : PXPAT0S);
		/* wait voltage to stabilize  */
		TCSM_DELAY(1000);
		/* resume clk div */
		cpm_outl(0x55700000 | (sleep_param->cpccr & 0xFFFFF),CPM_CPCCR);
		while(cpm_inl(CPM_CPCSR) & 7);
		/* resume clk source */
		cpm_outl(sleep_param->cpccr, CPM_CPCCR);
		while((cpm_inl(CPM_CPCSR) & 0xf0000000) != 0xf0000000);
	}

	{
		int tmp;

		/* enable pll */
		tmp = reg_ddr_phy(0x21);
		tmp &= ~(1 << 1);
		reg_ddr_phy(0x21) = tmp;

		while (!(reg_ddr_phy(0x32) & 0x8))
			serial_put_hex(reg_ddr_phy(0x32));

		/* dfi_init_start = 0, wait dfi_init_complete */
		*(volatile unsigned int *)0xb3012000 &= ~(1 << 3);
		while(!(*(volatile unsigned int *)0xb3012004 & 0x1));

		/* bufferen_core = 1 */
		tmp = rtc_read_reg(0xb0003048);
		tmp |= (1 << 21);
		rtc_write_reg(0xb0003048, tmp);

		/* exit sr */
		ddrc_ctrl = ddr_readl(DDRC_CTRL);
		ddrc_ctrl &= ~(1<<5);
		ddr_writel(ddrc_ctrl, DDRC_CTRL);

		while(ddr_readl(DDRC_STATUS) & (1<<2));

		TCSM_DELAY(1000);
		TCSM_PCHAR('1');
		ddrp_auto_calibration();
		TCSM_PCHAR('2');

		/* restore ddr auto-sr */
		ddr_writel(sleep_param->autorefresh, DDRC_AUTOSR_EN);
		TCSM_PCHAR('3');

		/* restore ddr LPEN */
		ddr_writel(sleep_param->dlp, DDRC_DLP);

		/* restore ddr deep power down state */
		ddrc_ctrl = ddr_readl(DDRC_CTRL);
		ddrc_ctrl |= sleep_param->pdt;
		ddrc_ctrl |= sleep_param->dpd;
		ddr_writel(ddrc_ctrl, DDRC_CTRL);

		TCSM_PCHAR('4');
	}


	TCSM_PCHAR('5');

	__asm__ volatile(
		".set push	\n\t"
		".set mips32r2	\n\t"
		"jr.hb %0	\n\t"
		"nop		\n\t"
		".set pop 	\n\t"
		:
		: "r" (restore_goto)
		:
		);
}

static inline void m_flush_icache_all(void)
{
    unsigned int addr, t = 0;
    unsigned long lsize = cpu_icache.linesz;
    unsigned long cache_size = cpu_icache.size;

    for (addr = CKSEG0; addr < CKSEG0 + cache_size;
         addr += lsize) {
        cache_op(INDEX_INVALIDATE_I, addr);
    }

    /* invalidate btb */
    __asm__ __volatile__(
        "mfc0 %0, $16, 7\n\t"
        "nop\n\t"
        "ori %0,2\n\t"
        "mtc0 %0, $16, 7\n\t"
        :
        : "r" (t));
}

static inline void m_flush_dcache_all(void)
{
    unsigned int addr;
    unsigned long lsize = cpu_dcache.linesz;
    unsigned long cache_size = cpu_dcache.size;

    for (addr = CKSEG0; addr < CKSEG0 + cache_size;
         addr += lsize) {
        cache_op(INDEX_WRITEBACK_INV_D, addr);
    }

    unsigned long ssize = cpu_scache.linesz;
    cache_size = cpu_scache.size;
    for (addr = CKSEG0; addr < CKSEG0 + cache_size;
         addr += ssize) {
        cache_op(INDEX_WRITEBACK_INV_SD, addr);
    }

    fast_iob();
}

static noinline void cpu_sleep(void)
{
	sleep_param->cpu_div = cpm_inl(CPM_CPCCR) & 0xf;

	m_flush_icache_all();

	m_flush_dcache_all();

	{
		/* before power down cpu by set PD in OPCR, reduce cpu's frequency as the same as L2C's freq */
		unsigned int val, div;

		/* change disable */
		val = cpm_inl(CPM_CPCCR);
		val &= ~(1 << 22);
		cpm_outl(val, CPM_CPCCR);

		/* div cpu = div l2c */
		div = val & (0xf << 4);
		val &= ~0xf;
		val |= (div >> 4);
		cpm_outl(val, CPM_CPCCR);

		/* change enable */
		val |= (1 << 22);
		cpm_outl(val, CPM_CPCCR);

		while (cpm_inl(CPM_CPCSR) & 1);
	}

	TCSM_PCHAR('D');

 	{
		unsigned int tmp;
		unsigned int ddrc_ctrl;

		ddrc_ctrl = ddr_readl(DDRC_CTRL);

		/* save ddr low power state */
		sleep_param->pdt = ddrc_ctrl & DDRC_CTRL_PDT_MASK;
		sleep_param->dpd = ddrc_ctrl & DDRC_CTRL_DPD;
		sleep_param->dlp = ddr_readl(DDRC_DLP);
		sleep_param->autorefresh = ddr_readl(DDRC_AUTOSR_EN);

		/* ddr disable deep power down */
		ddrc_ctrl &= ~(DDRC_CTRL_PDT_MASK);
		ddrc_ctrl &= ~(DDRC_CTRL_DPD);
		ddr_writel(ddrc_ctrl, DDRC_CTRL);

		/* ddr diasble LPEN*/
		ddr_writel(0, DDRC_DLP);

		/* ddr disable auto-sr */
		ddr_writel(0, DDRC_AUTOSR_EN);
		tmp = *(volatile unsigned int *)0xa0000000;

		/* DDR self refresh */
		ddrc_ctrl = ddr_readl(DDRC_CTRL);
		ddrc_ctrl |= 1 << 5;
		ddr_writel(ddrc_ctrl, DDRC_CTRL);
		while(!(ddr_readl(DDRC_STATUS) & (1<<2)));


		/* bufferen_core = 0 */
		tmp = rtc_read_reg(0xb0003048);
		tmp &= ~(1 << 21);
		rtc_write_reg(0xb0003048, tmp);


		/* dfi_init_start = 1 */
		*(volatile unsigned int *)0xb3012000 |= (1 << 3);

		{
			int i;
			for (i = 0; i < 4; i++) {

				__asm__ volatile("ssnop\t\n");
				__asm__ volatile("ssnop\t\n");
				__asm__ volatile("ssnop\t\n");
				__asm__ volatile("ssnop\t\n");
				__asm__ volatile("ssnop\t\n");
				__asm__ volatile("ssnop\t\n");
				__asm__ volatile("ssnop\t\n");
				__asm__ volatile("ssnop\t\n");
			}
		}

		/* disable pll */
		reg_ddr_phy(0x21) |= (1 << 1);
	}

	TCSM_PCHAR('W');

	if (sleep_param->gpio >= 0) {
		sleep_param->cpccr = cpm_inl(CPM_CPCCR);
		/* set cpu clk to 24M */
		cpm_outl(0x55700000,CPM_CPCCR);
		while((cpm_inl(CPM_CPCSR) & 0xf0000007) != 0xf0000000);
		/* reduce voltage */
		gpio_outl(1 << (sleep_param->gpio % 32), sleep_param->gpio / 32,  sleep_param->gpio_level ? PXPAT0S : PXPAT0C);
	}

	__asm__ volatile(
		".set push	\n\t"
		".set mips32r2	\n\t"
		"wait		\n\t"
		"nop		\n\t"
		"nop		\n\t"
		"nop		\n\t"
		".set pop	\n\t"
	);

	TCSM_PCHAR('N');

	TCSM_PCHAR('\r');
	TCSM_PCHAR('\n');
	TCSM_PCHAR('?');
	TCSM_PCHAR('?');
	TCSM_PCHAR('?');
	TCSM_PCHAR('\r');
	TCSM_PCHAR('\n');

	__asm__ volatile(
		".set push	\n\t"
		".set mips32r2	\n\t"
		"jr.hb %0	\n\t"
		"nop		\n\t"
		".set pop 	\n\t"
		:
		: "r" (SLEEP_CPU_RESUME_BOOTUP_TEXT)
		:
		);
	TCSM_PCHAR('F');
}


int soc_pm_enter_sleep(void)
{
	unsigned long flags;

	os_stop_other_cpu();

	local_irq_save(flags);

	ext_intc_writel(intc_readl(INTC_ICMR0), INTC_ICMR0);
	ext_intc_writel(intc_readl(INTC_ICMR1), INTC_ICMR1);

	soc_pm_sleep();

	load_func_to_tcsm((unsigned int *)SLEEP_CPU_RESUME_BOOTUP_TEXT, (unsigned int *)cpu_resume_bootup, SLEEP_CPU_RESUME_BOOTUP_LEN);
	load_func_to_tcsm((unsigned int *)SLEEP_CPU_RESUME_TEXT, (unsigned int *)cpu_resume, SLEEP_CPU_RESUME_LEN);
	load_func_to_tcsm((unsigned int *)SLEEP_CPU_SLEEP_TEXT, (unsigned int *)cpu_sleep, SLEEP_CPU_SLEEP_LEN);

	/* set reset entry */
	*(volatile unsigned int *)0xb2200f00 = SLEEP_CPU_RESUME_BOOTUP_TEXT;

#ifdef DEBUG_PM
	printf("LCR: %08lx\n", cpm_inl(CPM_LCR));
	printf("OPCR: %08lx\n", cpm_inl(CPM_OPCR));
	x2000_pm_gate_check();
#endif

	mb();
	save_goto((unsigned int)SLEEP_CPU_SLEEP_TEXT);
	mb();

	soc_post_wakeup();

	ext_intc_writel(0xFFFFFFFF, INTC_ICMR0);
	ext_intc_writel(0xFFFFFFFF, INTC_ICMR1);

	local_irq_restore(flags);

	os_start_other_cpu();

	return 0;
}
