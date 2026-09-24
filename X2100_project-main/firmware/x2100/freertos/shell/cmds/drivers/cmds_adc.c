#include <shell.h>
#include <driver/adc.h>
#include <string.h>
#include <fcntl.h>
#include <dfs_device.h>
#include <os.h>

static void cmd_func_cp_help(char *cmd)
{
    shell_printf("Usage: \t%s <channel> \n", cmd);
    shell_printf("\tadc sample unit: unsigned int, Doc\n");
    shell_printf("Example:\n");
    shell_printf("\t%s 0\n", cmd);
}

void cmd_func_adc(struct cmd_arg *arg, int argc, char **argv)
{
    int ret;
    int data;
    int channel;

    if (argc != 2) {
        goto adc_sample_err;
    }

    ret = sscanf(argv[1], "%d", &channel);
    if (ret != 1 || channel < 0) {
        goto adc_sample_err;
    }

    adc_init();
    data = adc_read_data(channel);
    if (data < 0) {
        shell_printf("adc read data failure\n");
        adc_deinit();
        return;
    }

    shell_printf("channel %d:data = %d\n", channel, data);
    adc_deinit();
    return;

adc_sample_err:
    cmd_func_cp_help(argv[0]);
}

void cmd_adc_init(void)
{
    shell_cmd_register(cmd_func_adc, "adc_sample", NULL, "adc sample");
}
