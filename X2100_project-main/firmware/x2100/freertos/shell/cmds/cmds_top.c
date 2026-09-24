#include <string.h>
#include <shell.h>
#include <common.h>

extern struct cmd_t *shell_get_cmd(const char *const name);

extern int get_thread_runtime_stats_alloc(void **buffer);

extern char *get_thread_interval_runtime_stats_alloc(int interval_s,
        void *snapshot1, int snapshot1_num,
        void *snapshot2, int snapshot2_num);

extern void free_thread_runtime_stats(void *task_array, int array_num);

#define TAG_ATTR_CLOSE                  "\033[0m"   /* 关闭所有属性 */
#define TAG_CLEAR_SCREEN                "\033[2J\033[0;0H"   /* 清屏，设置光标坐标[0,0] */
#define TAG_SAVE_CURSOR                 "\033[s"    /* 保存光标位置 */
#define TAG_RESTORE_CURSOR              "\033[u"    /* 恢复光标位置 */
#define TAG_CURSOR_TOP                  "\033[80A"  /* 光标上移动80行 */
#define TAG_CURSOR_BUTTOM               "\033[80B"  /* 光标下移动80行 */
#define TAG_CURSOR_LEFT                 "\033[80C"  /* 光标左移动80行 */
#define TAG_CURSOR_ANTI_DIS             "\033[7m"   /* 反显 */
#define TAG_CURSOR_HIDE                 "\033[?25l" /* 隐藏光标 */
#define TAG_CURSOR_SHOW                 "\33[?25h"  /* 显示光标 */

enum {
    UPDATE_MODE_INTERVAL,
    UPDATE_MODE_TOTAL,
};

static void cmd_func_top_help(char *cmd)
{
    shell_printf("Usage: %s [-d -t]\n", cmd);
    shell_printf("\tdisplay system summary information as well as a list of processes or threads currently\n");
    shell_printf("\tbeing managed by the OS\n");
    shell_printf("OPTION:\n");
    shell_printf("\t-d, [Display] update-time interval, unit seconds. Default 5s\n");
    shell_printf("\t-t, [Totle]  total run time(utilization percentage) of the task so far since the Device is work\n");
    shell_printf("\t-n, [Number] specifies the maximum number of iterations. Default 1000\n");
    shell_printf("Example:\n");
    shell_printf("\t%s\n", cmd);
    shell_printf("\t%s -d 5\n", cmd);
    shell_printf("\t%s -t -d 5 -n 10\n", cmd);
}


/*
 * NOTE:
 * 命令的实现过程会挂起所有任务，因此建议该命令仅在调试阶段使用
 */
void cmd_func_top(struct cmd_arg *arg, int argc, char **argv)
{
    struct cmd_t *cmd = shell_get_cmd(argv[0]);
    void *thread_snapshot1 = NULL;
    void *thread_snapshot2 = NULL;
    int thread_snapshot1_num = 0;
    int thread_snapshot2_num = 0;
    char *buffer = NULL;
    int update_mode = UPDATE_MODE_INTERVAL;
    int interval = 5;   /* update-time interval default */
    int max_count = 1000;

    int ret = -1;
    int i = 0;

    /* helpful info */
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        cmd_func_top_help(argv[0]);
        goto cmd_out;
    }

    /* check argument */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0) {
            update_mode = UPDATE_MODE_TOTAL;

        } else if (strcmp(argv[i], "-d") == 0) {
            if (i+1 >= argc) {
                shell_printf("%s: '%s' requires argument\n", argv[0], argv[i]);
                goto cmd_out;
            }

            interval = atoi(argv[i+1]);
            i++;
            if (!interval) {
                interval = 5;
            }

        } else if (strcmp(argv[i], "-n") == 0) {
            if (i+1 >= argc) {
                shell_printf("%s: '%s' requires argument\n", argv[0], argv[i]);
                goto cmd_out;
            }

            max_count = atoi(argv[i+1]);
            i++;
            if (!max_count) {
                max_count = 1000;
            }

        } else {
            /* can not handle option */
            shell_printf("%s: unknown option '%s'\n", argv[0], argv[i]);
            goto cmd_out;
        }
    }


    /* command */
    while (max_count--) {
        if (update_mode == UPDATE_MODE_INTERVAL) {
            if (thread_snapshot1 == NULL) {
                /* 显示各thread从系统启动开始记时的占用率 */
                /* 第一次记录各thread状态信息 */
                thread_snapshot1_num = get_thread_runtime_stats_alloc(&thread_snapshot1);
                goto display_snapshot;
            }

            if (thread_snapshot2 == NULL) {
                /* 第二次记录各thread状态信息 */
                thread_snapshot2_num = get_thread_runtime_stats_alloc(&thread_snapshot2);
                goto display_snapshot;
            }

            /*
             * 多次记录各thread状态信息
             */
            /* 将上次状态信息更新至thread_snapshot1中 */
            #if CONFIG_OS_ITOS
                free_thread_runtime_stats(thread_snapshot1, thread_snapshot1_num);
            #else
                free(thread_snapshot1);
            #endif
            thread_snapshot1_num = thread_snapshot2_num;
            thread_snapshot1 = thread_snapshot2;
            thread_snapshot2_num = get_thread_runtime_stats_alloc(&thread_snapshot2);

        } else if (update_mode == UPDATE_MODE_TOTAL) {
            if (thread_snapshot1 != NULL) {
                #if CONFIG_OS_ITOS
                    free_thread_runtime_stats(thread_snapshot1, thread_snapshot1_num);
                #else
                    free(thread_snapshot1);
                #endif
                thread_snapshot1 = NULL;
            }

            /* 显示各thread从系统启动开始记时的占用率 */
            thread_snapshot1_num = get_thread_runtime_stats_alloc(&thread_snapshot1);
        }

display_snapshot:
        buffer = get_thread_interval_runtime_stats_alloc(interval,
                    thread_snapshot1, thread_snapshot1_num,
                    thread_snapshot2, thread_snapshot2_num);
        if (buffer) {
            shell_printf(TAG_CLEAR_SCREEN);

            #if CONFIG_OS_ITOS
            shell_printf("ThreadID State\tPriority %%CPU\tCpuID\tTime\t\tName\n");
            #else
            shell_printf("ThreadID State\tPriority %%CPU\tTime\t\tName\n");
            #endif

            shell_printf("%s", buffer);
            free(buffer);
            buffer = NULL;

            shell_printf("Key to State:\n");
            shell_printf("  X(running)  R(ready) B(blocked) D(deleted) S(suspend)\n");
            shell_printf("  Stack: Stack Size unit Bytes\n");

            shell_printf(TAG_CURSOR_BUTTOM);
            shell_printf(TAG_CURSOR_ANTI_DIS);
            shell_printf("CTRL+C for Quit                                            ");
            shell_printf(TAG_ATTR_CLOSE);
            //shell_printf(TAG_CURSOR_HIDE);
        }

        ret = thread_waiter_wait_timeout(&cmd->cond, interval * 1000);
        if (ret == 0) {
            /* thread cond wakeup by CTRL+C,  */
            goto cmd_out;
        }
    } /* end of while(max_count--) */

    shell_printf("\n");

cmd_out:
    if (thread_snapshot1) {
        #if CONFIG_OS_ITOS
            free_thread_runtime_stats(thread_snapshot1, thread_snapshot1_num);
        #else
            free(thread_snapshot1);
        #endif
    }

    if (thread_snapshot2) {
        #if CONFIG_OS_ITOS
            free_thread_runtime_stats(thread_snapshot2, thread_snapshot2_num);
        #else
            free(thread_snapshot2);
        #endif
    }
}
