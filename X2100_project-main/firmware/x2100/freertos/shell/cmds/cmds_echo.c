#include <string.h>
#include <dfs_posix.h>
#include <dfs_device.h>
#include <shell.h>

extern int write(int fd, const void *buf, size_t len);

enum {
    FILE_NO_OPERATE                     = 0,
    FILE_RE_WRITE                       = 1,
    FILE_APPEND                         = 2,
};
static void cmd_func_echo_help(char *cmd)
{
    shell_printf("Usage: %s <STRING>  >  <FILE>\n", cmd);
    shell_printf("\techo string to file\n");
    shell_printf("\t>  re-write the strint to file. if the file does not exist, create it.\n");
    shell_printf("\t>> append the string to the end of file\n");
    shell_printf("Example:\n");
    shell_printf("\t%s 123 >  123.txt\n", cmd);
    shell_printf("\t%s 123 >> 123.txt\n", cmd);
}

/*
 * return =0: is not argumen
 *        =1: re-write to file
 *        =2: append to file
 */
static inline int check_is_argument(const char *argument)
{
    int ret = FILE_NO_OPERATE;

    if (strcmp(argument, ">") == 0) {
        ret = FILE_RE_WRITE;
    } else if (strcmp(argument, ">>") == 0) {
        ret = FILE_APPEND;
    }

    return ret;
}

void cmd_func_echo(struct cmd_arg *arg, int argc, char **argv)
{
    int ret = 0;

    /* helpful info */
    if (argc != 4) {
        cmd_func_echo_help(argv[0]);
        goto cmd_out;
    }

    /* command */

    /* argument check: argv[2] must be ">" / ">>" */
    ret = check_is_argument(argv[2]);
    if (ret == FILE_NO_OPERATE) {
        cmd_func_echo_help(argv[0]);
        goto cmd_out;
    }

    /* write string to file */
    int fd;
    int file_flag = 0;

    switch (ret) {
    case FILE_RE_WRITE:
        file_flag = O_WRONLY | O_TRUNC | O_CREAT;
        break;

    case FILE_APPEND:
    default:
        file_flag = O_WRONLY | O_APPEND | O_CREAT;
        break;
    } /* end of switch(... */

    struct stat st;
    ret = stat(argv[3], &st);
    if (ret >= 0) {
        if (S_ISDIR(st.st_mode)) {
            shell_printf("%s: \"%s\": Is a directory\n", argv[0], argv[3]);
            goto cmd_out;
        }
    }

    fd = open(argv[3], file_flag, 0);
    if (fd < 0) {
        switch (fs_get_errno()) {
        case -EACCES:
            shell_printf("%s: can't create \"%s\": Permission denied\n", argv[0], argv[3]);
            break;

        default:
            shell_printf("%s: operation \"%s\" failed: error = %d\n", argv[0], argv[3], fs_get_errno());
            break;
        } /* end of switch */

    } else {
        int string_len = strlen(argv[1]);
        ret = write(fd, argv[1], string_len);
        close(fd);
        if ( ret != string_len ) {
            switch (fs_get_errno()) {
            case -EACCES:
                shell_printf("%s: can't create \"%s\": Permission denied\n", argv[0], argv[3]);
                break;

            default:
                shell_printf("%s: write to file:%s error\n", argv[0], argv[3]);
                shell_printf("Actual length:%d:  !=  Except length:%d.\n", ret, string_len);
                break;
            } /* end of switch */
        }
    } /* end of if ... else ... */

cmd_out:
    return;
}


