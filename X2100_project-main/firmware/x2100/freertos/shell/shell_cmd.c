#include <common.h>
#include <malloc.h>
#include <shell.h>
#include <assert.h>
#include <dfs_posix.h>

int shell_strspilt_alloc(char ** (*argv), char *input_buf, int mode);
void shell_cmd_exit(struct cmd_arg *arg);
void shell_strspilt_free(int argc, char ** (*argv));
extern int shell_printf(const char *__restrict fmt, ...);
extern char working_directory[];

#define SHELL_MATCH_ECHO_MASK           (1 << 0)
#define SHELL_MATCH_COPY_MASK           (1 << 1)

struct argv_list {
    char *cmd;
    int index;
    struct argv_list *head;
    struct argv_list *next;
};

static struct list_head cmd_list = LIST_HEAD_INIT(cmd_list);

struct cmd_t *shell_get_cmd(const char *const name)
{
    struct cmd_t *cmd = NULL;

    list_for_each_entry(cmd, &cmd_list, entry) {
        if (!strcmp(cmd->name, name)) {
            return cmd;
        }
    }

    return NULL;
}

static void cmd_list_add(struct cmd_t *cmd)
{
    list_add(&cmd->entry, &cmd_list);
}

void shell_get_all_cmds_help(struct shell *shell)
{
    struct cmd_t *cmd;

    list_for_each_entry(cmd, &cmd_list, entry) {
        shell_printf("%-24s %s\n", cmd->name, cmd->help);
    }
}

void shell_get_cmds_history(struct shell *shell, int numbers)
{
    int count = shell->history_count;
    int index = 0;

    if (numbers < 0 || numbers > SHELL_HISTORY_LINES) {
        count = shell->history_count;
        index = 0;
    } else if (numbers < count) {
        count = shell->history_count;
        index = count - numbers;

    } else {
        count = shell->history_count;
        index = 0;
    }

    for (; index <count; index++) {
        shell_printf("%03d  %s\n", index, &shell->cmd_history[index][0]);
    }

}

static void shell_printf_all_cmd_name(struct shell *shell)
{
    struct cmd_t *cmd;

    shell_printf("\n");
    list_for_each_entry(cmd, &cmd_list, entry) {
        shell_printf("%s\t", cmd->name);
    }
    shell_printf("\n");
}

__attribute__((__unused__)) static void shell_printf_all_args_name(struct shell *shell, struct cmd_t *cmd)
{
    int i;

    shell_printf("\n");

    shell_printf("%s\t", cmd->name);
    for(i = 0; i < cmd->argc; i++) {
        shell_printf("%s\t", cmd->argv[i]);
    }

    shell_printf("\n");
}


static struct cmd_t *shell_match_cmd_name(struct shell *shell, char *name, int flags)
{
    int i, cnt = 0;
    int echo = flags & SHELL_MATCH_ECHO_MASK;
    int copy = flags & SHELL_MATCH_COPY_MASK;
    int min_len = SHELL_CMD_NAME_SIZE + 1;
    int len = strlen(name);
    struct cmd_t *cmd;
    struct cmd_t *m_cmd = NULL;

    if (echo)
        shell_printf("\n");

    list_for_each_entry(cmd, &cmd_list, entry) {

        if (strncmp(name, cmd->name, len) == 0) {
            cnt++;

            if (cnt == 1) {
                m_cmd = cmd;
                min_len = strlen(cmd->name);
            } else {
                for (i = len-1; i < min_len; i++) {
                    if (cmd->name[i] != m_cmd->name[i]) {
                        min_len = i;
                        break;
                    }
                }
            }


            if (echo) {
                shell_printf("%s\t", cmd->name);
            }

        }
    } /* end of list_for_each_entry */

    if (m_cmd) {
        if (copy) {
            strncpy(name, m_cmd->name, min_len);
        } else if (strlen(name) < strlen(m_cmd->name)) {
            return NULL;
        }
    } else {
        return NULL;
    }

