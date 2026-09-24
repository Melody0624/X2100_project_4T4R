#include <driver/cache.h>
#include <driver/irq.h>
#include <os.h>
#include "asm/mipsregs.h"
#include "common.h"
#include "lds_symbol.h"
#include "asm_symbol.h"

#ifdef CONFIG_TCSM_SECTION_IRQ
#include <cpu/tcsm_section.h>
#else
#include <tcsm_section_null.h>
#endif


struct pt_regs {
    unsigned long regs[32];
    unsigned long lo;
    unsigned long hi;
    unsigned long epc;
    unsigned long status;
    unsigned long badva;
    unsigned long cause;
};

void (*exception_handlers[32])(void);

static unsigned int save_vector_irq;

void enable_vect_int_irq(int irq)
{
    set_c0_status(STATUSF_IP0 << (irq - IRQ_V_IP0));
    save_vector_irq |= 1 << (irq - IRQ_V_IP0);
}

void disable_vect_int_irq(int irq)
{
    clear_c0_status(STATUSF_IP0 << (irq - IRQ_V_IP0));
    save_vector_irq &= ~(1 << (irq - IRQ_V_IP0));
}

void save_and_disable_other_vect_int_irqs(unsigned int not_disable_irqs)
{
    unsigned int status = read_c0_status();
    save_vector_irq = (status & ST0_IM) >> STATUSB_IP0;
    not_disable_irqs &= save_vector_irq;
    status &= ~ST0_IM;
    status |= not_disable_irqs << STATUSB_IP0;
    write_c0_status(status);
}

void restore_save_vect_int_irqs(void)
{
    unsigned status = read_c0_status();
    status &= ~ST0_IM;
    status |= save_vector_irq << STATUSB_IP0;
    write_c0_status(status);
}

void set_exception_handler(unsigned int i, void (*handler)(void))
{
    assert(i < 32);
    exception_handlers[i] = handler;
}

static const char *exception_names[32] = {
    [0] = "interrupt",
    [1] = "TLB modification",
    [2] = "TLB (load or instruction fetch)",
    [3] = "TLB (store)",
    [4] = "Address error (load or instruction fetch)",
    [5] = "Address error (store)",
    [8] = "Syscall",
    [9] = "Breakpoint",
    [10] = "Reserve red instruction",
    [11] = "Coprocessor unusable",
    [12] = "Integer overflow",
    [13] = "Trap",
    [15] = "Floating point",
    [21] = "MSA Disabled exception",
    [23] = "Reference to WatchHi/WatchLo address",
    [24] = "Machine Check",
};

#include <cpu/cpu.h>

/*
 * 被 handle_exceptions_default 调用
 */
void do_exception_default(struct pt_regs *regs)
{
    unsigned int code = get_bit_field(&regs->cause, 2, 6);

    os_enter_critical();

    printf("This is %s expection. code: %d cpu%d\n", exception_names[code], code, arch_get_cpu_id());
    printf("epc: %08lx badva: %08lx\n", regs->epc, regs->badva);

    unsigned long *r = regs->regs;
    printf("zero at v0 v1: %08lx, %08lx, %08lx, %08lx\n", 0l, r[1], r[2], r[3]);
    printf("  a0 a1 a2 a3: %08lx, %08lx, %08lx, %08lx\n", r[4], r[5], r[6], r[7]);
    printf("  t0 t1 t2 t3: %08lx, %08lx, %08lx, %08lx\n", r[8], r[9], r[10], r[11]);
    printf("  t4 t5 t6 t7: %08lx, %08lx, %08lx, %08lx\n", r[12], r[13], r[14], r[15]);
    printf("  s0 s1 s2 s3: %08lx, %08lx, %08lx, %08lx\n", r[16], r[17], r[18], r[19]);
    printf("  s4 s5 s6 s7: %08lx, %08lx, %08lx, %08lx\n", r[20], r[21], r[22], r[23]);
    printf("  t8 t9 k0 k1: %08lx, %08lx, %08lx, %08lx\n", r[24], r[25], r[26], r[27]);
    printf("  gp sp s8 ra: %08lx, %08lx, %08lx, %08lx\n", r[28], r[29], r[30], r[31]);

    printf("cause: %08lx status: %08lx\n", regs->cause, regs->status);
    printf("lo: %08lx hi: %08lx\n", regs->lo, regs->hi);

#ifdef CONFIG_DUMP_STACK
    show_stacktrace(regs->regs[29], regs->epc, regs->regs[31]);
#endif

    /*
     * Game over - no way to handle this if it ever occurs. Most probably
     * caused by a new unknown cpu type or after another deadly
     * hard/software error.
     */
    printf("Game over\n");

    os_exit_critical();

    while(1);
}
/*-----------------------------------------------------------*/

static void init_default_handlers(void)
{
    int i;

    for (i = 0; i < 32; i++)
        exception_handlers[i] = handle_exceptions_default;

    /*
     * 设置总中断处理函数,异常0
     * 这个函数定义在汇编文件中,
     * 此函数会调用到 handle_int_c()
     */
    exception_handlers[0] = handle_int;

#ifdef CONFIG_OS
    /*
     * fpu/mxu 异常处理
     * 并不是每次切换线程都保存 fpu/mxu 的寄存器
     */
    exception_handlers[11] = handle_cpu_unusable;
#ifdef __mips_msa
    exception_handlers[21] = handle_msa_disabled;
#endif
#endif
}

#ifdef CONFIG_TCSM_SECTION_IRQ
extern unsigned char tcm_exception_section[];
#endif

void init_exception_section(void)
{
#ifdef CONFIG_TCSM_SECTION_IRQ
    unsigned char *section = (void *)tcm_exception_section;
#else
    unsigned char *section = (void *)&__exception_section_start;
#endif

    /*
     * 拷贝异常处理函数到所有可能的异常入口
     */
    memcpy(section + 0x00, exception_entry, 0x20);
    memcpy(section + 0x180, exception_entry, 0x20);
    memcpy(section + 0x200, exception_entry, 0x20);

#ifdef DEBUG
    dump_mem32(section + 0x00,  0x20, 8);
    dump_mem32(section + 0x180, 0x20, 8);
    dump_mem32(section + 0x200, 0x20, 8);
#endif

    flush_cache_all();
}

static void init_c0_regs(void)
{

#ifdef CONFIG_TCSM_SECTION_IRQ
    unsigned char *section = (void *)tcm_exception_section;
#else
    unsigned char *section = (void *)&__exception_section_start;
#endif

    /*
     * 重设异常向量表的位置
     */
    write_c0_ebase((unsigned int)section | EBASEF_WG);

    unsigned int status = read_c0_status();

    /* 修改异常向量表的位置
     * bootstrap -> normal
     */
    clear_bits(status, ST0_BEV);

    /* 从reset模式切换到正常模式
     */
    clear_bits(status, ST0_ERL);

    /* 允许异常产生
     */
    clear_bits(status, ST0_EXL);

    /* 允许中断异常
     */
    set_bits(status, ST0_IE);

    /* 使能 SW0 中断
     */
    set_bits(status, STATUSF_IP0);

    /* 禁止 timer 中断
     */
    clear_bits(status, STATUSF_IP7);

    write_c0_status(status);
    asm ("ehb\n\t");
}

void arch_irq_init(void)
{
    /* 初始化异常向量表
     */
    init_exception_section();

    /* 初始化默认的异常/中断处理函数
     */
    init_default_handlers();

    init_c0_regs();

    irq_dispatcher_init();

    soc_irq_init();
}

void arch_irq_init_other_cpu(void)
{
    init_c0_regs();
}
