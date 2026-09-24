#include <shell.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <driver/sfc_nor.h>


static void cmd_func_nor_read_help(char *cmd)
{
    shell_printf("Usage:%s <ADDR> <SIZE>\n", cmd);
    shell_printf("\tRead flash, All Parameter format is Hex \n");
    shell_printf("Example:\n");
    shell_printf("\t%s 0x400000 0x2\n", cmd);
}

static void cmd_func_nor_write_help(char *cmd)
{
    shell_printf("Usage:%s <ADDR> <DATA...>\n", cmd);
    shell_printf("\tWrite flash, All Parameter format is Hex ; data unit : unsigned char\n");
    shell_printf("Example:\n");
    shell_printf("\t%s 0x400000 0x11 0x22\n", cmd);
}

void cmd_func_nor_read(struct cmd_arg *arg, int argc, char **argv)
{
    int i;
    int ret;
    u8 *buf;
    u32 len;
    u32 offset;
    u32 flash_size;
    const struct storage_info *info;

    info = sfc_nor_flash_info();
    flash_size = info -> chipsize;

    if (argc < 3)
        goto nor_read_out;

    ret = sscanf(argv[1], "%x", &offset);
    if (ret != 1)
         goto nor_read_out;

    ret = sscanf(argv[2], "%x", &len);
    if (ret != 1)
        goto nor_read_out;

    if ((offset + len > flash_size)) {
        shell_printf("read error : Memory out of bounds\n");
        goto nor_read_out;
    }

    buf = (u8 *)malloc(len * sizeof(u8));
    ret = sfc_nor_flash_read(offset, len, buf);
    if (ret < 0){
        shell_printf("read error \n");
        goto nor_read_out;
    }

    shell_printf("read_buf:\n");
    for (i = 0; i < len; i++) {
        shell_printf("0x%02x  ", buf[i]);
        if ((i + 1) % 10 == 0)
            shell_printf("\n");
    }
    shell_printf("\n");
    free(buf);
    return;

nor_read_out:
    cmd_func_nor_read_help(argv[0]);
}


void cmd_func_nor_write(struct cmd_arg *arg, int argc, char **argv)
{
    int i, temp, ret, len;
    u8 *backup_buf = NULL;
    u8 *write_buf = NULL;
    u32 offset;
    u32 flash_size;
    u32 erase_size;
    u32 setcor_address;
    u32 relative_address;
    const struct storage_info *info;

    if (argc < 3) {
        shell_printf("too little argc or too much data(max is  64)\n");
        goto nor_write_out;
    }

    info = sfc_nor_flash_info();
    flash_size = info->chipsize;
    erase_size = info->erasesize;

    len = argc - 2;
    write_buf = (u8*)malloc(len);
    if (write_buf == NULL) {
        shell_printf("malloc write_buf error!!\n");
        goto nor_write_out;
    }

    ret = sscanf(argv[1], "%x", &offset);
    if (ret != 1) {
        shell_printf("nor flash addr error!!\n");
        goto nor_write_out;
    }

    for (i = 0; i < len; i++) {
        ret = sscanf(argv[i + 2], "%x",&temp);
        if (ret != 1) {
            shell_printf("data format error\n ");
            goto nor_write_out;
        }
        write_buf[i] = (u8) temp;
    }

    if ((offset + len) > flash_size) {
        shell_printf("write error : Memory out of bounds\n");
    }

    backup_buf = (u8 *)malloc(erase_size);
    if (backup_buf == NULL) {
        shell_printf("malloc backup_buf error!!\n");
        goto nor_write_out;
    }

    setcor_address = (offset / erase_size) * erase_size;
    relative_address = offset % erase_size;
    temp = 0;

    do {
        ret = sfc_nor_flash_read(setcor_address, erase_size, backup_buf);
        if (ret < 0) {
            shell_printf("cmd nor read fail\n");
            goto nor_write_out;
        }

        ret = sfc_nor_flash_erase(setcor_address, erase_size);
        if (ret < 0) {
            shell_printf("cmd nor erase fail\n");
            goto nor_write_out;
        }

        for (i = 0; i < len; i++) {
            if (relative_address + i == erase_size)
                break;
            backup_buf[relative_address + i] = write_buf[i + temp];
        }
        temp += i;

        ret = sfc_nor_flash_write(setcor_address, erase_size, backup_buf);
        if (ret < 0) {
            shell_printf("cmd nor write fail\n");
            goto nor_write_out;
        }

        len -= i;
        setcor_address = setcor_address + erase_size;
        relative_address = 0;
    } while (len > 0);

    free(backup_buf);
    free(write_buf);
    return;

nor_write_out:
    if (backup_buf)
        free(backup_buf);
    if (write_buf)
        free(write_buf);
    cmd_func_nor_write_help(argv[0]);
}

void cmd_nor_init(void)
{
    shell_cmd_register(cmd_func_nor_read, "nor_read",    NULL, "nor flash read");
    shell_cmd_register(cmd_func_nor_write, "nor_write",    NULL, "nor flash write");
}