    if (cnt == 1) {
        len = strlen(name);
        if (len < SHELL_CMD_NAME_SIZE-1)
            name[len] = ' ';
    }

    if (echo)
        shell_printf("\n");


    return m_cmd;
}

__attribute__((__unused__)) static void shell_match_cmd_args(struct shell *shell, struct cmd_t *cmd, char *str, int echo)
{
    int i, j, cnt = 0;
    int len = strlen(str);
    int min_len = SHELL_CMD_NAME_SIZE + 1;
    char *m_str = NULL;

    if (echo)
        shell_printf("\n");

    for(i = 0; i < cmd->argc; i++) {

        if (strncmp(str, cmd->argv[i], len) == 0) {

            cnt++;
            if (min_len > strlen(cmd->argv[i])) {
                m_str = cmd->argv[i];
                min_len = strlen(cmd->argv[i]);

            } else if (min_len == strlen(cmd->argv[i])) {

                for (j = len-1; j < min_len; j++) {
                    if (cmd->argv[i][j] != m_str[j]) {
                        min_len = j;
                        break;
                    }
                }

            }

            if (echo)
                shell_printf("%s\t", cmd->argv[i]);
        }
    } /* end of for(... */

    if (m_str) {
        strncpy(str, m_str, min_len);
    }

    if (cnt == 1) {
        len = strlen(str);
        str[len] = ' ';
    }

    if (echo)
        shell_printf("\n");
}

static int str_common(const char *str1, const char *str2)
{
    const char *str = str1;

    while ((*str != 0) && (*str2 != 0) && (*str == *str2)) {
        str ++;
        str2 ++;
    }

    return (str - str1);
}

static unsigned int get_ch_pos_in_str(const char * str, char ch)
{
    char *p = (char *)str;
    unsigned char str_len = strlen(str);
    while ( p != (str + str_len))
    {
        if ( *p == ch) {
            return (p - str);
        } else {
            p++;
        }
    }
    return -1;
}

static void shell_printf_all_file_name(DIR *dir)
{
    struct dirent *dirent = NULL;
    for (;;) {
            dirent = readdir(dir);
            if (dirent == NULL)
                break;

            if (get_ch_pos_in_str(dirent->d_name, ' ') != -1) {
                shell_printf("'%s'\n",dirent->d_name);
            }
            else  {
                shell_printf("%s\n", dirent->d_name);
            }
        }
}

