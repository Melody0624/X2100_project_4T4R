#include <dfs.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <dfs_posix.h>

#ifdef CONFIG_DFS

#define TEST_FILE_SIZE  (200 * 1024)
#define TEST_COUNT      128

extern int dfs_init(void);
extern int file_system_insert_partition(const char *name);
extern int file_system_remove_partition(const char *name);

static int file_test_ops(void)
{
    int i;
    int fd;
    int size;
    int sum_size = 0;
    int cluster = 0;
    int now, time, rate;
    char filename[64] = "/mmcblk0p0";
    unsigned char *buf = malloc (TEST_FILE_SIZE);

    fd = open(filename, O_DIRECTORY, 0);
    assert(fd >= 0);

    ioctl(fd, DFS_GET_CLUSTER_SIZE_CMD, &cluster);
    printf("%s: cluster = %d KB\n", filename, cluster / 1024);

    close(fd);

    now = systick_get_time_ms();

    for (i = 0; i < TEST_COUNT; i++) {
        sprintf(filename, "/mmcblk0p0/test_file%03d.txt", i);
        memset(buf, 'a' + (i % 26), TEST_FILE_SIZE);

        fd = open(filename, O_CREAT | O_RDWR, 0);
        assert(fd >= 0);

        size = write(fd, buf, TEST_FILE_SIZE);
        close(fd);
        sum_size += size;
    }

    time = systick_get_time_ms() - now;

    rate = sum_size / time;

    printf("write %d KB, rate = %d KB/s\n", sum_size / 1024, rate);

    sum_size = 0;
    now = systick_get_time_ms();

    for (i = 0; i < TEST_COUNT; i++) {
        fd = open(filename, O_CREAT | O_RDWR, 0);
        assert(fd >= 0);

        size = read(fd, buf, TEST_FILE_SIZE);
        close(fd);
        sum_size += size;
    }

    time = systick_get_time_ms() - now;
    rate = sum_size / time;

    printf("read %d KB, rate = %d KB/s\n\n", sum_size / 1024, rate);

    free(buf);

    return 0;
}

static void modify_cluster_ops(int cluster_size)
{
    const char *partition_name = "/mmcblk0p0";
    const char *device_name = "mmcblk0p0";
    int cluster = 0;

    int fd = open(partition_name, O_DIRECTORY, 0);
    assert(fd >= 0);

    ioctl(fd, DFS_GET_CLUSTER_SIZE_CMD, &cluster);

    close(fd);

    if (cluster == cluster_size)
        return;

    /* cluster_size 为 fat 文件系统的单元大小，会影响读写速度，需要根据分区大小和存储的文件大小决定，同时，其大 需要是 2 的幂次方*/
    file_system_remove_partition(device_name);
    dfs_mkfs("elm", device_name, cluster_size);
    file_system_insert_partition(device_name);

}

static int cluster_size_tab[] = {
    0,/* 由 tf 卡容量自己决定 */
    4096,
    8192,
    16384,
    32768,
    65536,
    131072,
    262144,
    524288,
    1048576,
    2097152,
    4194304,
};

static void test_tf_rate_thread_func(void *data)
{
    int i;
    int result;
    struct statfs buffer;
    char *partition_name = "/mmcblk0p0";

    printf("Start test TF RATE:\n");

    result = dfs_statfs(partition_name, &buffer);
    if (result == 0) {
        long long total_cap = ((long long)buffer.f_bsize) * ((long long)buffer.f_blocks) / 1024LL;
        printf("%s size = %lld KB\n\n", partition_name, total_cap);
    } else
        elm_printf("dfs_statfs failed.\n\n");


    for (i = 0; i < sizeof(cluster_size_tab) / sizeof(int); i++) {
        printf("Now set cluster to %d Byte\n", cluster_size_tab[i]);
        modify_cluster_ops(cluster_size_tab[i]);

        file_test_ops();
    }

    printf("TF Rate Test Successfully.\n");
    thread_delete(NULL);
}

void test_tf_rate_example_test(void)
{
#ifdef CONFIG_OS
    thread_create("tf test -thread", 8192, test_tf_rate_thread_func, NULL);
#else
#error "NO OS not support File System."
#endif
}
#endif
