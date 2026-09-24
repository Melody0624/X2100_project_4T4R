#include <string.h>
#include <dfs_device.h>
#include <shell.h>

extern struct cmd_t *shell_get_cmd(const char *const name);
extern int mkdir(const char *path, mode_t mode);

static void cmd_func_mkdir_help(char *cmd)
{
    shell_printf("Usage:%s [OPTION] directory\n", cmd);
    shell_printf("\tCreate the DIRECTORY(ies), if they do not already exist\n");
    shell_printf("\t-p, --parents     no error if existing, make parent directories as needed\n");
    shell_printf("Example:\n");
    shell_printf("\t%s 123\n", cmd);
    shell_printf("\t%s -p 111/222/333\n", cmd);
}

static inline int _cmds_create_dir(char *path)
{
    int error = 0;

    error = mkdir(path, 0);

    if (error < 0) {
        error = fs_get_errno();
    }

    return error;

}

void cmd_func_mkdir(struct cmd_arg *arg, int argc, char **argv)
{
    int i = 0;
    int ret = 0;
    int _has_parents = 0;
    struct cmd_t *cmd = NULL;

    /* helpful info */
    if (argc == 1) {
        cmd_func_mkdir_help(argv[0]);
        goto cmd_out;
    } else if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        cmd_func_mkdir_help(argv[0]);
        goto cmd_out;
    }

    /* check argument */
    cmd = shell_get_cmd(argv[0]);
    if (cmd == NULL){
        shell_printf("%s: command not found\n", argv[0]);
        goto cmd_out;
    }
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            _has_parents = i;
            break;
        }
    }

    /* command */
    if (_has_parents == 0) {
        for (i = 1; i < argc; i++) {
            ret = _cmds_create_dir(argv[i]);
            if (ret == -ENOENT) {
                /* ENOENT */
                shell_printf("%s: cannot create directory \"%s\" : No such directory\n", argv[0], argv[i]);
            } else if (ret == -EEXIST) {
                /* EEXIST */
                shell_printf("%s: cannot create directory \"%s\" : File exists\n", argv[0], argv[i]);
            } else if (ret == -EACCES) {
                /* EACCES */
                shell_printf("%s: cannot create directory \"%s\" : Permission denied\n", argv[0], argv[i]);
            }
        } /* end of for(... */
    }

    if (_has_parents) {
        if (argc == 2) {
            shell_printf("%s: missing operand\n", argv[0]);
            goto cmd_out;
        }

        char *str_split = NULL;
        char create_dir_path[256];
        for (i = 1; i < argc; i++) {
            /* is argument not directory, no need handle */
            if (i == _has_parents) {
                continue;
            }

            if (strlen(argv[i]) >= sizeof(create_dir_path)) {
                shell_printf("the directory path is too long\n");
                continue;
            }

            memset(create_dir_path, 0x00, sizeof(create_dir_path));
            str_split = strtok(argv[i], "/");
            while (str_split) {
                if (strlen(create_dir_path) == 0) {
                    sprintf(create_dir_path, "%s",str_split);
                } else {
                    sprintf(create_dir_path, "%s/%s", create_dir_path, str_split);
                }
                ret = _cmds_create_dir(create_dir_path);
                if (ret == 0) {
                    /* create directory success. */

                } else if (ret == -EEXIST) {
                    /* nothing to do, continue */

                } else if (ret == -ENOENT) {
                    shell_printf("%s: cannot create directory \"%s\" : No such directory\n", argv[0], argv[i]);
                    break;

                } else if (ret == -EACCES) {
                    /* EACCES */
                    shell_printf("%s: cannot create directory \"%s\" : Permission denied\n", argv[0], argv[i]);
                } else  {
                    shell_printf("%s: cannot handle : error =%d\n", argv[0], ret);
                    break;
                }

                str_split = strtok(NULL, "/");
            } /* end of while(... */

        } /* end of for(... */
    }


cmd_out:
    return;
}


