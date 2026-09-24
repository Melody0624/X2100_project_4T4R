#include <dfs.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <dfs_posix.h>
#include <dfs_debugfs.h>

/*
 * DebugFS Test
 */
static struct inode *debugfs_root;
static char hello[32] = "Hello world!\n";
static u8 a = 0;
static u32 type_bool = 0;

static int file_c_open(struct inode *inode, struct dfs_fd *file)
{
    return 0;
}

static ssize_t file_c_read(struct dfs_fd *file, char *buffer, size_t count, off_t *ppos)
{
    if (*ppos >= 32)
        return 0;

    if (*ppos + count > 32)
        count = 32 - *ppos;

    memcpy(buffer, hello + *ppos, count);

    *ppos += count;

    return count;
}

static ssize_t file_c_write(struct dfs_fd *file, const char *buffer, size_t count, off_t *ppos)
{
    if (*ppos >= 32)
        return 0;

    if (*ppos + count > 32)
        count = 32 - *ppos;

    if (*ppos == 0) {
        memset(hello, 0x00, 32);
    }

    memcpy(hello + *ppos, buffer, count);

    *ppos += count;

    return count;
}

static struct file_operations file_c_fops = {
    .open       = file_c_open,
    .read       = file_c_read,
    .write      = file_c_write,
};


int debugfs_directory_test(void)
{
    struct inode *sub_dir1;
    struct inode *sub_dir2;
    struct inode *sub_dir3;

    struct inode *three_dir1;
    struct inode *three_dir2;
    struct inode *three_dir3;

    struct inode *four_dir;

    /* create root mount point */
    debugfs_root = dfs_debugfs_create_dir("debugfs-example", NULL);
    if (!debugfs_root) {
        printf("DebugFS:Create debugs root failed\n");
        return -ENOENT;
    }

    /* create directory */
    sub_dir1 = dfs_debugfs_create_dir("subdir1", debugfs_root);
    if (!sub_dir1) {
        printf("DebugFS:Create subdir(subdir1) failed\n");
        return -ENOENT;
    }

    sub_dir2 = dfs_debugfs_create_dir("subdir2", debugfs_root);
    if (!sub_dir2) {
        printf("DebugFS:Create subdir(subdir2) failed\n");
        return -ENOENT;
    }

    sub_dir3 = dfs_debugfs_create_dir("subdir3", debugfs_root);
    if (!sub_dir3) {
        printf("DebugFS:Create subdir(subdir) failed\n");
        return -ENOENT;
    }

    three_dir1 = dfs_debugfs_create_dir("three_dir1", sub_dir2);
    if (!three_dir1) {
        printf("DebugFS:Create subdir(three_dir1) failed\n");
        return -ENOENT;
    }

    three_dir2 = dfs_debugfs_create_dir("three_dir2", sub_dir2);
    if (!three_dir2) {
        printf("DebugFS:Create subdir(three_dir2) failed\n");
        return -ENOENT;
    }

    three_dir3 = dfs_debugfs_create_dir("three_dir3", sub_dir2);
    if (!three_dir3) {
        printf("DebugFS:Create subdir(three_dir3) failed\n");
        return -ENOENT;
    }

    four_dir = dfs_debugfs_create_dir("four_dir", three_dir2);
    if (!four_dir) {
        printf("DebugFS:Create subdir(four_dir) failed\n");
        return -ENOENT;
    }


    /* create file */
    struct inode *file_c;

    file_c = dfs_debugfs_create_file("c", 0644, four_dir, NULL, &file_c_fops);
    if (!file_c) {
        printf("DebugFS:Create file(c) failed\n");
        return -ENOENT;
    }

    struct inode *int_a;

    int_a = dfs_debugfs_create_u8("u8", 0644, four_dir, &a);
    if (!int_a) {
        printf("DebugFS:Create int(a) failed\n");
        return -ENOENT;
    }

    struct inode *inode_bool;

    inode_bool = dfs_debugfs_create_bool("bool", 0644, four_dir, &type_bool);
    if (!inode_bool) {
        printf("DebugFS:Create Bool(bool) failed\n");
        return -ENOENT;
    }

    struct inode *inode_array;
    static u32 array[] = {1,2,3,4,5,6,7};
    inode_array = dfs_debugfs_create_u32_array("array", 0x644, four_dir, array, sizeof(array)/sizeof(array[0]));
    if (!inode_array) {
        printf("DebugFS:Create Array(U32) failed\n");
        return -ENOENT;
    }

    printf("=====Dump Tree======\n");
    dfs_debugfs_dump_tree(debugfs_root);

    //printf("=====Remove Node======\n");
    //dfs_debugfs_remove_recursive(sub_dir3);

    return 0;
}
