#include <string.h>
#include <fcntl.h>
#include <shell.h>
#include <dfs_posix.h>

#ifdef CONFIG_OS_MODULE
#define MODULE_DIRECTORY                "/sys/module"

extern void list_show_module(int (*shell_printf)(const char *__restrict fmt, ...));

static void cmd_func_lsmod_help(char *cmd)
{
    shell_printf("Usage: %s \n", cmd);
    shell_printf("\tShow the status of modules in the Kernel\n");
}

void cmd_func_lsmod(struct cmd_arg *arg, int argc, char **argv)
{
    /* helpful info */
    if (argc != 1) {
        cmd_func_lsmod_help(argv[0]);
        goto cmd_out;
    }

    list_show_module(shell_printf);


cmd_out:
    return;
}

#endif /* end of CONFIG_OS_MODULE */
