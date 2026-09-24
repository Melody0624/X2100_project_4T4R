#include <stdlib.h>
#include <shell.h>

extern void shell_get_cmds_history(struct shell *shell, int numbers);


void cmd_func_history(struct cmd_arg *arg, int argc, char **argv)
{
    int number = -1;

    switch (argc) {
    case 1:
        shell_get_cmds_history(arg->shell, -1);
        break;
    case 2:
        number = atoi(argv[1]);
        if (number >= 0) {
            shell_get_cmds_history(arg->shell, number);
        } else {
            shell_printf("%s: %s :numeric argument required\n", argv[0], argv[1]);
        }
        break;
    default:
        shell_printf("%s: too many arguments\n",argv[0]);
        break;
    }

    return;
}


