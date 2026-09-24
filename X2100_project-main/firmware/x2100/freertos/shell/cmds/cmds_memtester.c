#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <shell.h>
#include <dfs.h>
#include <common.h>

#define EXIT_FAIL_NONSTARTER        0x01
#define EXIT_FAIL_ADDRESSLINES      0x02
#define EXIT_FAIL_OTHERTEST         0x04
#define EXIT_FAIL_NOSUPPORTSIZE     0x08

struct memtester_arg {
    int argc;
    char **argv;
};

static struct memtester_arg m_arg;
static struct cmd_t *cmd;

extern struct cmd_t *shell_get_cmd(const char *const name);

int memtester_start(char *size, unsigned long loops);
void memtester_stop(void);

static void cmd_func_memtester_help(char *cmd)
{
    shell_printf("Usage:%s size [loops]\n", cmd);
    shell_printf("Example:\n");
    shell_printf("\t%s 4M 1000\n", cmd);
    shell_printf("\t%s 4m 1000\n", cmd);
    shell_printf("\t%s 128K\n", cmd);
    shell_printf("\t%s 128k\n", cmd);
}

void memtester_thread(void *data)
{
    int ret;
    unsigned int loops;

    struct memtester_arg *m_arg = (struct memtester_arg *)data;

    if (m_arg->argc == 2)
        loops = 0;
    else
        sscanf(m_arg->argv[2], "%d", &loops);

    ret = memtester_start(m_arg->argv[1], loops);
    if (ret < 0)
        goto memtester_err;

    thread_waiter_wakeup(&cmd->cond);

    return;

memtester_err:
    shell_printf("Wrong input parameters\n");
    cmd_func_memtester_help(m_arg->argv[0]);
}

void cmd_func_memtester(struct cmd_arg *arg, int argc, char **argv)
{
    cmd = shell_get_cmd(argv[0]);

    if (argc > 3 || argc < 2)
        goto memtester_err;

    m_arg.argc = argc;
    m_arg.argv = argv;

    thread_create("memtest thread", 8192, memtester_thread, &m_arg);

    shell_printf("\n");

    thread_waiter_wait(&cmd->cond);

    memtester_stop();

    return;

memtester_err:
    shell_printf("Wrong input parameters\n");
    cmd_func_memtester_help(argv[0]);
}


void cmd_memtester_init(void)
{
    shell_cmd_register(cmd_func_memtester, "memtester", NULL, "memory test");
}
