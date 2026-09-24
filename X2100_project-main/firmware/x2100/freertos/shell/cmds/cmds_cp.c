#include <string.h>
#include <fcntl.h>
#include <dfs_device.h>
#include <shell.h>

extern void copy(const char *src, const char *dst);

static void cmd_func_cp_help(char *cmd)
{
    shell_printf("Usage: %s <SOURCE...> <DIRECTORY>\n", cmd);
    shell_printf("\tCopy SOURCE to DEST, or multiple SOURCE(s) to DIRECTORY\n");
    shell_printf("Example:\n");
    shell_printf("\t%s 123.txt  abc.txt\n", cmd);
}

void cmd_func_cp(struct cmd_arg *arg, int argc, char **argv)
{
    int i = 0;
    int ret = 0;

    /* helpful info */
    if (argc < 3) {
        cmd_func_cp_help(argv[0]);
        goto cmd_out;
    }

    /* copy SOURCE to DEST */
    if (argc == 3) {
        copy(argv[1], argv[2]);
        goto cmd_out;
    }

    /* copy multiple SOURCE(s) to DIRECTORY */
    struct stat st;
    ret = stat(argv[argc-1], &st);
    if (ret < 0) {
        shell_printf("%s: target \"%s\": Is not a directory\n", argv[0], argv[argc-1]);
        goto cmd_out;
    }

    if (!S_ISDIR(st.st_mode)) {
        shell_printf("%s: target \"%s\": Is not a directory\n", argv[0], argv[argc-1]);
        goto cmd_out;
    }

    for (i = 1; i < argc-1; i++) {
        copy(argv[i], argv[argc-1]);
    }

cmd_out:
    return;
}


