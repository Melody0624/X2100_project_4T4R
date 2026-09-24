#include <stdio.h>
#include <common.h>
#include <os.h>
#include <os/cpux_safe_libc.h>
#include <cpu/cpu.h>

#ifdef CONFIG_XBURST2
static volatile int cpux_printf_test_done;

static void cpux_printf_test_entry(void)
{
    int i;

    cpux_printf("[cpux] printf test start, cpu=%d\n", arch_get_cpu_id());

    for (i = 0; i < 100; i++) {
        cpux_printf("[cpux] printf round=%d, cpu=%d\n", i, arch_get_cpu_id());
        mdelay(20);
    }

    cpux_printf("[cpux] printf test done\n");
    cpux_printf_test_done = 1;
    arch_shutdown_current_cpu();
}
#endif

static void rtos_cpux_printf_test_thread(void *arg)
{
    int i;

    (void)arg;

    printf("[rtos] printf test start, cpu=%d\n", arch_get_cpu_id());
    for (i = 0; i < 5; i++) {
        printf("[rtos] printf round=%d, cpu=%d\n", i, arch_get_cpu_id());
        msleep(20);
    }

#ifdef CONFIG_XBURST2
    cpux_printf_test_done = 0;
    if (cpux_printf_service_init()) {
        printf("[rtos] cpux printf service init failed\n");
        return;
    }

    printf("[rtos] start cpux printf test\n");
    arch_startup_cpu(1, (unsigned long)cpux_printf_test_entry);

    for (i = 0; i < 500; i++) {
        if (cpux_printf_test_done)
            break;
        printf("[rtos] printf round=%d, cpu=%d\n", i, arch_get_cpu_id());
        msleep(10);
    }

    cpux_printf_service_deinit();

    printf("[rtos] cpux printf test %s, ring_drop=%u bytes\n",
           cpux_printf_test_done ? "done" : "timeout",
           cpux_printf_service_drop_bytes());
#endif
}

void cpux_printf_example_init(void)
{
    thread_create("cpux-printf-test", 4096, rtos_cpux_printf_test_thread, NULL);
}
