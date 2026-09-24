#include <shell.h>
#include <wake_lock.h>
#include <printf.h>
#include <dump_mem.h>

static void cmd_wake_locks_show(struct cmd_arg *cmd_arg, int argc, char **argv)
{
    wake_locks_show();
}

void cmd_wake_lock_init(void)
{
    shell_cmd_register(cmd_wake_locks_show, "wake_locks_show", NULL, NULL);
}