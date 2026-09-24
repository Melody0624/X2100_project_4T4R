#include <shell.h>
#include <driver/pwm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <driver/gpio.h>

static void pwm_config_help(char *cmd)
{
    shell_printf("Usage: %s <gpio> <freq> <max_level> [idle_level] \n", cmd);
    shell_printf("Example:\n");
    shell_printf("\t%s PC25 2000000 300 1\n", cmd);
}

static void pwm_setlevel_help(char *cmd)
{
    shell_printf("Usage: %s <pwm_id> <level> \n", cmd);
    shell_printf("Example:\n");
    shell_printf("\t%s 0 200\n", cmd);
}

static void pwm_release_help(char *cmd)
{
    shell_printf("Usage: %s <pwm_id> \n", cmd);
    shell_printf("\tRELEASE pwm resource\n");
    shell_printf("Example:\n");
    shell_printf("\t%s 0\n", cmd);
}

void cmd_func_pwm_release(struct cmd_arg *arg, int argc, char **argv)
{
    int id, ret;

    if (argc != 2)
        goto pwm_release_err;

    ret = sscanf(argv[1], "%d", &id);
    if (ret != 1)
        goto pwm_release_err;

    pwm_release(id);
    shell_printf("release pwm_%d\n",id);
    return;

pwm_release_err:
    pwm_release_help(argv[0]);
}

void cmd_func_pwm_set_level(struct cmd_arg *arg, int argc, char **argv)
{
    int id, ret;
    long level;

    if (argc != 3)
        goto set_level_err;

    ret = sscanf(argv[1], "%d", &id);
    if (ret != 1)
        goto set_level_err;

    ret = sscanf(argv[2], "%ld", &level);
    if (ret != 1)
        goto set_level_err;

    pwm_set_level(id, level);
    shell_printf("pwm_level = %d\n", level);
    return;

set_level_err:
    pwm_setlevel_help(argv[0]);
}

void cmd_func_pwm_config(struct cmd_arg *arg, int argc, char **argv)
{
    long freq;
    int ret, id, levels, gpio_num;
    int idle_level = PWM_idle_high;

    struct pwm_config_data config;

    if ((argc != 4) && (argc != 5))
        goto pwm_config_err;

    gpio_num = str_to_gpio(argv[1]);
    if (gpio_num < 0)
        goto pwm_config_err;

    id = pwm_request(gpio_num, "cmd_pwm");
    shell_printf("pwm_id = %d\n", id);

    config.idle_level = PWM_idle_high;
    config.clk_id = "exit1";
    config.accuracy_priority = PWM_accuracy_freq_first;

    ret = sscanf(argv[2], "%ld", &freq);
    if (ret != 1 || freq <= 0)
        goto pwm_config_err;

    config.freq = freq;

    ret = sscanf(argv[3], "%d", &levels);
    if (ret != 1 || levels <= 0)
        goto pwm_config_err;

    config.levels = levels;

    shell_printf("pwm_freq = %ld, pwm_levels = %d\n", freq, levels);

    if (argc == 5) {
        idle_level = atoi(argv[4]);
        if(idle_level)
            idle_level = PWM_idle_high;
        else
            idle_level = PWM_idle_low;

        config.idle_level = idle_level;
    }

    shell_printf("idle_level = %s!\n", idle_level ? "PWM_idle_high" : "PWM_idle_low");

    pwm_config(id, &config);

    /* If you want to know the current freq , you can use pwm_get_freq() */
    shell_printf("current freq: %ld\n", pwm_get_freq(id));
    return ;

pwm_config_err:
    pwm_config_help(argv[0]);
}

void cmd_pwm_init(void)
{
    shell_cmd_register(cmd_func_pwm_config,    "pwm_request",   NULL,    "request pwm and config");
    shell_cmd_register(cmd_func_pwm_set_level,    "pwm_setlevel",    NULL,    "set pwm level");
    shell_cmd_register(cmd_func_pwm_release,    "pwm_free",    NULL,    "free pwm");
}