static void shell_path_auto_complete(char *** argv, int argc, struct shell *shell)
{
    DIR *dir = NULL;
    struct dirent *dirent = NULL;
    char *full_path, *ptr, *index;
    char *abs_full_path;
    char *shell_index = NULL;

    full_path = (char *)malloc(256);
    if (full_path == NULL)
        return; /* out of memory */
    memset(full_path, 0, 256);

    abs_full_path = (char *)malloc(256);
    if (abs_full_path == NULL) {
        free(full_path);
        return ;
    }
    memset(abs_full_path, 0, 256);

    if ((*argv)[argc-1][0] != '/') {
        getcwd(full_path, 256);
        if (full_path[strlen(full_path) - 1]  != '/') {
            strcat(full_path, "/");
        }
    }

    index = NULL;

    ptr = (*argv)[argc-1];
    for (;;) {
        if (*ptr == '/') {
            index = ptr + 1;
        }

        if (!*ptr) {
            break;
        }

        ptr ++;
    }

    ptr = shell->line;
    while (1) {
        if (*ptr == '/') {
            shell_index = ptr + 1;
        }

        if (!*ptr) {
            break;
        }

        ptr ++;
    }

    if (shell_index == NULL) {
        ptr = &shell->line[strlen(shell->line)];
        while (ptr > shell->line) {
            if (*ptr == ' ' && *(ptr-1) != '\\' && *(ptr-1) != '\'' &&*(ptr-1) != '\"') {
                break;
            }
            else {
                ptr --;
            }
        }
        shell_index = ptr+1;
    } else if (*shell_index == ' ') {
        shell_index ++;
    }

    if (index == NULL)
        index = (*argv)[argc-1];

    if (index != NULL) {
        char *dest = index;

        /* fill the parent path */
        ptr = full_path;
        while (*ptr){
            ptr ++;
        }

        for (index = (*argv)[argc-1]; index != dest;) {
            *ptr++ = *index++;
        }

        *ptr = '\0';
        dir = opendir(full_path);
        if (dir == NULL) { /* open directory failed! */
            free(full_path);
            return;
        }

        /* restore the index position */
        index = dest;
    }
    int last_argv_len = strlen((*argv)[argc-1]);

    if ((shell->line[strlen(shell->line)-1] == ' ' && (shell->line[strlen(shell->line)-2] != '\\')) || 
        (last_argv_len == 0)) {
        shell_printf("\n");

        closedir(dir);
        dir = opendir(working_directory);
        if (dir == NULL) { /* open directory failed! */
            free(full_path);
            return;
        }
        shell_printf_all_file_name(dir);
        goto out;
    }

    /* display all of files and directories */
    if (*index == '\0') {
        shell_printf("\n");
        shell_printf_all_file_name(dir);
    } else {
        char *path = index;

        size_t length, min_length;

        min_length = 0;
        /* find the match string */
        for (;;) {
            dirent = readdir(dir);
            if (dirent == NULL)
                break;

            /* matched the prefix string */
            if (strncmp(index, dirent->d_name, strlen(index)) == 0) {
                if (min_length == 0) {
                    min_length = strlen(dirent->d_name);

                    /* save absolut path dirent for auto match */
                    if (strlen(full_path) + strlen(dirent->d_name) < 255) {
                        sprintf(abs_full_path, "%s/%s", full_path, dirent->d_name);
                    }
                    /* save dirent name */
                    strcpy(full_path, dirent->d_name);
                }

                length = str_common(dirent->d_name, full_path);

                if (length < min_length)
                {
                    min_length = length;
                }
            }

        } /* end of for (;;) */

        /* list view match result */
        if (min_length) {
            if (min_length <= strlen(full_path)) {
                /* list the candidate */
                rewinddir(dir);
                int echo_flag = 0;

                for (;;) {
                    dirent = readdir(dir);
                    if (dirent == NULL)
                        break;

                    if (strncmp(index, dirent->d_name, strlen(index)) == 0) {
                        if (!echo_flag) {
                            shell_printf("\n");
                        }

                        echo_flag++;
                        shell_printf("%s\n", dirent->d_name);
                    }
                }

                /* handle candidate to view  */
                if (echo_flag == 1) {

                    /* only find one match file/directory */
                    struct stat st;
                    int ret = 0;

                    ret = stat(abs_full_path, &st);
                    if (ret >= 0) {
                        if (S_ISDIR(st.st_mode)) {
                            /* is directory */
                            int len_change = 0;
                            length = index - path;
                            memcpy(shell_index, full_path, min_length);
                            for (int i = 0; i < min_length; i++) {
                                if (full_path[i] == ' ' || full_path[i] == '\"'|| full_path[i] == '\'') {
                                    shell_index[len_change++] = '\\';
                                }
                                shell_index[len_change++] = full_path[i];
                            }

                            shell_index[len_change++] = '/';
                            shell_index[len_change++] = '\0';
                        } else {
                            /* is file */
                            length = index - path;
                            int len_change = 0;
                            for (int i = 0; i < min_length; i++) {
                                if (full_path[i] == ' ' || full_path[i] == '\"'|| full_path[i] == '\'') {
                                    shell_index[len_change++] = '\\';
                                }
                                shell_index[len_change++] = full_path[i];
                            }

                            shell_index[len_change++] = ' ';
                            shell_index[len_change++] = '\0';
                        }
                    }

                } else {
                    int len_change = 0;

                    for (int i = 0; i < min_length; i++) {
                        if (full_path[i] == ' ' || full_path[i] == '\"'|| full_path[i] == '\'') {
                            shell_index[len_change++] = '\\';
                        }
                        shell_index[len_change++] = full_path[i];
                    }

                    shell_index[len_change++] = '\0';
                }

            } /* end of if (min_length ...) */

        } else {
            /* matched the special string: “..” */
            int postion = 0;

            length = strlen(path);
            if (length >= 2) {
                postion = length - 2;
            }

            if (strncmp(&path[postion], "..", 2) == 0) {
                path[length] = '/';
                path[length + 1] = '\0';
            }
        } /* end of if (min_length)...else ... */
    }
out:
    closedir(dir);
    free(full_path);
    free(abs_full_path);
}

