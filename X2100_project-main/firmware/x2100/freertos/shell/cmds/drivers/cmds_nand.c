#include <shell.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <driver/sfc_nand.h>
#include <driver/cache.h>
#include <types.h>

static void cmd_func_nand_read_help(char *cmd)
{
    shell_printf("Usage:%s <ADDR> <SIZE>\n", cmd);
    shell_printf("\tRead flash, All Parameter format is Hex \n");
    shell_printf("Example:\n");
    shell_printf("\t%s 0x400000 0x2\n", cmd);
}

static void cmd_func_nand_write_help(char *cmd)
{
    shell_printf("Usage:%s <ADDR> <DATA...>\n", cmd);
    shell_printf("\tWrite flash, All Parameter format is Hex ; data unit : unsigned char\n");
    shell_printf("Example:\n");
    shell_printf("\t%s 0x400000 0x11 0x22\n", cmd);
}

void cmd_func_nand_read(struct cmd_arg *arg, int argc, char **argv)
{
    int i;
    u8 *buf = NULL;
    u8 *read_buf = NULL;
    int ret;
    u32 len;
    u32 count;
    u32 offset;
    u32 flash_size;
    u32 erasesize;
    const struct storage_info *info;

    int temp = 0;
    u32 blk_start;
    u32 extra_size;

    info = sfc_nand_flash_info();
    flash_size = info->chipsize;
    erasesize = info->erasesize;

    if (argc < 3)
        goto nand_read_out;

    ret = sscanf(argv[1], "%x", &offset);
    if (ret != 1)
         goto nand_read_out;

    ret = sscanf(argv[2], "%x", &len);
    if (ret != 1)
        goto nand_read_out;

    if ((offset + len > flash_size)) {
        shell_printf("read error : Memory out of bounds\n");
        goto nand_read_out;
    }

    buf = (u8 *)cache_align_malloc(erasesize * sizeof(u8));
    memset(buf, 0xff, erasesize);
    read_buf = (u8 *)malloc(len);

    count = len;
    extra_size = (offset & (erasesize - 1));
    blk_start = (offset & ~(erasesize - 1));

    do {
        ret = sfc_nand_flash_read_check_badblock(&blk_start, erasesize, buf);
        if (ret < 0){
            shell_printf("read error \n");
            goto nand_read_out;
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

nand_read_out:
    cmd_func_nand_read_help(argv[0]);
}

void cmd_func_nand_write(struct cmd_arg *arg, int argc, char **argv)
{
    int i, temp, ret, len;
    u8 *backup_buf = NULL;
    u8 *write_buf = NULL;
    u32 offset;
    u32 flash_size;
    u32 erase_size;
    u32 blk_start;
    u32 extra_size;
    const struct storage_info *info;

    if (argc < 3) {
        shell_printf("too little argc or too much data(max is  64)\n");
        goto nand_write_out;
    }

    info = sfc_nand_flash_info();
    flash_size = info->chipsize;
    erase_size = info->erasesize;

    len = argc - 2;
    write_buf = (u8*)malloc(len);
    if (write_buf == NULL) {
        shell_printf("malloc write_buf error!!\n");
        goto nand_write_out;
    }

    ret = sscanf(argv[1], "%x", &offset);
    if (ret != 1) {
        shell_printf("nand flash addr error!!\n");
        goto nand_write_out;
    }

    for (i = 0; i < len; i++) {
        ret = sscanf(argv[i + 2], "%x",&temp);
        if (ret != 1) {
            shell_printf("data format error\n ");
            goto nand_write_out;
        }
        write_buf[i] = (u8) temp;
    }

    if ((offset + len) > flash_size) {
        shell_printf("write error : Memory out of bounds\n");
    }

    backup_buf = (u8 *)cache_align_malloc(erase_size);
    if (backup_buf == NULL) {
        shell_printf("malloc backup_buf error!!\n");
        goto nand_write_out;
    }
    memset(backup_buf, 0xff, erase_size);

    extra_size = offset & (erase_size - 1);
    blk_start = (offset & ~(erase_size - 1));
    temp = 0;

    do {
badblk_rewrite:

        ret = sfc_nand_flash_read_check_badblock(&blk_start, erase_size, backup_buf);
        if (ret < 0) {
            shell_printf("cmd nand read fail\n");
            goto nand_write_out;
        }

        ret = sfc_nand_flash_erase(blk_start, erase_size);
        if (ret < 0) {
            shell_printf("cmd nand erase fail\n");
            goto nand_write_out;
        }

        for (i = 0; i < len; i++) {
            if (extra_size + i == erase_size)
                break;
            backup_buf[extra_size + i] = write_buf[i + temp];
        }

        ret = sfc_nand_flash_write_check_badblock(blk_start, erase_size, backup_buf);
        if (ret == -1) {
            shell_printf("bad block :%x\n", blk_start);
            blk_start += erase_size;
            goto badblk_rewrite;
        } else if (ret == -EIO || (blk_start + erase_size) >= flash_size) {
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

nand_write_out:
    if (backup_buf)
        free(backup_buf);
    if (write_buf)
        free(write_buf);
    cmd_func_nand_write_help(argv[0]);
}

void cmd_nand_init(void)
{
    shell_cmd_register(cmd_func_nand_read, "nand_read",    NULL, "nand flash read");
    shell_cmd_register(cmd_func_nand_write, "nand_write",    NULL, "nand flash write");
}