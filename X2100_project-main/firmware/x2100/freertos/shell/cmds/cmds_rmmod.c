#include <string.h>
#include <fcntl.h>
#include <shell.h>
#include <dfs_posix.h>

#ifdef CONFIG_OS_MODULE

extern int delete_module(const char *name);

static void cmd_func_rmmod_help(char *cmd)
{
    shell_printf("Usage: %s <module_name>\n", cmd);
    shell_printf("\tSimple program to remove a module from the Kernel\n");
    shell_printf("Example:\n");
    shell_printf("\t%s module_example\n", cmd);
}

void cmd_func_rmmod(struct cmd_arg *arg, int argc, char **argv)
{
    int ret = 0;

    /* helpful info */
    if (argc != 2) {
        cmd_func_rmmod_help(argv[0]);
        goto cmd_out;
    }

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        cmd_func_rmmod_help(argv[0]);
        goto cmd_out;
    }

    /* command */
    ret = delete_module(argv[1]);
    if (ret < 0) {
        switch (ret) {
        case -ENOENT:
            shell_printf("%s can't unload '%s': unknown symbol in module\n", argv[0], argv[1]);
            break;
        case -EBUSY:
            shell_printf("%s can't unload '%s': Device or resource busy\n", argv[0], argv[1]);
            break;
        default:
            shell_printf("%s can't unload '%s': Unknow error:%d\n", argv[0], argv[1], ret);
            break;
        }

    }

cmd_out:
    return;
}

#endif /* end of CONFIG_OS_MODULE */
