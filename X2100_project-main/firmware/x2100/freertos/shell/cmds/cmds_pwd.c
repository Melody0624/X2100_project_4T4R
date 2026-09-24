#include <string.h>
#include <shell.h>

extern struct cmd_t *shell_get_cmd(const char *const name);

#ifdef DFS_USING_WORKDIR
extern char working_directory[];
#endif

static void cmd_func_pwd_help(char *cmd)
{
    shell_printf("Usage: %s \n", cmd);
    shell_printf("\tprint name of current/working directory\n");
    shell_printf("\tThis command has no OPTION\n");
    shell_printf("Example:\n");
    shell_printf("\t%s  \n", cmd);
}

void cmd_func_pwd(struct cmd_arg *arg, int argc, char **argv)
{
    if (argc != 1) {
        cmd_func_pwd_help(argv[0]);
        goto cmd_out;
    }
#ifdef DFS_USING_WORKDIR
    shell_printf(working_directory);
#else
    shell_printf("/");
#endif
    shell_printf("\n");

cmd_out:
    shell_printf("");
}