void shell_auto_complete(struct shell *shell)
{
    int echo = (shell->stat == INPUT_STAT_TAB_KEY);
    int copy = SHELL_MATCH_COPY_MASK;
    int len, sub_len = 0;
    int argc = 0;
    char **argv = NULL;
    struct cmd_t *cmd;
    char typing_cmd_name[SHELL_CMD_NAME_SIZE];

    memset(typing_cmd_name, 0x00, SHELL_CMD_NAME_SIZE);

    char *shell_line_backup = NULL;
    uint8_t line_cur_pos_backup = shell->line_cur_pos;
    uint8_t line_position_backup = shell->line_position;
    uint8_t restore_len = line_position_backup - line_cur_pos_backup;

    if (restore_len > 0) {
        shell_line_backup = (char *)malloc(restore_len + 1);
        shell_line_backup[restore_len] = 0;

        memcpy(shell_line_backup, shell->line + line_cur_pos_backup, restore_len);
        memset(shell->line + shell->line_cur_pos, 0, shell->line_position - shell->line_cur_pos);
        shell->line_position = shell->line_cur_pos;
    }

    argc = shell_strspilt_alloc(&argv, shell->line, INPUT_STAT_TAB_KEY);

    if (argc == 0) {
        if (echo) {
            shell_printf_all_cmd_name(shell);
        }
        return;
    }
    /* Case1:match commands in single parameter  */
    if (argc == 1) {
        memcpy(typing_cmd_name, shell->line, shell->line_cur_pos);
        /* save current position */
        sub_len = strlen(typing_cmd_name);

        if (typing_cmd_name[sub_len-1] != ' '&& typing_cmd_name[sub_len-1] != '\'' && typing_cmd_name[sub_len-1] != '\"') {
            /*
             * auto complete match sub-string of the all cmd name,
             * typing_cmd_name maybe changed
             */
            shell_match_cmd_name(shell, &typing_cmd_name[0], echo | copy);
            len = strlen(typing_cmd_name);
            /* auto match typing name, relocation */
            if (len > sub_len) {
                if ( shell->line_position + (len - sub_len) >  SHELL_CMD_SIZE - 1) {
                    /*
                     * show auto match name
                     * out of line size discard excess data
                     */
                    memmove(&shell->line[len], &shell->line[sub_len], SHELL_CMD_SIZE - 1 - len);
                    memcpy(&shell->line[0], &typing_cmd_name[0], strlen(typing_cmd_name));

                    /* re-calculate position */
                    shell->line_cur_pos = strlen(typing_cmd_name);
                    shell->line_position = SHELL_CMD_SIZE - 1;

                } else {
                    /* show auto match name  */
                    memmove(&shell->line[len], &shell->line[sub_len], shell->line_position - sub_len);
                    memcpy(&shell->line[0], &typing_cmd_name[0], strlen(typing_cmd_name));
                    /* re-calculate position */
                    shell->line_cur_pos = strlen(typing_cmd_name);
                    shell->line_position = strlen(shell->line);
                }
            } /* end of if (len > sub_len) ... */

        } else if (echo) {
            cmd = shell_match_cmd_name(shell, argv[0], 0);

            if (cmd) {
                /* auto match list files/directory */
                shell_path_auto_complete(&argv, argc, shell);
            }

            /* re-calculate position */
            shell->line_cur_pos = shell->line_position = strlen(shell->line);
        }


        goto out;
    } else {
        shell_path_auto_complete(&argv, argc, shell);
        shell->line_cur_pos = shell->line_position = strlen(shell->line);
    }
    /* Case2:auto complete commands in multiple parameters */
    memcpy(typing_cmd_name, shell->line, shell->line_cur_pos);
    if (strlen(typing_cmd_name) <= strlen(argv[0])) {
        /* save current position */
        sub_len = strlen(typing_cmd_name);
        /*
         * auto complete match sub-string of the all cmd name,
         * typing_cmd_name maybe changed
         */
        cmd = shell_match_cmd_name(shell, typing_cmd_name, echo | copy);
        len = strlen(typing_cmd_name);

        /* auto match typing name, relocation */
        if (len > sub_len) {
            if ( shell->line_position + (len - sub_len) >  SHELL_CMD_SIZE - 1) {
                /*
                 * show auto match name
                 * out of line size discard excess data
                 */
                memmove(&shell->line[len], &shell->line[sub_len], SHELL_CMD_SIZE - 1 - len);
                memcpy(&shell->line[0], &typing_cmd_name[0], strlen(typing_cmd_name));

                /* re-calculate position */
                shell->line_cur_pos = strlen(typing_cmd_name);
                shell->line_position = SHELL_CMD_SIZE - 1;

            } else {
                /* show auto match name  */
                memmove(&shell->line[len], &shell->line[sub_len], shell->line_position - sub_len);
                memcpy(&shell->line[0], &typing_cmd_name[0], strlen(typing_cmd_name));
                /* re-calculate position */
                shell->line_cur_pos = strlen(typing_cmd_name);
                shell->line_position = strlen(shell->line);
            }
        } /* end of if (len > sub_len) ... */
    } else {
        /* the current position not at argv[0], nothing TODO */
    }

out:
    if (restore_len > 0) {
        uint8_t remaining_len = SHELL_CMD_SIZE - 1 - shell->line_position;

        restore_len = restore_len > remaining_len ? remaining_len : restore_len;
        memcpy(shell->line + shell->line_position, shell_line_backup, restore_len);

        shell->line_position += restore_len;
        free(shell_line_backup);
    }

    shell_strspilt_free(argc, &argv);
}

