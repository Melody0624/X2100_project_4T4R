#include <string.h>
#include <fcntl.h>
#include <dfs_device.h>
#include <dfs_posix.h>
#include <shell.h>

extern struct cmd_t *shell_get_cmd(const char *const name);
extern int rmdir(const char *pathname);
extern int unlink(const char *pathname);

static void cmd_func_rm_help(char *cmd)
{
    shell_printf("Usage: %s <FILE/Directory>...\n", cmd);
    shell_printf("\tRemove (unlink) the FILE(s)\n");
    shell_printf("\t-r remove directories and their contents recursively\n");
    shell_printf("Example:\n");
    shell_printf("\t%s 123.txt\n", cmd);
    shell_printf("\t%s -r /test/\n", cmd);
}

/*
 * return: =1 variable is argument
 *         =0
 */
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

static inline int remove_directory(char *pathname)
{
    int ret = 0;
    int error = 0;

    ret = rmdir(pathname);
    if (ret < 0) {
        error = fs_get_errno();
    }

    return error;
}


static inline int remove_file(char *pathname)
{
    int ret = 0;
    int error = 0;

    ret = unlink(pathname);
    if (ret < 0) {
        error = fs_get_errno();
    }

    return error;
}

/*
 * return <0: remove file / directory failed
 *        =0: remove successful.
 */
static int remove_target_recursively(char *pathname)
{
#define FILE_DIR_PATH_LENGTH            (2048)

    int ret = 0;
    struct stat st;
    char *full_path;
    char *current_path;
    struct dirent *dirent = NULL;
    DIR *dir = NULL;

    if (!pathname)
        return -1;

    current_path = (char *)malloc(FILE_DIR_PATH_LENGTH);
    if (current_path == NULL) {
        shell_printf("%s: path:%s. malloc current failed", "rm", pathname);
        return -1;
    }

    full_path = (char *)malloc(FILE_DIR_PATH_LENGTH);
    if (full_path == NULL) {
        shell_printf("%s: path:%s. malloc full failed", "rm", pathname);
        ret = -1;
        goto cmd_out;
    }

    memset(full_path, 0x00, FILE_DIR_PATH_LENGTH);

    /* full path */
    getcwd(full_path, FILE_DIR_PATH_LENGTH);
    if (full_path[strlen(full_path) - 1]  != '/') {
        strcat(full_path, "/");
    }
    strcat(full_path, pathname);

    ret = stat(full_path, &st);
    if (ret < 0) {
        shell_printf("%s: cannot remove \"%s\": No such file or directory\n", "rm", full_path);
        ret = -1;
        goto cmd_out;
    }

    if (S_ISDIR(st.st_mode)) {
        /* remove directory */
        dir = opendir(full_path);
        if (dir == NULL) { /* open directory failed! */
            ret = -1;
            goto cmd_out;
        }

        /* find all file / directory */
        for (;;) {
            dirent = readdir(dir);
            if (dirent == NULL)
                break;

            memset(current_path, 0x00, sizeof(FILE_DIR_PATH_LENGTH));
            sprintf(current_path, "%s/%s", pathname, dirent->d_name);

            remove_target_recursively(current_path);
        }

        closedir(dir);
        ret = remove_directory(pathname);
        switch (ret) {
         case -EROFS:
             shell_printf("\"%s\": directory is not empty\n", pathname);
             break;
         case -EBUSY:
             shell_printf("\"%s\": directory is busy\n", pathname);
             break;
         case 0:
             /* this is successfull, nothing todo */
             break;
         default:
             shell_printf("remove directory failed:\"%s\": error=%d\n", pathname, ret);
             break;
         }

    } else {
        /* remove file */
        ret = remove_file(pathname);
        if (ret < 0) {
            shell_printf("remove file failed:\"%s\": error=%d\n", pathname, ret);
        }
    }

cmd_out:
    if (full_path) {
        free(full_path);
    }
    if (current_path) {
        free(current_path);
    }

    return 0;
}

void cmd_func_rm(struct cmd_arg *arg, int argc, char **argv)
{
    int i = 0;
    int ret = 0;
    int _has_recursively = 0;
    struct cmd_t *cmd = NULL;

    /* helpful info */
    if (argc == 1) {
        cmd_func_rm_help(argv[0]);
        goto cmd_out;
    } else if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        cmd_func_rm_help(argv[0]);
        goto cmd_out;
    }

    /* check argument */
    cmd = shell_get_cmd(argv[0]);
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) {
            _has_recursively = 1;
            break;
        }
    }

    /* command */
    if (_has_recursively == 0) {

        struct stat st;

        for (i=1; i<argc; i++) {
            ret = stat(argv[i], &st);
            if (ret < 0) {
                shell_printf("%s: cannot remove \"%s\": No such file or directory\n", argv[0], argv[i]);
                continue;
            }

            if ( (strcmp(argv[i], "..") == 0) || (strcmp(argv[i], "../") == 0)
                    || (strcmp(argv[i], ".") == 0) || (strcmp(argv[i], "./") == 0) ) {

                shell_printf("rm: cannot remove directory \"%s\"\n", argv[i]);
                continue;
            }

            if (S_ISDIR(st.st_mode)) {
                /* remove directory */
                ret = remove_directory(argv[i]);
                switch (ret) {
                case -EROFS:
                    shell_printf("\"%s\": directory is not empty\n", argv[i]);
                    break;
                case 0:
                    /* this is successfull, nothing todo */
                    break;
                default:
                    shell_printf("remove directory failed:\"%s\": error=%d\n", argv[i], ret);
                    break;
                }

            } else {
                /* remove file */
                ret = remove_file(argv[i]);
                if (ret < 0) {
                    shell_printf("remove file failed:\"%s\": error=%d\n", argv[i], ret);
                }

            }
        } /* end of for(... */
    } /* end of if (_has_recursively == 0) */

    else if (_has_recursively == 1) {


        for (i=1; i<argc; i++) {
            /* is argument not directory/file, no need handle */
            ret = check_is_argument(cmd, argv[i]);
            if (ret == 1) {
                continue;
            }

            if ( (strcmp(argv[i], "..") == 0) || (strcmp(argv[i], "../") == 0)
                    || (strcmp(argv[i], ".") == 0) || (strcmp(argv[i], "./") == 0) ) {

                shell_printf("rm: cannot remove directory \"%s\"\n", argv[i]);
                continue;
            }

            remove_target_recursively(argv[i]);


        } /* end of for(i=1; i<argc ... */


    } /* end of if (_has_recursively == 1) */


cmd_out:
    return;
}


