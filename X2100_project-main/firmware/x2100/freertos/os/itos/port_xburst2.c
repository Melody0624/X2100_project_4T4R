
#include <stdlib.h>
#include <asm/mipsregs.h>
#include <driver/hrtimer.h>
#include <driver/irq.h>
#include <asm_symbol.h>
#include <common.h>
#include <cpu/cpu.h>
#include <os.h>

#include "task.h"

#include <soc/ccu.h>

#include <asm/barrier.h>

/*
 * The EXL bit is set to ensure interrupts do not occur while the context of
 * the first task is being restored.
 */
#define portINITIAL_SR                  (ST0_IE | ST0_EXL)

void end_of_thread(void)
{
    panic("thread: it can't be here!\n");
}

static inline unsigned int prvExpectedGP( void )
{
    unsigned int gp;

    __asm__ volatile ( "la %0, __global_pointer$" : "=r"( gp ) );

    return gp;
}

void *port_it_task_init_stack(void *stack_pointer, it_task_entry task_entry, void *user_data)
{
    unsigned int *sp = stack_pointer;
    sp -= 36; /* $0 ~ $31 status epc hi lo */

    unsigned int status = read_c0_status() | portINITIAL_SR;
    status &= ~(ST0_CU1 | ST0_CU2);

    /* Enable interrupt */
    sp[35] = status; /* CP0_STATUS */
    sp[34] = (unsigned int)task_entry;      /* CP0_EPC */
    sp[33] = 0xDEADBEEF;          /* hi */
    sp[32] = 0xDEADBEEF;          /* lo */
    sp[31] = (unsigned int) end_of_thread; // ra
    sp[28] = prvExpectedGP();
    sp[4] = (unsigned int) user_data; // a0

    return sp;
}

int port_it_task_current_cpu_id(void)
{
    return arch_get_cpu_id();
}

void port_it_task_enter_critical(void)
{
    os_enter_critical();
}

void port_it_task_exit_critical(void)
{
    os_exit_critical();
}

void port_it_task_disable_local_irq(void)
{
    local_irq_disable();
}

void port_it_task_enable_local_irq(void)
{
    local_irq_enable();
}

static inline void do_yield(int cpu_id)
{
    ccu_write_reg(CCU_MBR(cpu_id), 1);
    fast_iob();
    asm volatile (
    "    .set    push                        \n"
    "    .set    reorder                     \n"
    "    .set    noat                        \n"
	"    nop                    \n"
    "    nop                    \n"
    "    nop                    \n"
    "    nop                    \n"
    "    nop                    \n"
    "    .set    pop                         \n"
    :
	:
    : "memory"
    );
}

void port_it_task_yield(int cpu_id)
{
    do_yield(cpu_id);
}

void port_it_task_enter_idle(unsigned int ticks)
{
    /*
     * 现在是 idle 的版本
     */
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
}

static uint64_t last_time;
static struct hrtimer os_tick_timer;
static int is_start;

#define TICK_PERIOD_US (1000000/config_it_task_tick_hz)

static void os_tick_timer_callback(struct hrtimer *timer)
{
    it_task_add_ticks(1);

    uint64_t now = systick_get_time_us();

    last_time += TICK_PERIOD_US;

    if (now >= last_time + TICK_PERIOD_US)
        last_time = now;

    hrtimer_restart_at_expires(timer, last_time + TICK_PERIOD_US);
}

void port_it_task_start(int cpu_id)
{
    /* 关中断
     */
    asm __volatile__ (  "ehb    \n\t"
                        "di     \n\t"
                        "nop    \n\t");

    if (!is_start) {
        is_start = 1;
        last_time = systick_get_time_us();
        hrtimer_init(&os_tick_timer, os_tick_timer_callback);
        hrtimer_start_at_expires(&os_tick_timer, last_time + TICK_PERIOD_US);
    }

    request_irq(IRQ_V_IP3, 0, (irq_handler_t)mailbox_irq_handler, "mailbox int", NULL);

    arch_disable_fpu();

#ifdef __mips_msa
    arch_disable_simd();
#endif

    /* stack size = 36*4 */
    asm __volatile__ (
                "move   $4,  %0                \n\t"  /* a0 是 cpu_id */
                "jal    it_task_get_current    \n\t"  /* 获取 当前任务的地址到 v0 */
                "nop                           \n\t"
                "lw     $29, 0($2)             \n\t"  /* 将 v0+0 的值 stack_pointer 赋值为 sp */
                "lw     $28, (28*4)($29)       \n\t"  /* Restore gp */
                "lw     $4,  (4*4)($29)        \n\t"  /* Restore a0 */
                "lw     $5,  (5*4)($29)        \n\t"  /* Restore a1, 这里没有用到 */
                "lw     $31, (31*4)($29)       \n\t"  /* Restore ra */
                "lw     $26, (34*4)($29)       \n\t"  /* Restore EPC */
                "mtc0   $26, $14               \n\t"
                "addiu  $29, $29, 4*36         \n\t"  /* sp */
                "ehb                           \n\t"
                "ei                            \n\t"  /* 开中断 */
                "eret                          \n\t"
                "nop                           \n\t"
                :
                : "r"(cpu_id)
            );

}

static void start_second_cpu(void)
{
    int id = arch_get_cpu_id();

    it_task_start_kernel(id);
}

void port_it_task_startup_cpu(int cpu_id)
{
    arch_startup_cpu(cpu_id, (unsigned long)start_second_cpu);
}

void port_it_task_shutdown_cpu_prepare(int cpu_id)
{
    if (cpu_id == 0)
        panic("can't stop cpu 0\n");

    disable_irq(IRQ_V_IP3);
    release_irq(IRQ_V_IP3);

    local_irq_disable();

    arch_shutdown_cpu_prepare(cpu_id);
}

void port_it_task_shutdown_cpu(int cpu_id)
{
    arch_shutdown_cpu(cpu_id);
}

void PortShowStackTrace(unsigned int *pxTopOfStack)
{
    show_stacktrace((unsigned long)&pxTopOfStack[36], pxTopOfStack[34], pxTopOfStack[31]);
}
