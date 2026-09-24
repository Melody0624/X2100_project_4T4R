#include <stdio.h>
#include <stdlib.h>
#include <asm/addrspace.h>
#include <sys/errno.h>
#include <shell.h>

static void cmd_func_devmem_help(char *cmd)
{
    shell_printf("\nUsage:\t%s { address } [ width [ data ] ]\n", cmd);
    shell_printf("Read/write from physical address\n");
    shell_printf("\taddress : memory address to act upon\n");
    shell_printf("\twidth   : Width (8/16/32/...)\n");
    shell_printf("\tdata    : data to be written\n");
}

int get_unaligned_4bytes(unsigned int *virt_addr, unsigned int read_result)
{
    /*little Endian*/
    __asm__ __volatile__ (
        "1:\tlwr\t%0, 0(%1)\n"         \
        "2:\tlwl\t%0, 3(%1)\n\t"       \
        : "=&r" (read_result)          \
        : "r" (virt_addr)              \
    );
    return read_result;
}

void set_varlength_4bytes(unsigned int *virt_addr, unsigned int writeval, int align_offset, int width)
{
    unsigned int *op_addr = (unsigned int *)((unsigned int)virt_addr & ~0x07);
    unsigned int num = *op_addr;      /* 待被替换的数据 */
    unsigned int replace = writeval;  /* 待替换数据 */
    int pos  = align_offset * 8;     /* 待写入的起始位置 */

    // 将待替换的16位数清零
    if (width == 16)
        num &= ~(0xFFFF << pos);
    if (width == 8)
        num &= ~(0xFF << pos);

    // 将替换数据左移至对应位置
    replace <<= pos;

    // 替换
    num |= replace;

    // 写入
    *op_addr = num;
}


void cmd_func_devmem(struct cmd_arg *arg, int argc, char **argv)
{
    unsigned int *virt_addr;
    unsigned int physic_addr;

    int width = 8 * sizeof(int);    //default width is 32 bits.
    int align_offset;
    unsigned int read_result;
    unsigned int writeval;

    if (argc < 2) {
        goto param_err;
    }

    /*virtual address*/
    physic_addr = strtoul(argv[1], 0, 0);
    if (physic_addr > 0x1ffffffc) {
        shell_printf("Over 512MB! maximun accessible address is 0x1ffffffc\n");
        shell_printf("please input correct address\n");
        return;
    }
    virt_addr = (unsigned int *)(KSEG1ADDR(physic_addr));

    /* WIDTH */
    if (argc >= 3) {
        width = atoi(argv[2]);
    }

    align_offset = physic_addr % 4;
    if (argc <= 3) {
        switch (width) {
        case 8:
            if (align_offset)
                read_result = (uint8_t)get_unaligned_4bytes(virt_addr, read_result);
            else
                read_result = *(volatile uint8_t*)virt_addr;
            break;
        case 16:
            if (align_offset)
                read_result = (uint16_t)get_unaligned_4bytes(virt_addr, read_result);
            else
                read_result = *(volatile uint16_t*)virt_addr;
            break;
        case 32:
            if (align_offset)
                read_result = (uint32_t)get_unaligned_4bytes(virt_addr, read_result);
            else
                read_result = *(volatile uint32_t*)virt_addr;
            break;
        default:
            shell_printf("bad width\n");
            return;
        }
        shell_printf("0x%0*X\n", (width >> 2), read_result);
    } else {
        writeval = strtoul(argv[3], 0, 0);
        switch (width) {
        case 8:
            set_varlength_4bytes(virt_addr, writeval, align_offset, width);
            break;
        case 16:
            if (align_offset == 3)
                goto out_of_4byte_boundary;
            else
                set_varlength_4bytes(virt_addr, writeval, align_offset, width);
            break;
        case 32:
            if (align_offset)
                goto out_of_4byte_boundary;
            else
                *virt_addr = writeval;
            break;
        default:
            shell_printf("bad width");
            return;
        }
    }
    return;

param_err:
    cmd_func_devmem_help(argv[0]);
    return;

out_of_4byte_boundary:
    shell_printf("error: could't write out of 4bytes boundary.\n");
}