#include <shell.h>
#include <os.h>
#include <wake_lock.h>
#include <printf.h>
#include <driver/pm.h>
#include <usb/usb.h>
#include <driver/fb.h>

static void cmd_func_sleep_help(char *cmd)
{
    shell_printf("Usage:%s [timeout(ms)]\n", cmd);
    shell_printf("Example:\n");
    shell_printf("\t%s 1000\n", cmd);
    shell_printf("\t%s\n", cmd);
}

static void cmd_sleep(struct cmd_arg *cmd_arg, int argc, char **argv)
{
    unsigned int timeout;

    if (argc > 2)
        goto sleep_err;

    if (argc == 2)
        sscanf(argv[1], "%d", &timeout);
    else
        timeout = 100;

#ifdef CONFIG_FB
    fb_core_suspend();
#endif

    os_enter_critical();

    if (wake_lock_wait_timeout(timeout)) {
        printf("Enter sleep timeout, maybe some wake_lock locked!\n");
        wake_locks_show();
        goto exit_critical;
    }

#ifdef CONFIG_USB_DRIVER
    usb_core_suspend();
#endif

    pm_enter_sleep();

#ifdef CONFIG_USB_DRIVER
    usb_core_resume();
#endif

    shell_printf("\n");

exit_critical:
    os_exit_critical();

#ifdef CONFIG_FB
    fb_core_resume();
#endif

    return;

sleep_err:
    shell_printf("Wrong input parameters\n");
    cmd_func_sleep_help(argv[0]);
}

static void cmd_power_off(struct cmd_arg *arg, int argc, char **argv)
{
    printf("System power off！\n");
    pm_power_off();
}

void cmd_pm_init(void)
{
    shell_cmd_register(cmd_sleep, "sleep", NULL, "enter low power mode");
    shell_cmd_register(cmd_power_off, "power_off", NULL, "power off system");
}