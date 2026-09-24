#include <string.h>
#include <fcntl.h>
#include <shell.h>
#include <dfs_posix.h>

#ifdef CONFIG_OS_MODULE

extern int insert_module(void *data, unsigned long len,int argc, const char **argv);

static void cmd_func_insmod_help(char *cmd)
{
    shell_printf("Usage: %s <filename>  [module options...]\n", cmd);
    shell_printf("\tSimple program to insert a module into the Kernel\n");
    shell_printf("Example:\n");
    shell_printf("\t%s /module_example.mo\n", cmd);
}

void cmd_func_insmod(struct cmd_arg *arg, int argc, char **argv)
{
    int ret = 0;

    /* helpful info */
    if (argc < 2) {
        cmd_func_insmod_help(argv[0]);
        goto cmd_out;
    }

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        cmd_func_insmod_help(argv[0]);
        goto cmd_out;
    }

    /* command */
    struct stat st;
    void *data = NULL;
    char *filename = argv[1];
    int fd = -1;

    ret = stat(filename, &st);
    if (ret < 0) {
        shell_printf("%s: \"%s\": No such file\n", argv[0], filename);
        goto cmd_out;
    }

    if (S_ISDIR(st.st_mode)) {
        shell_printf("%s: \"%s\": Is a directory\n", argv[0], filename);
        goto cmd_out;
    }

    /* get module info */
    data = malloc(st.st_size);
    if (!data) {
        printf("%s:malloc module info failed\n", argv[0]);
        goto cmd_err_malloc;
    }

    fd = open(filename, O_RDONLY, 0);
    if (fd < 0) {
        printf("open file(%s) failed err=%d\n", filename, fs_get_errno());
        goto cmd_err_open;
    }

    ret = read(fd, data, st.st_size);
    if (ret != st.st_size) {
        printf("read file(%s) failed. read length:%d:  !=  Except length:%ld\n", filename, ret, st.st_size);
        goto cmd_err_read;
    }

    ret = insert_module(data, st.st_size, argc - 2, (const char **)&argv[2]);
    if (ret < 0) {
        switch (ret) {
        case -EINVAL:
            shell_printf("%s can't insert '%s': Invalid argument\n", argv[0], argv[1]);
            break;
        case -EEXIST:
            shell_printf("%s can't insert '%s': File exists\n", argv[0], argv[1]);
            break;
        default:
            shell_printf("%s can't insert '%s': Unknow error:%d\n", argv[0], argv[1], ret);
            break;
        }

    }

cmd_err_read:
    if (fd > 0) {
        close(fd);
    }

cmd_err_open:
    if (data) {
        free(data);
    }

cmd_err_malloc:
cmd_out:
    return;
}

#endif /* end of CONFIG_OS_MODULE */
