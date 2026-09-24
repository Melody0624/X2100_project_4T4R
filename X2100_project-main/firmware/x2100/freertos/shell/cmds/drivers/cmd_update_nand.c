#include <shell.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <dfs_file.h>
#include <dfs.h>
#include <dfs_posix.h>
#include <driver/sfc_nand.h>

static void cmd_func_update_help(char *cmd)
{
    shell_printf("Usage: \t%s <upgrade file name path> <upgrade partition name>\n", cmd);
    shell_printf("Example:\n");
    shell_printf("\t%s zero.bin rtos\n", cmd);
}

static void cmd_func_update(struct cmd_arg *arg, int argc, char **argv)
{
    int ret;
    size_t file_len;//文件大小
    struct dfs_fd fd;
    const char *file_name = argv[1];
    char *part_name = argv[2];

    if (argc != 3) {
        cmd_func_update_help(argv[0]);
        return;
    }

    /* get storage information */
    const struct storage_info *info = sfc_nand_flash_info();

    /* 判断分区是否正确 */
    uint32_t offset = 0;
    uint32_t size = 0;
    if (get_nand_partition_information_by_name(part_name, &offset, &size)) {
        shell_printf("partition name not exist!\n");
        return;
    }
    shell_printf("offset: 0x%x size: 0x%x\n", offset, size);

    /* open file */
    if (dfs_file_open(&fd, file_name, O_RDONLY) < 0) {
        shell_printf("open file : %s is failure! \n", file_name);
        return;
    }
    file_len = fd.size;
    shell_printf("file len is : %d \n", file_len);

    /* 判断文件大小是否超出分区 */
    if (file_len > size) {
        shell_printf("file len(%d) id exceed partition size(%d)!\n", file_len, size);
        dfs_file_close(&fd);
        return;
    }

    /* 将文件写入对应的分区中 */
    uint32_t address = offset;
    uint32_t blk_size = info->erasesize;
    uint32_t length, sum = 0;
    uint8_t *buffer = malloc(blk_size);
    memset(buffer, 0, blk_size);

    while (sum < file_len) {
        length = dfs_file_read(&fd, buffer, blk_size);
        if (length <= 0) {
            shell_printf("file : %s read failed! \n", file_name);
            break;
        }

        /* 数据长度小于块大小，补齐数据至块大小 */
        if (length < blk_size)
            memset(buffer + length, 0xff, blk_size - length);

badblk_rewrite:

        /* 按块擦除 */
        ret = sfc_nand_flash_erase(address, blk_size);
        if (ret < 0)
            break;

        /* 按块写入 */
        ret = sfc_nand_flash_write_check_badblock(address, blk_size, buffer);
        if (ret == -1) {
            shell_printf("bad block :%x\n", address);
            address += blk_size;
            goto badblk_rewrite;
        } else if (ret == -EIO || address >= offset + size) {
            shell_printf("address exceed partition end or EIO!\n");
            break;
        }

        address += blk_size;
        sum += length;
        shell_printf("\r update %8dBytes ", sum);
        fflush(stdout);
    }

    printf("\n");
    fflush(stdout);
    printf("update ending...\n");

    free(buffer);
    /* 关闭文件 */
    dfs_file_close(&fd);

}

void cmd_update_nand_init(void)
{
    shell_cmd_register(cmd_func_update, "update_nand", NULL, "update partition");
}
