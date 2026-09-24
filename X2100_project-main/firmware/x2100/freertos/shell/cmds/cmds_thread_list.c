#include <string.h>
#include <shell.h>
#include <common.h>

extern char *get_thread_list_alloc(void);

/*
 * NOTE:
 * 命令的实现过程会挂起所有任务，因此建议该命令仅在调试阶段使用
 */
void cmd_func_thread_list(struct cmd_arg *arg, int argc, char **argv)
{
    char *buf = NULL;

#if CONFIG_OS_ITOS
    shell_printf("ThreadID State\tPriority Stack\tCpuID\tTime\t\tName\n");
#else
    shell_printf("ThreadID State\tPriority Stack\tTime\t\tName\n");
#endif

    buf = get_thread_list_alloc();
    if (buf) {
        shell_printf("%s", buf);
        free(buf);
    }
    shell_printf("Key to State:\n");
    shell_printf("  X(running)  R(ready) B(blocked) D(deleted) S(suspend)\n");
    shell_printf("  Stack: Stack Size unit Bytes\n");
}


