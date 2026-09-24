/*
 * FreeRTOS Kernel V10.0.1
 * Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 * 1 tab == 4 spaces!
 */


/*-----------------------------------------------------------
 * Implementation of functions defined in portable.h for the ARM7 port.
 *
 * Components that can be compiled to either ARM or THUMB mode are
 * contained in this file.  The ISR routines, which can only be compiled
 * to ARM mode are contained in portISR.c.
 *----------------------------------------------------------*/

#include <stdlib.h>
#include <asm/mipsregs.h>
#include <driver/hrtimer.h>
#include <driver/irq.h>
#include <asm_symbol.h>
#include <common.h>
#include "FreeRTOS.h"
#include "task.h"
#include <cpu/cpu.h>
#include <os.h>

/*
 * The EXL bit is set to ensure interrupts do not occur while the context of
 * the first task is being restored.
 */
#define portINITIAL_SR                  (ST0_IE | ST0_EXL)

static struct hrtimer os_tick_timer;

/*
 * Setup the timer to generate the tick interrupts.
 */
static void prvSetupTimerInterrupt( void );

/*
 * The scheduler can only be started from ARM mode, so
 * vPortISRStartFirstSTask() is defined in portISR.c.
 */
static void vPortISRStartFirstTask_C( void )
{
#ifndef CONFIG_FPU_ALWAYS_ON
    arch_disable_fpu();

#ifdef __mips_msa
    arch_disable_simd();
#endif

#endif

    /* stack size = 36*4 */
    asm __volatile__ (
                        "la     $26, pxCurrentTCB   \n\t"  /* 获取 pxCurrentTCB的地址 到k0 */
                        "lw     $26, 0($26)         \n\t"  /* 将 k0+0 的值赋值为 k0 */
                        "lw     $29, 0($26)         \n\t"  /* 将 k0+0 的值 pxTopOfStack 赋值为 sp */
#ifdef CONFIG_FPU_ALWAYS_ON
                        "addiu  $29, $29, 4*34      \n\t"  /* 跳过 fpu 寄存器 */
#endif
                        "lw     $28, (28*4)($29)    \n\t"  /* Restore gp */
                        "lw     $4,  (4*4)($29)     \n\t"  /* Restore a0 */
                        "lw     $5,  (5*4)($29)     \n\t"  /* Restore a1 */
                        "lw     $31, (31*4)($29)    \n\t"  /* Restore ra */
                        "lw     $26, (34*4)($29)    \n\t"  /* Restore EPC */
                        "mtc0   $26, $14            \n\t"
                        "addiu  $29, $29, 4*36      \n\t"  /* sp */
                        "ehb                        \n\t"
                        "ei                         \n\t"  /* 开中断 */
                        "eret                       \n\t"
                        "nop                        \n\t"
            );
}

void end_of_thread(void)
{
    panic("why i am here\n");
}

static void start_a_thread(TaskFunction_t pxCode, void *pvParameters)
{
    pxCode(pvParameters);
#ifdef CONFIG_OS
    thread_delete(NULL);
#else
    vTaskDelete(NULL);
#endif
}

static inline StackType_t prvExpectedGP( void )
{
    StackType_t gp;

    __asm__ volatile ( "la %0, __global_pointer$" : "=r"( gp ) );

    return gp;
}

/*
 * Initialise the stack of a task to look exactly as if a call to
 * portSAVE_CONTEXT had been called.
 *
 * See header file for description.
 */
StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack,
                    TaskFunction_t pxCode,
                    void *pvParameters )
{
    static unsigned char c = 0xa5;

    pxTopOfStack -= 36; /* $0 ~ $31 status epc hi lo */

    memset(pxTopOfStack, c, 32 * 4);
    c++;

    pxTopOfStack[ 28 ] = prvExpectedGP();

    unsigned int status = read_c0_status() | portINITIAL_SR;
#ifndef CONFIG_FPU_ALWAYS_ON
    status &= ~(ST0_CU1 | ST0_CU2);
#endif

    /* Enable interrupt */
    pxTopOfStack[35] = status; /* CP0_STATUS */
    pxTopOfStack[34] = (StackType_t) start_a_thread;      /* CP0_EPC */
    pxTopOfStack[33] = (StackType_t) 0xDEADBEEF;          /* hi */
    pxTopOfStack[32] = (StackType_t) 0xDEADBEEF;          /* lo */
    pxTopOfStack[31] = (StackType_t) end_of_thread; // ra
    pxTopOfStack[4] = (StackType_t) pxCode; // a0
    pxTopOfStack[5] = (StackType_t) pvParameters; // a1

#ifdef CONFIG_FPU_ALWAYS_ON
    pxTopOfStack -= 34;
    pxTopOfStack[32] = 0; // fpu status
#endif

    return pxTopOfStack;
}

