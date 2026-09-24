#include <stdarg.h>
#include <common.h>
#include <malloc.h>
#include <shell.h>
#include <assert.h>
#include <driver/console.h>

/*
 * Ctrl+A~Z 组合按键值为: 1 ~ 26
 */
#define _CTRL_C                         (3)
#define _CTRL_Z                         (26)

extern void shell_cmd_prepare_exit(struct cmd_arg *arg);
extern int shell_strspilt_alloc(char ** (*argv), char *input_buf, int mode);
extern thread_ptr_t shell_cmd_run(char *name, struct cmd_arg *arg, os_priority_id priority);
extern void shell_strspilt_free(int argc, char ** (*argv));
extern void shell_auto_complete(struct shell *shell);
unsigned xvformat(void (*outchar)(void *arg,char),void *arg,const char * fmt,va_list args);

int shell_printf(const char *__restrict fmt, ...);

static void shell_handle_history(struct shell *shell);
static void shell_push_history(struct shell *shell);
static int shell_handle_enter_key(struct shell * shell);
static void shell_handle_normal_key(struct shell *shell, char ch);
static void shell_handle_backspace_key(struct shell *shell);
static void shell_handle_tab_key(struct shell *shell);

void shell_thread_func(void *data)
{
    char ch;
    char current_path[32];
    struct shell shell;
    int ret = 0;

    memset(current_path, 0x00, sizeof(current_path));
    strcpy(current_path, "/");

    memset(&shell, 0x00, sizeof(struct shell));
    shell.thread = thread_get_current();
    shell.line_position = 0;
    shell.line_cur_pos = 0;
    shell.stat = INPUT_STAT_NORMAL;

    while(1) {
        ch = console_get_char();

        /*
         * combination key
         */
        if (ch == _CTRL_C) {            /* handle CR-C key */
            shell_printf("^C");

            shell.line_position = 0;
            shell.line_cur_pos = 0;
            memset(shell.line, 0x00, SHELL_CMD_SIZE);
            if (shell.stat == INPUT_STAT_RUN_CMD) {
                shell_printf("\n");
                shell_cmd_prepare_exit(shell.cur_cmd);
            } else {
                shell_printf("\n" SHELL_PROMPT);
            }
            continue;

        } else if (ch == _CTRL_Z) {     /* handle CR-Z key */
            shell_printf("^Z");
            shell_printf("\n" SHELL_PROMPT);

            shell.stat = INPUT_STAT_NORMAL;
            shell.cur_cmd = NULL;
            continue;

        } else if (shell.stat == INPUT_STAT_RUN_CMD) {
            /* send char to command thread */
            /* TODO */
            continue;
        }

        /*
         * handle control key
         * Up    : 0x1B 0x5B 0x41
         * Down  : 0x1B 0x5B 0x42
         * Right : 0x1B 0x5B 0x43
         * Left  : 0x1B 0x5B 0x44
         */
        if (ch == 0x1B) {
            shell.stat = INPUT_STAT_DIRE_KEY_1;
            continue;

        } else if (shell.stat == INPUT_STAT_DIRE_KEY_1) {
            if (ch == 0x5B)  {
                shell.stat = INPUT_STAT_DIRE_KEY_2;
                continue;
            }
            shell.stat = INPUT_STAT_NORMAL;
            continue;

        } else if (shell.stat == INPUT_STAT_DIRE_KEY_2) {
            shell.stat = INPUT_STAT_NORMAL;
            if (ch == 0x41) {           /* Up Key */
                /* prev history */
                if (shell.current_history <= 0)
                    continue;

                shell.current_history --;
                shell_handle_history(&shell);
                continue;
            } else if (ch == 0x42) {    /* Down Key */
                /* next history */
                if (shell.current_history >= shell.history_count - 1)
                    continue;

                shell.current_history ++;
                shell_handle_history(&shell);
                continue;
            } else if (ch == 0x44) {    /* Left Key */
                if (shell.line_cur_pos) {
                    shell_printf("\b");
                    shell.line_cur_pos--;
                }
                continue;
            } else if (ch == 0x43) {    /* Right Key */
                if (shell.line_cur_pos < shell.line_position) {
                    shell_printf("%c", shell.line[shell.line_cur_pos]);
                    shell.line_cur_pos++;
                }
                continue;
            }

        } else if (ch == '\t') {        /* handle TAB key */
            shell_handle_tab_key(&shell);
            continue;
        }

        /*
         * backspace
         * enter
         */
        shell.stat = INPUT_STAT_NORMAL;
        if (ch == 0x7F || ch == 0x08) { /* handle backspace key */
            shell_handle_backspace_key(&shell);
            continue;
        } else if (ch == '\n' || ch == '\r') {
                                        /* handle end of line, break */
            shell_push_history(&shell);
            ret = shell_handle_enter_key(&shell);
            if (ret < 0) {
                shell_printf("\n");
                shell_printf(SHELL_PROMPT);
            }

        } else {                        /* normal character */
            shell_handle_normal_key(&shell, ch);
        }
    } /* end of while(1) */

}

