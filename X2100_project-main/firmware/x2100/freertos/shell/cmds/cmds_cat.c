#include <string.h>
#include <fcntl.h>
#include <dfs_device.h>
#include <shell.h>

extern void cat(const char *filename);

static void cmd_func_cat_help(char *cmd)
{
    shell_printf("Usage: %s <File>\n", cmd);
    shell_printf("\tshow file content\n");
    shell_printf("Example:\n");
    shell_printf("\t%s  /test/123.txt\n", cmd);
}

void cmd_func_cat(struct cmd_arg *arg, int argc, char **argv)
{
    int ret = 0;
    int i = 0;

    /* helpful info */
    if (argc == 1) {
        cmd_func_cat_help(argv[0]);
        goto cmd_out;
    }

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        cmd_func_cat_help(argv[0]);
        goto cmd_out;
    }

    /* command */
    struct stat st;

    for (i = 1; i < argc; i++) {
        ret = stat(argv[i], &st);

        if (ret < 0) {
            shell_printf("%s: \"%s\": No such file\n", argv[0], argv[i]);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            shell_printf("%s: \"%s\": Is a directory\n", argv[0], argv[i]);
        } else {
            /* show file content */
            cat( argv[i] );
        }

    }


cmd_out:
    return;
}