/**
 * @brief 将输入的命令行内容分割成参数
 * @param argv 存储命令行参数
 * @param input_buf input_buf 输入的字符缓冲区
 * @param mode 补全模式 INPUT_STAT_NORMAL下，将校验输入的命令格式
 * @return int 解析到的argv数目
 */
int shell_strspilt_alloc(char ** (*argv), char *input_buf, int mode)
{
    int cmd_line_len = strlen(input_buf);
    if (cmd_line_len == 0)
        return 0;

    struct argv_list *argv_head = NULL;

    char *cmd_line_buf = malloc(cmd_line_len+1);
    if (cmd_line_buf == NULL)
        return 0;

    memset(cmd_line_buf, 0, cmd_line_len +1);
    memcpy(cmd_line_buf, input_buf, cmd_line_len);

    int argc = 0;               //参数个数
    char split_ch_num[3] = {0}; // 0:', 1:", 2:\ 特殊分隔符计数
    char *flag_ch_pos[3] = {0}; // 特殊符起始位置
    char flag_spilt[2] = " ";   // 分割符号
    char *sub_str = NULL;
    char *buf_tail = NULL;
    argv_head = malloc(sizeof(struct argv_list));
    assert(argv_head != NULL);

    memset(argv_head, 0, sizeof(struct argv_list));
    struct argv_list  *const list_head = argv_head;
    struct argv_list *argv_p = argv_head;

    // 检查是否含有" ' 和 \, 计数并标志其起始位置
    char *ptr = cmd_line_buf;
    while ( ptr != (cmd_line_buf + cmd_line_len)) {
        switch (*ptr) {
            case '\'':
            {
                if ( *(ptr-1) != '\\') {
                    if (flag_ch_pos[0] == NULL) {
                        flag_ch_pos[0] = ptr;
                    }
                    split_ch_num[0] ++;
                }
                break;
            }
            case '\"':
            {
                if ( *(ptr-1) != '\\') {
                    if (flag_ch_pos[1] == NULL) {
                        flag_ch_pos[1] = ptr;
                    }
                    split_ch_num[1] ++;
                }
                break;
            }
            case '\\':
            {
                if (flag_ch_pos[2] == NULL) {
                    flag_ch_pos[2] = ptr;
                }
                split_ch_num[2] ++;
                break;
            }
        }
        ptr++;
    }

    ptr = NULL;
    int i = -1;
    // 使用的分割符号和解析起始位置确定, 仅使用一种分隔符
    if (flag_ch_pos[0] != NULL && flag_ch_pos[1] != NULL) {
        if (flag_ch_pos[0] < flag_ch_pos[1]) {
            i = 0;
        } else {
            i = 1;
        }
    } else if (flag_ch_pos[0] != NULL) {
        i = 0;
    } else if (flag_ch_pos[1] != NULL) {
        i = 1;
    } else {
        i = -1;
    }

    if (i != -1) {
        if (split_ch_num[i] % 2 != 0 && mode == INPUT_STAT_NORMAL) {
                shell_printf("\nCommand format error! Please check the number of \' or \".");
                goto out;
        }
        if (i == 0) {
            flag_spilt[0] = '\'';
            ptr = flag_ch_pos[0];
        } else {
            flag_spilt[0] = '\"';
            ptr = flag_ch_pos[1];
        }
    }

    argv_p->head = list_head;
    // 没有特殊符号 '和",仅考虑'\'
    if (ptr == NULL) {
        sub_str = strtok(cmd_line_buf, flag_spilt);
        if (sub_str != NULL) {
            while (sub_str != NULL) {
                argv_p->cmd = sub_str;
                if (sub_str[strlen(sub_str)-1] == '\\' && sub_str[strlen(sub_str)] == 0){
                    argv_p->index = argc;
                } else
                    argv_p->index = argc++;

                argv_p->next = (struct argv_list *)malloc(sizeof(struct argv_list));
                assert(argv_p->next != NULL);
                memset((argv_p->next), 0, sizeof(struct argv_list));

                argv_p->next->head = argv_p;
                argv_p->next->index = -1;
                argv_p = argv_p->next;

                sub_str = strtok(NULL, flag_spilt);
            }

            if (argv_p->head->cmd != NULL && argv_p->head->cmd[strlen(argv_p->head->cmd)-1] == '\\') {
                argc++;
            }
        } else {
            // 命令内容全为空格
            goto out;
        }
    } else {
        // 有特殊符号"或者','\'转义符失效
        // 先处理特殊符号前的部分, 按照空格分割
        // 然后 ptr 定位到 "或'起始处, 按照特殊符号分割

        buf_tail = malloc(strlen(ptr) + 1);
        assert(buf_tail);
        memset(buf_tail, 0, (strlen(ptr) + 1));
        memcpy(buf_tail, ptr, strlen(ptr));

        // 清除后部分的内容，以完成对前半部分的操作
        memset(ptr, 0, strlen(ptr));

        // 获取前半部分参数
        sub_str = strtok(cmd_line_buf, " ");
        while ( sub_str != NULL) {
            argv_p->cmd = sub_str;
            argv_p->index = argc++;

            argv_p->next = (struct argv_list *)malloc(sizeof(struct argv_list));
            assert(argv_p->next != NULL);
            memset(argv_p->next, 0, sizeof(struct argv_list));

            argv_p->next->head = argv_p;
            argv_p->next->index = -1;
            argv_p = argv_p->next;

            sub_str = strtok(NULL, " ");
        }

        // 获取后半部分参数
        int skip_time = 0;
        int sub_str_len = strlen(buf_tail);
        sub_str = strtok(buf_tail, flag_spilt);
        skip_time++;
        if (sub_str != NULL) {
            while ( sub_str != NULL) {
                ptr = sub_str;
                // 跳过用 " 做分隔符号间的空格
                if (skip_time % 2 == 0) {
                    sub_str = strtok(NULL, flag_spilt);
                    skip_time++;
                    continue;
                }

                argv_p->cmd = sub_str;
                argv_p->index = argc++;

                argv_p->next = (struct argv_list *)malloc(sizeof(struct argv_list));
                assert(argv_p->next != NULL);
                memset(argv_p->next, 0, sizeof(struct argv_list));

                argv_p->next->head = argv_p;
                argv_p->next->index = -1;
                argv_p = argv_p->next;

                sub_str = strtok(NULL, flag_spilt);
                skip_time++;
            }
        } else if(sub_str_len == 1){
            // [cd ']这个样式的命令补全时直接显示全部文件名
            argv_p->cmd = "";
            argv_p->index = argc++;
            argv_p->next = (struct argv_list *)malloc(sizeof(struct argv_list));
            assert(argv_p->next != NULL);
            memset(argv_p->next, 0, sizeof(struct argv_list));

            argv_p->next->head = argv_p;
            argv_p->next->index = -1;
            argv_p = argv_p->next;
        }
    }

    // 定位所有的参数成功，根据链表记录取出
    (*argv) = (char **)malloc(sizeof(char *)*argc);
    assert((*argv));
    memset((*argv), 0, sizeof(char *)*argc);

    // 记录参数开始位置
    struct argv_list *p_start = list_head;

    // 遍历参数指针
    argv_p = list_head;

    // 拼接获取argv
    int argv_len_cnt = 0, argc_index_last_time = 0;
    while ( 1 ) {
        if (argv_p->index == argc_index_last_time) {
            // 上次参数未结束
            if (argv_p->cmd != NULL)
                argv_len_cnt += strlen(argv_p->cmd);
        } else {
            // 上次参数结束,根据起始位置拼接参数
            if (argc_index_last_time == 0){
                // 第一个参数 cmd 可能在 shell_match_cmd_name 中被补全，预留足够的空间
                (*argv)[argc_index_last_time] = malloc(SHELL_CMD_SIZE);
                assert((*argv)[argc_index_last_time]);
                memset((*argv)[argc_index_last_time], 0, SHELL_CMD_SIZE);
            } else{
                (*argv)[argc_index_last_time] = malloc(argv_len_cnt + 1);
                assert((*argv)[argc_index_last_time]);
                memset((*argv)[argc_index_last_time], 0, argv_len_cnt + 1);
            }

            char *dest = (*argv)[argc_index_last_time];
            // 转义符多占的长度
            int len_change = 0;
            //  从开始位置拼接
            while (p_start->index == argc_index_last_time) {
                len_change = 0;
                memcpy(dest, p_start->cmd, strlen(p_start->cmd));

                int special_ch_pos = get_ch_pos_in_str(p_start->cmd, '\\');

                // 转义符生效且存在转义符
                if (flag_spilt[0] == ' ' && special_ch_pos != -1 ) {

                    while (special_ch_pos != -1) {
                        if (dest[special_ch_pos+1] == 0) {
                            dest[special_ch_pos] = ' ';
                        } else {
                            memmove(dest + special_ch_pos, dest + special_ch_pos + 1, strlen(dest + special_ch_pos) + 1);
                            len_change --;
                        }

                        special_ch_pos = get_ch_pos_in_str(dest, '\\');
                    }
                } else if(special_ch_pos != -1){
                    // 转义符失效但存在转义符
                    if (p_start->cmd[special_ch_pos+1] == 0) {
                        dest[special_ch_pos+1] = flag_spilt[0];
                        len_change ++;
                    }
                }
                dest += strlen(p_start->cmd) + len_change;
                p_start = p_start->next;
            }

            if (argv_p->index != -1) {
                argv_len_cnt = 0;
                argv_len_cnt += strlen(argv_p->cmd);
                argc_index_last_time = argv_p->index;
            } else {
                break;
            }
        }

        argv_p = argv_p->next;
    }
out:
    argv_p = list_head;
    struct argv_list *p_tmp = list_head;

    while (argv_p != NULL) {
        if (argv_p->index != -1) {
            p_tmp = argv_p->next;
            free(argv_p);
            argv_p = p_tmp;
        } else {
            free(argv_p);
            break;
        }
    }

    if (buf_tail)
        free(buf_tail);

    free(cmd_line_buf);
    return argc;
}