static int shell_handle_enter_key(struct shell *shell)
{
    thread_ptr_t thread = NULL;
    struct cmd_arg *arg;

    assert(shell->cur_cmd == NULL);

    if (shell->line_position == 0) {
        return -1;
    }

    arg = malloc(sizeof(struct cmd_arg));
    assert(arg);

    shell->cur_cmd = arg;
    arg->argc = shell_strspilt_alloc(&arg->argv, shell->line, INPUT_STAT_NORMAL);
    if (arg->argc > 0) {
        arg->shell = shell;
        shell->stat = INPUT_STAT_RUN_CMD;
#ifdef CONFIG_FREERTOS
        prvCheckTasksWaitingTermination();
#endif
        thread = shell_cmd_run(arg->argv[0], arg, OS_priority_realtime);
        if (thread == NULL) {
            shell_strspilt_free(arg->argc, &arg->argv);
            free(arg);
            shell->cur_cmd = NULL;
            shell->stat = INPUT_STAT_NORMAL;
        }
    } else {
        /* invalid typing */
        free(arg);
        shell->cur_cmd = NULL;
        shell->stat = INPUT_STAT_NORMAL;
    }
    memset(shell->line, 0, sizeof(shell->line));
    shell->line_cur_pos = 0;
    shell->line_position = 0;
    //shell->history_continue = 0;

    return (thread == NULL) ? -1 : 0;
}

static void shell_handle_history(struct shell *shell)
{
    int i;

#if 0
    if (!shell->history_continue) {
        shell->history_continue = 1;
    }
#endif

    /* clear command line timestamp */
    shell_printf("\r");
    for (i=0; i<SHELL_CMD_SIZE; i++) {
        shell_printf(" ");
    }
    shell_printf("\r");

    shell_printf(SHELL_PROMPT);
    /* copy the history command */
    memcpy(shell->line, &shell->cmd_history[shell->current_history][0], SHELL_CMD_SIZE);
    shell->line_cur_pos = shell->line_position = strlen(shell->line);
    shell_printf("%s", shell->line);
}

static void shell_push_history(struct shell *shell)
{
    if (shell->line_position != 0) {
        if (!strcmp(shell->line, shell->cmd_history[shell->history_count-1])) {
            shell->current_history = shell->history_count;
            return;
        }

        /* push history */
        if (shell->history_count  >= SHELL_HISTORY_LINES) {
            /* move history */
            int index;
            for (index=0; index<SHELL_HISTORY_LINES-1; index++) {
                memcpy(&shell->cmd_history[index][0], &shell->cmd_history[index+1][0], SHELL_CMD_SIZE);
            }
            memset(&shell->cmd_history[index][0], 0, SHELL_CMD_SIZE);
            memcpy(&shell->cmd_history[index][0], shell->line, shell->line_position);

            /* it's the maximum history */
            shell->history_count = SHELL_HISTORY_LINES;
        } else {
            memset(&shell->cmd_history[shell->history_count][0], 0x00, SHELL_CMD_SIZE);
            memcpy(&shell->cmd_history[shell->history_count][0], shell->line, shell->line_position);

            /* increase count and set current history position */
            shell->history_count++;
        }

    }

    shell->current_history = shell->history_count;
}