BaseType_t xPortStartScheduler( void )
{
    /* Setup the timer to generate the tick.
     * Interrupts will have been
     * disabled by the time we get here.
     */
    request_irq(IRQ_V_IP0, 0, (irq_handler_t)sw0_irq_handler, "SW0_int", NULL);

    prvSetupTimerInterrupt();
    vPortISRStartFirstTask_C();

    /*
     * Should not get here never!
     */
    return 0;
}

void vPortEndScheduler( void )
{
  /*
   * It is unlikely that the ARM port will require this function
   * as there is nothing to return to.
   */
    panic("todo vPortEndScheduler!\n");
}

void vPortDisableInterruptsFrom ( void )
{
    asm __volatile__ (  "ehb    \n\t"
                        "di     \n\t"
                        "nop    \n\t");
}

void vPortEnableInterruptsFrom ( void )
{
    asm __volatile__ (  "ehb    \n\t"
                        "ei     \n\t"
                        "nop    \n\t");
}

static inline void do_yield(void)
{
    set_c0_cause(CAUSEF_IP0);
}

void vPortYield ( void )
{
    portENTER_CRITICAL();

    do_yield();

    portEXIT_CRITICAL();
}

static uint64_t last_time;

#define TICK_PERIOD_US (1000000 / configTICK_RATE_HZ)
#define MAX_SLEEP_TICKS (0xffffffff / TICK_PERIOD_US)

static void os_tick_timer_callback(struct hrtimer *timer)
{
    if (xTaskIncrementTick()) {
        do_yield();
    }

    uint64_t now = systick_get_time_us();

    last_time += TICK_PERIOD_US;

    if (now >= last_time + TICK_PERIOD_US)
        last_time = now;

    hrtimer_restart_at_expires(timer, last_time + TICK_PERIOD_US);
}

static void prvSetupTimerInterrupt( void )
{
    last_time = systick_get_time_us();
    hrtimer_init(&os_tick_timer, os_tick_timer_callback);
    hrtimer_start_at_expires(&os_tick_timer, last_time + TICK_PERIOD_US);
}

extern uint64_t os_total_idle_time;

void vTaskStepTickFixed(TickType_t xTicksToJump);

void vPortSuppressTicksAndSleep( TickType_t xExpectedIdleTime )
{
    local_irq_disable();

    uint64_t now, old;
    TickType_t ticks;

    /*
     * 如果有任务需要被响应，直接返回
     */
    if (eTaskConfirmSleepModeStatus() == eAbortSleep)
        goto out;

    if (xExpectedIdleTime > MAX_SLEEP_TICKS)
        xExpectedIdleTime = MAX_SLEEP_TICKS;

    old = systick_get_time_us();

    /*
     * 如果休眠的时间在5us以内，直接返回
     */
    if (old > last_time + xExpectedIdleTime * TICK_PERIOD_US - 5)
        goto out;

    /*
     * 设置希望的定时，只在idle时有效
     */
    hrtimer_start_at_expires(&os_tick_timer,
        last_time + xExpectedIdleTime * TICK_PERIOD_US);

    unsigned int t = 0;

    /* invalidate btb */
    __asm__ __volatile__(
        "mfc0 %0, $16, 7\n\t"
        "nop\n\t"
        "ori %0,2\n\t"
        "mtc0 %0, $16, 7\n\t"
        :
        : "r" (t));

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

    now = systick_get_time_us();
    ticks = (TickType_t)(now - last_time) / TICK_PERIOD_US;

    if (ticks > xExpectedIdleTime)
        ticks = xExpectedIdleTime;

    /*
     * 统计idle时间
     */
    os_total_idle_time += now - old;

    last_time = now - (TickType_t)(now - last_time) % TICK_PERIOD_US;

    if (ticks > 0) {
        /*
         * 此函数弥补了 vTaskStepTick(ticks) 的功能缺陷
         * 在其中调用了 xTaskIncrementTick(), 保证正常运行
         */
        vTaskStepTickFixed(ticks);
    }

    /*
     * 启动一个 tick 的时间
     */
    hrtimer_start_at_expires(&os_tick_timer,
        last_time + TICK_PERIOD_US);

out:
    local_irq_enable();
}

void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
    int thread_id = (int)thread_get_user_data((void *)xTask);

    panic("stack overfollow, please check ! thread_name = %s, pthread_t = %x, thread_ptr_t = %x\n",\
        pcTaskName, thread_id, (int)xTask);
}

void PortShowStackTrace(unsigned int *pxTopOfStack)
{
    show_stacktrace((unsigned long)&pxTopOfStack[36], pxTopOfStack[34], pxTopOfStack[31]);
}
