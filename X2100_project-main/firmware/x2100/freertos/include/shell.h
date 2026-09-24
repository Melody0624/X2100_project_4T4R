#ifndef _SHELL_H_
#define _SHELL_H_

#include <os.h>

#define SHELL_CMD_SIZE                  81
#define SHELL_CMD_NAME_SIZE             81
#define SHELL_CMD_STACK_SIZE            2048*2
#define SHELL_HISTORY_LINES             100
#define SHELL_IO_BUFFER_LEN             32

#define SHELL_PROMPT                    "$ "

struct cmd_arg;

typedef void(*cmd_func_t)(struct cmd_arg *, int argc, char **argv);

struct cmd_arg {
    int argc;
    char **argv;

    struct shell *shell;
    thread_ptr_t thread;
    cmd_func_t func;
};

struct cmd_t {
    struct list_head entry;
    char name[SHELL_CMD_NAME_SIZE];
    thread_waiter_t cond;
    cmd_func_t func;
    int argc;
    char **argv;
    char *help;
};

enum input_status {
    INPUT_STAT_NORMAL                   = 0,
    INPUT_STAT_DIRE_KEY_1,
    INPUT_STAT_DIRE_KEY_2,
    INPUT_STAT_TAB_KEY,
    INPUT_STAT_RUN_CMD,
};


struct shell {
    thread_ptr_t thread;
    enum input_status stat;

    uint16_t current_history;
    uint16_t history_count;
    char cmd_history[SHELL_HISTORY_LINES][SHELL_CMD_SIZE];

    struct cmd_arg *cur_cmd;

    char line[SHELL_CMD_SIZE];
    uint8_t line_position;
    uint8_t line_cur_pos;

    char buf[SHELL_IO_BUFFER_LEN];
    int count;

    int (*get_char)(void);
    int (*put_char)(char *buf, int size);
};

void shell_cmd_register(cmd_func_t cmd_fn, char *name, char *args, char *help);
int shell_printf(const char *__restrict fmt, ...);

#endif /* _SHELL_H_ */
