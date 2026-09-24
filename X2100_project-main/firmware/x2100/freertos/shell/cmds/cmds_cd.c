#include <string.h>
#include <shell.h>
#include <dfs_posix.h>


#ifdef DFS_USING_WORKDIR

extern char working_directory[];
extern int chdir(const char *path);

void cmd_func_cd(struct cmd_arg *arg, int argc, char **argv)
{
    if (argc == 1) {
        shell_printf("%s\n",working_directory);
    } else {
        if (chdir(argv[1]) != 0) {
            switch(fs_get_errno()) {
            case -ENOTDIR:
                shell_printf("%s: %s: Not a directory\n", argv[0], argv[1]);
                break;
            default:
                shell_printf("%s: %s: No such directory\n", argv[0], argv[1]);
                break;
            }

        }
    }

    return;
}

#endif

