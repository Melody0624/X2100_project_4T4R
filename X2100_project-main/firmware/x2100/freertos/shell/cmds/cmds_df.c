#include <string.h>
#include <dfs_device.h>
#include <shell.h>

extern int df(const char *path);

static void cmd_func_df_help(char *cmd)
{
    shell_printf("Usage: %s [OPTION] ...[PATH]\n", cmd);
    shell_printf("\treport file system disk space usage\n");
    shell_printf("Example:\n");
    shell_printf("\t%s\n", cmd);
}

void cmd_func_df(struct cmd_arg *arg, int argc, char **argv)
{
    /* helpful info */
    if (argc > 2) {
        cmd_func_df_help(argv[0]);
        goto cmd_out;
    }

    if (argc == 2) {
        if (strcmp(argv[1], "--help") == 0) {
            cmd_func_df_help(argv[0]);
            goto cmd_out;
        } else {
            /* command */
            df(argv[1]);
        }
    }

    /* command */
    if (argc == 1) {
        df("/");
    }

cmd_out:
    return;
}


