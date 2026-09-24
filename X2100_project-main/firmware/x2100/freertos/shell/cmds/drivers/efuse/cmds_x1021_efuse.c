#include <shell.h>
#include <driver/efuse.h>
#include <stdlib.h>
#include <config.h>
#include <string.h>

#define X1021_EFUSE_SEG_CNT 8

static const char *seg_name[] = {
    "CHIP_ID",
    "USER_ID",
    "SARADC_CAL_DAT",
    "TRIM_DATA",
    "PROGRAM_PROTECT",
    "CPU_ID",
    "SPECIAL_USE",
    "CUSTOMER_RESV",
};

static unsigned int seg_size[] = {
    [CHIP_ID] = CHIP_ID_SIZE,
    [USER_ID] = USER_ID_SIZE,
    [SARADC_CAL_DAT] = SARADC_CAL_DAT_SIZE,
    [TRIM_DATA] = TRIM_DATA_SIZE,
    [PROGRAM_PROTECT] = PROGRAM_PROTECT_SIZE,
    [CPU_ID] = CPU_ID_SIZE,
    [SPECIAL_USE] = SPECIAL_USE_SIZE,
    [CUSTOMER_RESV] = CUSTOMER_RESV_SIZE,
};

static void cmd_func_efuse_help(char *cmd)
{
    int i;
    shell_printf("Usage: %s <section_id>\n", cmd);
    shell_printf("Read data from the specified segment of efuse\n");

    shell_printf("\tsection_id:\t\tsection_size:\n");
    for(i = 0; i < X1021_EFUSE_SEG_CNT; i++)
    {
        shell_printf("\t%-20s\t%d\n", seg_name[i], seg_size[i]);
    }

    shell_printf("Example:\n");
    shell_printf("\t%s CHIP_ID\n", cmd);

}
void cmd_func_efuse(struct cmd_arg *arg, int argc, char **argv)
{
    int i, ret;
    int size;
    int section_id = -1;
    unsigned char *resv;

    if (argc != 2)
        goto efuse_read_err;

    /*Parsing parameters */
    for (i = 0; i < X1021_EFUSE_SEG_CNT; i++) {
        if (strcmp(argv[1], seg_name[i]) == 0) {
            section_id = i;
        }
    }
    if (section_id == -1)
        goto efuse_read_err;

    size = seg_size[section_id];

    /*malloc receive buff*/
    resv = (unsigned char *)malloc(size);
    memset(resv, 0, size);

    if (resv == NULL) {
        shell_printf("malloc receive buff failure\n");
        goto efuse_read_err;
    }

    /*read data from the efuse*/
    ret = efuse_read_segment(section_id, resv, size);
    if (ret == 0) {
        for (i = 0; i < size; i++) {
            shell_printf("0x%02X ", resv[i]);
            if (((i + 1) % 10) == 0)
                shell_printf("\n");
        }
        shell_printf("\n");
    } else {
        shell_printf("efuse read segment failure\n");
    }

    free(resv);
    return;

efuse_read_err:
    cmd_func_efuse_help(argv[0]);
}

void cmd_efuse_init(void)
{
    shell_cmd_register(cmd_func_efuse,    "efuse_read",   NULL,    "read efuse segment data");
}