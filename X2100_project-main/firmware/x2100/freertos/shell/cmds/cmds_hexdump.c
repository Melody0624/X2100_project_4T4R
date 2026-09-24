#include <shell.h>
#include <dfs.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <dfs_shell.h>
#include <string.h>
#include <fcntl.h>
#include <dfs_device.h>
#include <dfs_posix.h>
#include <stdlib.h>

#define TYPE_HEX     0
#define TYPE_DEC     1
#define TYPE_CHAR    2
#define TYPE_U_INT   3

static void cmd_func_hexdump_help(char *cmd)
{
    shell_printf("Usage: %s <File> <option>\n", cmd);
    shell_printf("\tspecial way to show file content\n");
    shell_printf("Options:\n");
    shell_printf("-d   Decimal printing\n");
    shell_printf("-h   Hexadecimal print\n");
    shell_printf("-c   Characters print\n");
    shell_printf("-u   Unsigned int print\n");
    shell_printf("-w   Byte wide\n");
    shell_printf("-n   The length of the print\n");
    shell_printf("-l   The length of the print\n");
    shell_printf("Example:\n");
    shell_printf("\t%s  /test/123.txt\n", cmd);
    shell_printf("\t%s  /test/123.txt  -w 4\n", cmd);
    shell_printf("\t%s  /test/123.txt  -l 1024\n", cmd);
    shell_printf("\t%s  /test/123.txt  -n 8\n", cmd);
    shell_printf("\t%s  /test/123.txt  -d\n", cmd);
}

/*
 *  filename     filename
 *  data_width   Byte wide
 *               1  char
 *               2  short
 *               4  int
 *  row_num      The number of prints per line
 *  len          The length of the print
 *               -1 : print all
 *               >0 : print part
 *  data_type    data_type
 *               0 : Hexadecimal print
 *               1 : Decimal printing
 *               2 : Characters print
 */
static void hexdump(const char *filename, int len, int row_num, int data_width, int data_type)
{
    int fd, i, ret = 0;

    fd = open(filename, O_RDONLY);
    if (fd == -1) {
        elm_printf("file open is failure : %d\n", errno);
        return;
    }

    char *char_buf = "";
    char *dec_buf = "";
    char *hex_buf = "";
    char *uint_buf = "";

    if (data_width == 1) {
        char_buf = "%c ";
        dec_buf = "%03d ";
        hex_buf = "%02x ";
        uint_buf = "%03u ";
    }
    if (data_width == 2) {
        char_buf = "%c ";
        dec_buf = "%05d ";
        hex_buf = "%04x ";
        uint_buf = "%05u ";
    }
    if (data_width == 4) {
        char_buf = "%c ";
        dec_buf = "%10d ";
        hex_buf = "%08x ";
        uint_buf = "%10u ";
    }

    while (len) {
        for (i = 0; i < row_num; i++) {

            char *str;
            char value1;
            short value2;
            int value4;

            if (data_type == TYPE_HEX)
                str = hex_buf;
            if (data_type == TYPE_DEC)
                str = dec_buf;
            if (data_type == TYPE_CHAR)
                str = char_buf;
            if (data_type == TYPE_U_INT)
                str = uint_buf;

            if (data_width == 1)
                ret = read(fd, &value1, 1);
            if (data_width == 2)
                ret = read(fd, &value2, 2);
            if (data_width == 4)
                ret = read(fd, &value4, 4);

            if (ret <= 0) {
                len = 0;
                break;
            }

            if (data_width == 1) {
                if (data_type != TYPE_DEC)
                    elm_printf(str, (unsigned char)value1);
                else
                    elm_printf(str, value1);
            }
            if (data_width == 2) {
                if (data_type != TYPE_DEC)
                    elm_printf(str, (unsigned short)value2);
                else
                    elm_printf(str, value2);
            }
            if (data_width == 4) {
                if (data_type != TYPE_DEC)
                    elm_printf(str, (unsigned int)value4);
                else
                    elm_printf(str, value4);
            }

            if(len != -1)
                len--;

            if (len == 0)
                break;
        }
        elm_printf("\n");
    }
    close(fd);
}

static int str_to_int (int i, char **arg)
{
    char *endptr = NULL;
    int ret;

    ret = (int)strtoul(arg[i+1], &endptr, 10);
    if (!(*endptr >= 0 && *endptr <= 9)) {
        elm_printf("The parameter is not a number : %d\n", arg[i]);
        cmd_func_hexdump_help(arg[0]);
        return -1;
    }
    return ret;
}

/*
 * -d   Decimal printing
 * -h   Hexadecimal print
 * -c   Characters print
 * -w   Byte wide
 * -n   The number of prints per line
 * -l   The length of the print
 */
void cmd_func_hexdump(struct cmd_arg *arg, int argc, char **argv)
{
    struct stat st;
    int ret, i, data_width = 2, row_num = 16, len = -1, data_type = 0;
    const char *filename = NULL;

    if (argc == 1) {
        cmd_func_hexdump_help(argv[0]);
        return;
    }

    for (i = 1; i < argc ; i++) {

        if (strcmp(argv[i], "--help") == 0) {
            cmd_func_hexdump_help(argv[0]);
            return;
        }

        if (strcmp(argv[i], "-d") == 0) {
            data_type = TYPE_DEC;
            continue;
        }

        if (strcmp(argv[i], "-c") == 0) {
            data_type = TYPE_CHAR;
            continue;
        }

        if (strcmp(argv[i], "-h") == 0) {
            data_type = TYPE_HEX;
            continue;
        }

        if (strcmp(argv[i], "-u") == 0) {
            data_type = TYPE_U_INT;
            continue;
        }

        if (strcmp(argv[i], "-w") == 0) {
            data_width = str_to_int (i, argv);
            if (data_width == -1)
                return;
            i++;
            continue;
        }

        if (strcmp(argv[i], "-n") == 0) {
            row_num = str_to_int (i, argv);
            if (row_num == -1)
                return;
            i++;
            continue;
        }

        if (strcmp(argv[i], "-l") == 0) {
            len = str_to_int (i, argv);
            if (len == -1)
                return;
            i++;
            continue;
        }

        ret = stat(argv[i], &st);
        if (ret) {
            shell_printf("%s: \"%s\": No such file\n", argv[0], argv[i]);
            cmd_func_hexdump_help(argv[0]);
            return;
        }

        if (S_ISDIR(st.st_mode)) {
            shell_printf("%s: \"%s\": Is a directory\n", argv[0], argv[i]);
            cmd_func_hexdump_help(argv[0]);
             return;
        }

        filename = argv[i];
    }

    if (filename == NULL) {
        elm_printf("No file name given\n");
        cmd_func_hexdump_help(argv[0]);
        return;
    }

    hexdump(filename, len, row_num, data_width, data_type);
}