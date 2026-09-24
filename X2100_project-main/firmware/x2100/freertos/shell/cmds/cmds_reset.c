#include <shell.h>
#include <os.h>
#include <wake_lock.h>
#include <printf.h>
#include <driver/watchdog.h>

static DEFINE_CRITICAL_THREADCOND(cond);

static void cmd_func_reset_help(char *cmd)
{
    shell_printf("Usage:%s [second]\n", cmd);
    shell_printf("Example:\n");
    shell_printf("\t%s 10\n", cmd);
    shell_printf("\t%s\n", cmd);
}

static void cmd_reset(struct cmd_arg *cmd_arg, int argc, char **argv)
{
    unsigned int sec;
    int ret;

    if (argc > 2)
        goto reset_err;

    if (argc == 2)
        sscanf(argv[1], "%d", &sec);
    else
        sec = 0;

    if (sec) {
        wdt_stop();

        wdt_start(sec * 1000);
    } else {
        reset();
    }

    os_enter_critical();

    ret = critical_thread_cond_wait_timeout(&cond, sec * 1500);
    if (ret)
        shell_printf("%s: reset faild ！\n", argv[0]);

    shell_printf("\n");

    os_exit_critical();
    return;

reset_err:
    shell_printf("Wrong input parameters\n");
    cmd_func_reset_help(argv[0]);
}

void cmd_reset_init(void)
{
    shell_cmd_register(cmd_reset, "reset", NULL, "reset system");
}