static void shell_handle_normal_key(struct shell *shell, char ch)
{
    int i;

    if (shell->line_cur_pos >= SHELL_CMD_SIZE - 1) {
        return ;
    }

    if (shell->line_cur_pos < shell->line_position) {
        /* relocation and save typing character */
        if (shell->line_position < SHELL_CMD_SIZE - 1) {
            /* shell line is not full */
            memmove(&shell->line[shell->line_cur_pos + 1],
                    &shell->line[shell->line_cur_pos],
                    shell->line_position - shell->line_cur_pos);
            shell->line[shell->line_cur_pos] = ch;
            shell_printf("%s", &shell->line[shell->line_cur_pos]);

            /* move the cursor to new position */
            for (i = shell->line_cur_pos; i < shell->line_position; i++) {
                /* \b 退格 */
                shell_printf("\b");
            }

        } else {
            /* shell line is full */
            memmove(&shell->line[shell->line_cur_pos + 1],
                    &shell->line[shell->line_cur_pos],
                    shell->line_position - shell->line_cur_pos - 1);
            shell->line[shell->line_cur_pos] = ch;
            shell_printf("%s", &shell->line[shell->line_cur_pos]);

            /* move the cursor to new position */
            for (i = shell->line_cur_pos; i < shell->line_position - 1; i++) {
                /* \b 退格 */
                shell_printf("\b");
            }
        } /* end of if(shell->line_position ...) else ... */

    } else {
        /* save typing character */
        shell->line[shell->line_position] = ch;
        shell_printf("%c", ch);
    }

    if (shell->line_position < SHELL_CMD_SIZE - 1) {
        shell->line_position++;
    }

    if (shell->line_cur_pos < SHELL_CMD_SIZE - 1) {
        shell->line_cur_pos++;
    }
}

static void shell_handle_backspace_key(struct shell *shell)
{
    int i;

    if (shell->line_cur_pos == 0)
        return;

    shell->line_position--;
    shell->line_cur_pos--;

    if (shell->line_position > shell->line_cur_pos) {
        memmove(&shell->line[shell->line_cur_pos],
                &shell->line[shell->line_cur_pos + 1],
                shell->line_position - shell->line_cur_pos);
        shell->line[shell->line_position] = 0;

        shell_printf("\b%s  \b", &shell->line[shell->line_cur_pos]);

        /* move the cursor to the origin position */
        for (i = shell->line_cur_pos; i <= shell->line_position; i++) {
            shell_printf("\b");
        }

    } else {
        shell_printf("\b \b");
        shell->line[shell->line_position] = 0;
    }

}

static void shell_handle_tab_key(struct shell *shell)
{
    /* move the cursor to the beginning of line */
    shell_printf("\r");
    shell_auto_complete(shell);

    shell_printf(SHELL_PROMPT);

    shell_printf("%s", shell->line);
    shell->stat = INPUT_STAT_TAB_KEY;

    /* move the cursor to new position */
    int i = 0;
    for (i = shell->line_cur_pos; i < shell->line_position; i++) {
        /* \b 退格 */
        shell_printf("\b");
    }

}


static void shell_printf_putchar(void *arg, char c)
{
    (void) arg;

    console_put_char(c);
}

int shell_printf(const char *__restrict fmt, ...)
{
    va_list list;
    unsigned int count;

    va_start(list, fmt);
    count = xvformat(shell_printf_putchar, 0, fmt, list);
    va_end(list);

    return count;
}
