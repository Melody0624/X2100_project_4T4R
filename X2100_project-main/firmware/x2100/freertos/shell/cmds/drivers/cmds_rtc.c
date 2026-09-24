#include <shell.h>
#include <driver/rtc.h>
#include <string.h>
#include <stdlib.h>

static void cmd_func_rtc_help(char *cmd)
{
    shell_printf("Set RTC time\n");
    shell_printf("\tUsage: %s set_time [year] [mon] [day] [hour] [min] [sec]\n", cmd);
    shell_printf("Get RTC time\n");
    shell_printf("\tUsage: %s get_time \n", cmd);
    shell_printf("Set RTC alarm\n");
    shell_printf("\tUsage: %s set_alarm [second] \n", cmd);

    shell_printf("Example:\n");
    shell_printf("\t%s set_time 2019 5 20 23 58 59\n", cmd);
    shell_printf("\t%s get_time\n", cmd);
    shell_printf("\t%s set_alarm 5\n", cmd);
}

static void alarm_callback(void)
{
    printf("RTC alarm up！\n");
}

void cmd_func_rtc(struct cmd_arg *arg, int argc, char **argv)
{
    int sec, ret;
    struct rtc_time tm;

    if(argc < 2)
        goto rtc_err;

    /*set time*/
    if (strcmp(argv[1], "set_time") == 0) {

        if (argc != 8)
            goto rtc_err;

        ret = sscanf(argv[7], "%d", &tm.tm_sec);
        if(ret != 1 || tm.tm_sec < 0)
            goto rtc_err;

        ret = sscanf(argv[6], "%d", &tm.tm_min);
        if(ret != 1 || tm.tm_min < 0)
            goto rtc_err;

        ret = sscanf(argv[5], "%d", &tm.tm_hour);
        if(ret != 1 || tm.tm_hour < 0)
            goto rtc_err;

        ret = sscanf(argv[4], "%d", &tm.tm_mday);
        if(ret != 1 || tm.tm_mday < 0)
            goto rtc_err;

        ret = sscanf(argv[3], "%d", &tm.tm_mon);
        if(ret != 1 || tm.tm_mon < 0)
            goto rtc_err;

        ret = sscanf(argv[2], "%d", &tm.tm_year);
        if(ret != 1 || tm.tm_year < 0)
            goto rtc_err;

        rtc_set_tm(&tm);
        rtc_show_current_tm(&tm);
    } else if (strcmp(argv[1], "get_time") == 0) {
        rtc_show_current_tm(&tm);
    } else if (strcmp(argv[1], "set_alarm") == 0) {
        if (argc != 3)
            goto rtc_err;

        ret = sscanf(argv[2], "%d", &sec);
        if (ret != 1 || sec < 0)
            goto rtc_err;

        rtc_set_alarm(sec, alarm_callback);
    } else {
        goto rtc_err;
    }

    return;

rtc_err:
    cmd_func_rtc_help(argv[0]);
}

void cmd_rtc_init(void)
{
    shell_cmd_register(cmd_func_rtc,    "rtc",   NULL,    "set rtc time or alarm and get rtc time");
}