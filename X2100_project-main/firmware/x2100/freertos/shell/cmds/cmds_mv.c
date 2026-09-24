#include <string.h>
#include <fcntl.h>
#include <dfs.h>
#include <dfs_posix.h>
#include <dfs_device.h>
#include <shell.h>


static void cmd_func_mv_help(char *cmd)
{
    shell_printf("Usage: %s SOURCE DEST\n", cmd);
    shell_printf("\tRename SOURCE to DEST, or move SOURCE(s) to DIRECTORY\n");
    shell_printf("Example:\n");
    shell_printf("\t%s 123.txt  abc.txt\n", cmd);
}

void cmd_func_mv(struct cmd_arg *arg, int argc, char **argv)
{
    /* helpful info */
    if (argc != 3) {
        cmd_func_mv_help(argv[0]);
        goto cmd_out;
    }

    /* command */
    /* move SOURCE to DEST */
    int fd;
    char *dest = NULL;
    fd = open(argv[2], O_DIRECTORY, 0);
    if (fd > 0) {
        /* DEST is directory */
        char *src;
        close(fd);

        dest = malloc(DFS_PATH_MAX);
        if (dest == NULL) {
            shell_printf("out of memory.");
            goto cmd_out;
        }

        src = argv[1] + strlen(argv[1]);
        while(src != argv[1]) {
            if (*src == '/') {
                break;
            }
            src--;
        } /* end of while(... */

        snprintf(dest,DFS_PATH_MAX, "%s/%s", argv[2], src);

    } else {
        /* DEST is file */
        fd = open(argv[2], O_RDONLY, 0);
        if (fd >= 0) {
            close(fd);
            unlink(argv[2]);
        }

        dest = argv[2];
    }

    rename(argv[1], dest);

    if (dest != NULL && dest != argv[2]) {
        free(dest);
    }

cmd_out:
    return;
}


