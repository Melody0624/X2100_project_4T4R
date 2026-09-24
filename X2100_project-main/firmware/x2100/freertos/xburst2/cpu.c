#include "asm/mipsregs.h"
#include <soc/ccu.h>
#include <common.h>
#include <driver/cache.h>
#include <asm/barrier.h>
#include <soc/cpu_regs.h>

static unsigned int fcsr;
#ifdef __mips_msa
static unsigned int msa_csr;
#endif

void arch_enable_simd(void)
{
    unsigned long config = read_c0_config5();

    set_bit_field(&config, CONFIG5_MSAEn, 1);

    write_c0_config5(config);
    set_c0_status(ST0_CU2);
#ifdef __mips_msa
    //write_msa_register(MSA_CSR, 0);
    msa_csr = read_msa_register(MSA_CSR);
#endif
}

void arch_disable_simd(void)
{
    unsigned long config = read_c0_config5();

    set_bit_field(&config, CONFIG5_MSAEn, 0);

    clear_c0_status(ST0_CU2);
    write_c0_config5(config);
}

void arch_enable_fpu(void)
{
    /* 使能fpu */
    set_c0_status(ST0_CU1);
    fcsr = read_32bit_cp1_register(CP1_STATUS);
}

void arch_disable_fpu(void)
{
    /* 使能fpu */
    clear_c0_status(ST0_CU1);
}

int arch_get_cpu_id(void)
{
    unsigned long ebase = read_c0_ebase();

    return get_bit_field(&ebase, EBASE_CPUNum);
}

static unsigned long ebase = 0;
static unsigned int status = 0;

static void save_c0_status(void)
{
    ebase = read_c0_ebase();
    status = read_c0_status();
}

void restore_c0_status(void)
{
    write_c0_status(status);

    write_c0_ebase(ebase);

    flush_cache_all();
}

void soc_init_cpu_regs(struct cpu_regs *regs)
{
    regs->fcsr = fcsr;
#ifdef __mips_msa
    regs->msa_csr = msa_csr;
#endif
}

void arch_init_cpu(void)
{
    int id;

    // 保存 C0 状态
    save_c0_status();

    // 获取 CPU id
    id = arch_get_cpu_id();

    // 可以响应 INTC 中断
    ccu_set_bit(CCU_PIMR, id, 1);

    // 可以响应 Mailbox 中断
    ccu_set_bit(CCU_MIMR, id, 1);

    // 可以响应 OST 中断
    ccu_set_bit(CCU_OIMR, id, 1);

    // Disable IFU Small Buffer, system low power opt
    unsigned long config7 = read_c0_config7();
    set_bit_field(&config7, 3, 5, 7);
    write_c0_config7(config7);

    // 设置kseg0 的cache 属性是 Cacheable, Write-back, write-allocate
    unsigned long config = read_c0_config();
    set_bit_field(&config, CONFIG_K0, 3);
    write_c0_config(config);

    // 关闭cpu计数器
    unsigned long cause = read_c0_cause();
    set_bit_field(&cause, CAUSEB_DC, CAUSEB_DC, 0);
    write_c0_cause(cause);
}

void arch_init_other_cpu(void)
{
    // 获取 CPU id
    int id = arch_get_cpu_id();

    // 可以响应 Mailbox 中断
    ccu_set_bit(CCU_MIMR, id, 1);

    // Disable IFU Small Buffer, system low power opt
    unsigned long config7 = read_c0_config7();
    set_bit_field(&config7, 3, 5, 7);
    write_c0_config7(config7);

    // 设置kseg0 的cache 属性是 Cacheable, Write-back, write-allocate
    unsigned long config = read_c0_config();
    set_bit_field(&config, CONFIG_K0, 3);
    write_c0_config(config);

    // 关闭cpu计数器
    unsigned long cause = read_c0_cause();
    set_bit_field(&cause, CAUSEB_DC, CAUSEB_DC, 0);
    write_c0_cause(cause);
}

void arch_deinit_cpu(void)
{
    // 恢复 C0 状态
    restore_c0_status();
}

#include <cpu/cpu.h>
#include <soc/ccu.h>

void arch_irq_init_other_cpu(void);
void arch_flush_l1_dcache_all(void);

/* CPU1 runs C code directly after boot, so reserve enough stack for libc paths. */
#define BOOT_CPU_STACK_SIZE 8192
__align(8) unsigned char cpu_little_stack[BOOT_CPU_STACK_SIZE];
unsigned long init_func[2];

void do_bootup_cpu(void)
{
    int id = arch_get_cpu_id();

    if (id == 0)
        arch_init_cpu();
    else
        arch_init_other_cpu();

    /*
     * 这里只需初始化cpu相关的irq
     */
    arch_irq_init_other_cpu();

    void (*func)(void) = (void *)init_func[id];
    func();

    panic("why we are here\n");
}

static void bootup_cpu(void)
{
    asm volatile (
    "    .set    push                        \n"
    "    .set    reorder                     \n"
    "    .set    noat                        \n"
    "    la    $29, cpu_little_stack+%0     \n"
    "    la    $28, __global_pointer$       \n"
    "    j do_bootup_cpu   \n"
    "    nop                    \n"
    "    .set    pop                         \n"
    :
    : "i"(BOOT_CPU_STACK_SIZE)
    : "memory"
    );
}

void arch_startup_cpu(int cpu_id, unsigned long init_func_address)
{
    assert_range(cpu_id, 0, 1);

    os_enter_critical();
    init_func[cpu_id] = init_func_address;

    flush_dcache_all();

    // 清除可能残留的 mailbox中断
    ccu_write_reg(CCU_MBR(cpu_id), 0);

    ccu_write_reg(CCU_RER, (unsigned long)bootup_cpu);
    ccu_set_bit(CCU_CSRR, cpu_id, 1);

    wmb();
    udelay(1);
    ccu_set_bit(CCU_CSRR, cpu_id, 0);
    wmb();
    os_exit_critical();
}

void arch_shutdown_cpu_prepare(int cpu_id)
{
    // 取消响应 INTC 中断
    ccu_set_bit(CCU_PIMR, cpu_id, 0);

    // 取消响应 Mailbox 中断
    ccu_set_bit(CCU_MIMR, cpu_id, 0);

    // 取消响应 OST 中断
    ccu_set_bit(CCU_OIMR, cpu_id, 0);

    wmb();

    arch_flush_l1_dcache_all();

    udelay(1);

    asm __volatile__ (
                    "sync\n\t"
                    "nop\n\t"
                    "nop\n\t"
                    "nop\n\t"
                    "wait\n\t"
                    "nop\n\t"
                    "nop\n\t"
                    "nop\n\t"
                    "nop\n\t"
                    "nop\n\t"
                    "nop\n\t"
                    "nop\n\t"
    );

    panic("can't be here\n");
}

void arch_shutdown_cpu(int cpu_id)
{
    uint64_t start = systick_get_time_us();

    while (!ccu_get_bit(CCU_CSSR, cpu_id)) {
        if (systick_get_time_us() - start >= 10*1000)
            panic("shutdown cpu%d tiemout\n", cpu_id);
    }

    udelay(1);

    ccu_set_bit(CCU_CSRR, cpu_id, 1);
    wmb();
}

void arch_shutdown_current_cpu(void)
{
    volatile unsigned long *rtos_start = (unsigned long *)CONFIG_OS_MEM_ADDR;

    *rtos_start = 1;

    arch_shutdown_cpu_prepare(1);
}