void shell_strspilt_free(int argc, char ** (*argv))
{
    int i;

    for (i = 0; i < argc; i++)
        free((*argv)[i]);

    free((*argv));
}

void shell_cmd_register(cmd_func_t cmd_fn, char *name, char *args, char *help)
{
    struct cmd_t *cmd = NULL;

    cmd = shell_get_cmd(name);
    assert(cmd == NULL);

    if (strlen(name) > SHELL_CMD_NAME_SIZE-1)
        printf("%s:cmd is too long, cmd length cut to %d\n", name, SHELL_CMD_NAME_SIZE-1);


    cmd = malloc(sizeof(struct cmd_t));
    assert(cmd);
    memset(cmd, 0, sizeof(struct cmd_t));

    cmd->func = cmd_fn;
    strncpy(cmd->name, name, SHELL_CMD_NAME_SIZE-1);
    cmd->name[SHELL_CMD_NAME_SIZE-1] = 0;

    if (args != NULL)
        cmd->argc = shell_strspilt_alloc(&cmd->argv, args, INPUT_STAT_NORMAL);

    cmd->help = help;

    thread_waiter_init(&cmd->cond);

    cmd_list_add(cmd);
}


static void shell_cmd_thread_func(struct cmd_arg *arg)
{
    shell_printf("\n");
    arg->func(arg, arg->argc, arg->argv);
    shell_printf(SHELL_PROMPT);

    shell_cmd_exit(arg);

}


