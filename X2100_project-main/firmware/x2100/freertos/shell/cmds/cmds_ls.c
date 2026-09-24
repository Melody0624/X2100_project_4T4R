#include <string.h>
#include <shell.h>

extern struct cmd_t *shell_get_cmd(const char *const name);
extern void ls(const char *pathname);

#ifdef DFS_USING_WORKDIR
extern char working_directory[];
#endif

static inline int check_is_argument(struct cmd_t *cmd, const char *name)
{
    int i = 0;
    int ret = 0;

    for (i = 0; i < cmd->argc; i++) {
        if (strcmp(cmd->argv[i], name) == 0) {
            ret = 1;
            break;
        }
    }

    return ret;
}

void cmd_func_ls(struct cmd_arg *arg, int argc, char **argv)
{
    int i = 0;
    struct cmd_t *cmd = NULL;
    cmd = shell_get_cmd(argv[0]);

    if (argc == 1) {
#ifdef DFS_USING_WORKDIR
        ls(working_directory);
#else
        ls("/");
#endif
    } else {

        for (i = 1; i < argc; i++) {
            if (check_is_argument(cmd, argv[i]))
                continue;

            ls(argv[i]);
        }
    }

}


