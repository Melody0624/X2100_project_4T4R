#include <os.h>
#include <dfs.h>
#include <dfs_device.h>
#include <dfs_file.h>
#include <shell.h>
#include <printf.h>
#include <fcntl.h>
#include <string.h>
#include <dfs_posix.h>
#include <driver/uart_console.h>
#include <stdlib.h>
#include <lrzsz/lrzsz.h>

/**
 * @brief the help information for sz
 */
static void cmd_func_sz_help(char *cmd)
{
    shell_printf("Usage:%s [file_path]\n", cmd);
    shell_printf("Example:\n");
    shell_printf("\t%s file_path\n", cmd);
}

/**
 * @brief read your file and send it
 *
 * @param filename which is the file you will send
 * @param send_times from send_times to 1，1 to end
 * @return int 0 on success, -1 on failed
 */
static int cmd_lsz_send(const char *filename, int send_n, int send_times)
{
    /* send file content，这时不要打开终端 */
    uart_console_disconnect_to_console();
    int ret = lsz_send(filename, send_n, send_times);
    uart_console_connect_to_console();

    return ret;
}

/**
 * @brief the main func for sz,
 * you can use it to send your file
 * by using serial
 */
static void cmd_lsz(struct cmd_arg *cmd_arg, int argc, char **argv)
{
    /* helpful info */
    if (argc == 1) {
        cmd_func_sz_help(argv[0]);
        goto cmd_lsz_out;
    }

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        cmd_func_sz_help(argv[0]);
        goto cmd_lsz_out;
    }

    /* command */
    int ret = 0;
    int i = 0;
    struct stat st;
    int status;

    for (i = 1; i < argc; i++) {
        ret = stat(argv[i], &st);

        if (ret < 0) {
            shell_printf("%s: \"%s\": No such file\n", argv[0], argv[i]);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            shell_printf("%s: \"%s\": Is a directory\n", argv[0], argv[i]);
            shell_printf("%s: unspport directory\n", argv[0]);
        } else {
            status = cmd_lsz_send(argv[i], argc - 1, argc - i);
            if (status < 0)
                shell_printf("\n ERROR file: %s send \n", argv[i]);
            else
                shell_printf("file: %s send success\n", argv[i]);
        }
    }

cmd_lsz_out:
    return;
}

/**
 * @brief the help information for rz
 */
static void cmd_func_rz_help(char *cmd)
{
    shell_printf("Usage:%s\n", cmd);
    shell_printf("Example:\n");
    shell_printf("\t%s waitting...\n", cmd);
}

/**
 * @brief the main func for rz,
 * you can use it to receive your file
 * by using serial
 */
static void cmd_lrz(struct cmd_arg *cmd_arg, int argc, char **argv)
{
    int ret;

    if (argc == 1) {
        /* receive file content，这时不要打开终端 */
        uart_console_disconnect_to_console();
        ret = lrz_receive();
        uart_console_connect_to_console();
        if (ret < 0)
            shell_printf("\n\tfiles received ERROR\n");
        else
            shell_printf("\n\tfiles received %d\n", ret);
    } else {
        /* helpful info */
        cmd_func_rz_help(argv[0]);
    }
}

/**
 * @brief init func for lrzsz
 */
void cmd_lrzsz_init(void)
{
    shell_cmd_register(cmd_lsz, "sz", NULL, "sz send file to your host by using zmodem");
    shell_cmd_register(cmd_lrz, "rz", NULL, "rz receive files by using zmodem");
}