thread_ptr_t shell_cmd_run(char *name, struct cmd_arg *arg, os_priority_id priority)
{
    thread_ptr_t thread;
    struct cmd_t *cmd;

    assert(name);
    assert(arg->shell);

    cmd = shell_get_cmd(name);
    if (cmd == NULL) {
        shell_printf("\n%s: command not found", name);
        return NULL;
    }

    arg->func = cmd->func;

    os_disable_preempt();

    thread = thread_create(cmd->name, SHELL_CMD_STACK_SIZE, (thread_func_t)shell_cmd_thread_func, arg);
    assert(thread);

    arg->thread = thread;
    thread_set_priority(thread, priority);

    os_enable_preempt();

    return thread;
}



void shell_cmd_exit(struct cmd_arg *arg)
{
    assert(arg);

    struct shell *shell = arg->shell;
    thread_ptr_t thread = arg->thread;

    if (arg == shell->cur_cmd) {
        shell->cur_cmd = NULL;
        shell->stat = INPUT_STAT_NORMAL;
    }

    shell_strspilt_free(arg->argc, &arg->argv);
    free(arg);

    thread_delete(thread);
}

void shell_cmd_prepare_exit(struct cmd_arg *arg)
{
    assert(arg);

    struct cmd_t *cmd = shell_get_cmd(arg->argv[0]);

    thread_waiter_wakeup(&cmd->cond);
}
