#include <shell.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <driver/mmc_device.h>
#include <driver/cache.h>
#include <types.h>

static void cmd_func_mmc_read_help(char *cmd)
{
    shell_printf("Usage:%s <ADDR> <SIZE>\n", cmd);
    shell_printf("\tRead mmc, All Parameter format is Hex \n");
    shell_printf("Example:\n");
    shell_printf("\t%s 0x400000 0x2\n", cmd);
}

static void cmd_func_mmc_write_help(char *cmd)
{
    shell_printf("Usage:%s <ADDR> <DATA...>\n", cmd);
    shell_printf("\tWrite mmc, All Parameter format is Hex ; data unit : unsigned char\n");
    shell_printf("Example:\n");
    shell_printf("\t%s 0x400000 0x11 0x22\n", cmd);
}

void cmd_func_mmc_read(struct cmd_arg *arg, int argc, char **argv)
{
    int i;
    uint8_t *buf = NULL;
    uint8_t *read_buf = NULL;
    int ret;
    uint32_t len;
    uint32_t count;
    uint64_t offset;
    uint64_t chip_size;
    uint32_t erasesize;
    const struct storage_info *info;

    int temp = 0;
    uint32_t blk_start;
    uint32_t extra_size;

    /* get storage information */
    info = mmc_device_storage_info();
    chip_size = info->chipsize;
    erasesize = info->erasesize;

    if (argc < 3)
        goto mmc_read_out;

    ret = sscanf(argv[1], "%llx", &offset);
    if (ret != 1)
         goto mmc_read_out;

    ret = sscanf(argv[2], "%x", &len);
    if (ret != 1)
        goto mmc_read_out;

    if ((offset + len > chip_size)) {
        shell_printf("read error : Memory out of bounds\n");
        goto mmc_read_out;
    }

    buf = (uint8_t *)cache_align_malloc(erasesize * sizeof(uint8_t));
    memset(buf, 0xff, erasesize);
    read_buf = (uint8_t *)malloc(len);

    count = len;
    extra_size = (offset & (erasesize - 1));
    blk_start = (offset & ~(erasesize - 1));

    do {
        ret = mmc_device_block_read(blk_start, erasesize, buf);
        if (ret < 0){
            shell_printf("read error \n");
            goto mmc_read_out;
        }

        for (i = 0; i < count; i++) {
            if (extra_size + i == erasesize)
                break;
            read_buf[i + temp] = buf[extra_size + i];
        }

        temp += i;
        count -= i;
        blk_start += erasesize;
        extra_size = 0;
    } while (count > 0);


    shell_printf("read_buf:\n");
    for (i = 0; i < len; i++) {
        shell_printf("0x%02x  ", read_buf[i]);
        if ((i + 1) % 10 == 0)
            shell_printf("\n");
    }
    shell_printf("\n");
    free(buf);
    return;

mmc_read_out:
    cmd_func_mmc_read_help(argv[0]);
}

void cmd_func_mmc_write(struct cmd_arg *arg, int argc, char **argv)
{
    int i, temp, ret, len;
    uint8_t *backup_buf = NULL;
    uint8_t *write_buf = NULL;
    uint64_t offset;
    uint64_t chip_size;
    uint32_t erase_size;
    uint32_t blk_start;
    uint32_t extra_size;
    const struct storage_info *info;

    if (argc < 3) {
        shell_printf("too little argc or too much data(max is  64)\n");
        goto mmc_write_out;
    }

    info = mmc_device_storage_info();
    chip_size = info->chipsize;
    erase_size = info->erasesize;

    len = argc - 2;
    write_buf = (uint8_t*)malloc(len);
    if (write_buf == NULL) {
        shell_printf("malloc write_buf error!!\n");
        goto mmc_write_out;
    }

    ret = sscanf(argv[1], "%llx", &offset);
    if (ret != 1) {
        shell_printf("mmc flash addr error!!\n");
        goto mmc_write_out;
    }

    for (i = 0; i < len; i++) {
        ret = sscanf(argv[i + 2], "%x",&temp);
        if (ret != 1) {
            shell_printf("data format error\n ");
            goto mmc_write_out;
        }
        write_buf[i] = (uint8_t) temp;
    }

    if ((offset + len) > chip_size) {
        shell_printf("write error : Memory out of bounds\n");
    }

    backup_buf = (uint8_t *)cache_align_malloc(erase_size);
    if (backup_buf == NULL) {
        shell_printf("malloc backup_buf error!!\n");
        goto mmc_write_out;
    }
    memset(backup_buf, 0xff, erase_size);

    extra_size = offset & (erase_size - 1);
    blk_start = (offset & ~(erase_size - 1));
    temp = 0;

    do {

        ret = mmc_device_block_read(blk_start, erase_size, backup_buf);
        if (ret < 0) {
            shell_printf("cmd mmc read fail\n");
            goto mmc_write_out;
        }

        ret = mmc_device_block_erase(blk_start, erase_size);
        if (ret < 0) {
            shell_printf("cmd mmc erase fail\n");
            goto mmc_write_out;
        }

        for (i = 0; i < len; i++) {
            if (extra_size + i == erase_size)
                break;
            backup_buf[extra_size + i] = write_buf[i + temp];
        }

        ret = mmc_device_block_write(blk_start, erase_size, backup_buf);
        if (ret < 0 || (blk_start + erase_size) >= chip_size ) {
            shell_printf("address exceed partition end or EIO!\n");
            break;
        }

        temp += i;
        len -= i;
        blk_start += erase_size;
        extra_size = 0;
    } while (len > 0);

    free(backup_buf);
    free(write_buf);
    return;

mmc_write_out:
    if (backup_buf)
        free(backup_buf);
    if (write_buf)
        free(write_buf);
    cmd_func_mmc_write_help(argv[0]);
}

void cmd_mmc_init(void)
{
    shell_cmd_register(cmd_func_mmc_read, "mmc_read",    NULL, "mmc read");
    shell_cmd_register(cmd_func_mmc_write, "mmc_write",    NULL, "mmc write");
}