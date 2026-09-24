#include <shell.h>

extern void shell_get_all_cmds_help(struct shell *shell);
extern struct cmd_t *shell_get_cmd(const char *const name);


void cmd_func_help(struct cmd_arg *arg, int argc, char **argv)
{
    struct cmd_t *cmd = NULL;

    if (argc == 2) {
        cmd = shell_get_cmd(argv[1]);
        if (cmd) {
            shell_printf("%s:\t\t%s\n", cmd->name, cmd->help);
        } else {
            shell_printf("%s: No match command with \"%s\"\n", argv[0], argv[1]);
        }
    } else if (arg->argc != 2) {
        shell_printf("Usage: help [command]\n");

        shell_printf("%-24s %s\n", "Command", "Help");
        shell_get_all_cmds_help(arg->shell);
    }

    return;
}


