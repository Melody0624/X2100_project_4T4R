#include <shell.h>
#include <common.h>
#include "../../os/freertos/include/FreeRTOS.h"
#include "../../os/freertos/include/task.h"

StackType_t *FindThreadByNumber(UBaseType_t TaskNumber, char **TaskName);
TaskStatus2_t * FindThreadByName(char * pcTaskName, int *thread_count);
void PortShowStackTrace(StackType_t *pxTopOfStack);


static void cmd_func_thread_dump_help(char *cmd)
{
    shell_printf("Usage: %s <thread_name/thread_id>\n", cmd);
    shell_printf("\tPrint the calling process of a running thread\n");
    shell_printf("\tIf you want to get the thread id you can use thread_list cmd\n");
    shell_printf("Example:\n");
    shell_printf("\t%s shell\n", cmd);
    shell_printf("\t%s 3\n", cmd);
}

void cmd_func_thread_dump(struct cmd_arg *arg, int argc, char **argv)
{
    UBaseType_t TaskNumber;
    StackType_t *pxTopOfStack = NULL;
    TaskStatus2_t *thread_array;

    char *TaskName;
    int thread_count, i;

    if (argc != 2)
        goto thread_dump_err;

    /* 通过线程名查找线程
     */
    /* 得到线程的栈顶指针和线程id */
    thread_array = FindThreadByName(argv[1], &thread_count);

    /* 打印出该线程当前的调用流程 */
    for (i = 0; i < thread_count; i++) {
        shell_printf("\n\tthread_id = %ld thread_name = %s\n", thread_array[i].xTaskNumber, argv[1]);
        PortShowStackTrace(thread_array[i].pxTopOfStack);
    }

    if (thread_count != 0)
        return;

    /* 通过线程id查找线程
     */
    TaskNumber = atoi(argv[1]);
    if(TaskNumber == 0)
        goto thread_dump_err;

    /* 得到线程的栈顶指针和线程名 */
    pxTopOfStack = FindThreadByNumber(TaskNumber, &TaskName);
    if (pxTopOfStack == NULL) {
        shell_printf("not find this thread\n");
        return;
    }

    /* 打印出该线程当前的调用流程 */
    shell_printf("thread_id = %ld thread_name = %s\n", TaskNumber, TaskName);
    PortShowStackTrace(pxTopOfStack);
    free(thread_array);
    return;

thread_dump_err:
    cmd_func_thread_dump_help(argv[0]);
}