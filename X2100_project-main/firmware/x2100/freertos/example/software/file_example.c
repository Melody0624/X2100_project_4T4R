#include <dfs.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <dfs_posix.h>

#ifdef CONFIG_DFS

extern int dfs_init(void);

int file_test_ops(void)
{
    int fd;
    int size;
    char *filename = "/test/test_file.txt";
    char buffer2[128];
    char buffer[] = "This is test file operation write and read.";

    fd = open(filename, O_CREAT | O_RDWR, 0);
    assert(fd >= 0);
    printf("test_file: open: fd: %d.\n", fd);

    memset(buffer2, 0x00, sizeof(buffer2));
    size = read(fd, buffer2, sizeof(buffer2));
    printf("test_file: first read: size: %d [%s]\n", size, buffer2);

    lseek(fd, 0, SEEK_SET);
    size = write(fd, buffer, sizeof(buffer));
    printf("test_file: write: size: %d\n", size);

    lseek(fd, 0, SEEK_SET);
    memset(buffer2, 0x00, sizeof(buffer2));
    size = read(fd, buffer2, sizeof(buffer2));
    printf("test_file: second read: size: %d [%s]\n", size, buffer2);

    close(fd);

    size = strncmp(buffer, buffer2, strlen(buffer));
    if (size == 0) {
        printf("comparison write/read buffer OK.\n");
    } else {
        printf("comparison write/read buffer failed.\n");
        return size;
    }

#if 0
    ret = unlink(filename);
    if (ret < 0) {
        printf("test_file: remove file %s failed.\n", filename);
    } else {
        printf("test_file: remove file %s OK.\n", filename);
    }
#endif


    return 0;
}

int directory_test_ops(void)
{
    DIR *dir;
    int ret;
    int try_count = 0;
    const char *path = "/test/";

    while (1) {
        dir = opendir(path);
        if (dir == NULL) {
            goto mkdir_and_try_again;
        }

        printf("test_dir: success to open dir\n");

        closedir(dir);

        return 0;

    mkdir_and_try_again:
        if (try_count++ == 1) {
            printf("test_dir: open_dir errorno: %d\n", fs_get_errno());
            return -1;
        }

        ret = mkdir(path, 0666);
        if (ret) {
            printf("test_dir: mkdir error: %d errono: %d\n", ret, fs_get_errno());
            return -1;
        }

        printf("test_dir: success to mkdir\n");
    }

}

static void file_system_init_thread_func(void *data)
{
    int ret = -1;


    ret = directory_test_ops();
    if (ret < 0) {
        printf("directory operation test failed.\n");
    }

    ret = file_test_ops();
    if (ret < 0) {
        printf("file operation test failed.\n");
    }

    printf("FileSystem Test Successfully.\n");
    thread_delete(NULL);
}

void filesystem_example_test(void)
{
#ifdef CONFIG_OS
    thread_create("init-thread", 8192, file_system_init_thread_func, NULL);
#else
#error "NO OS not support File System."
#endif
}
#endif
