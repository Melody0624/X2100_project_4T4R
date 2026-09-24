#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <shell.h>
#include <dfs.h>


static void cmd_func_memcpy_help(char *cmd)
{
    shell_printf("Usage:%s destaddr srcaddr size\n", cmd);
    shell_printf("Example:\n");
    shell_printf("\t%s  0x81001000 0x81000000 1024\n", cmd);
}

void cmd_func_memcpy(struct cmd_arg *arg, int argc, char **argv)
{
    int ret, len;
    unsigned int dest_add, src_add;

    if (argc != 4)
        goto memcpy_err;

    ret = sscanf(argv[1], "%x", &dest_add);
    if (ret != 1)
        goto memcpy_err;

    ret = sscanf(argv[2], "%x", &src_add);
    if (ret != 1)
        goto memcpy_err;

    ret = sscanf(argv[3], "%d", &len);
    if (ret != 1 || len < 0)
        goto memcpy_err;

    memcpy((void *)dest_add, (void *)src_add, len);

    shell_printf("\n");

    return;

memcpy_err:
    shell_printf("Wrong input parameters\n");
    cmd_func_memcpy_help(argv[0]);
}


static int get_unit(char *in)
{
    if (strcmp(in, "char") == 0)
        return 1;
    else if (strcmp(in, "short") == 0)
        return 2;
    else if (strcmp(in, "int") == 0)
        return 4;
    else
        return 0;
}


static void cmd_func_memclear_help(char *cmd)
{
    shell_printf("Usage:%s unit startaddr value size\n", cmd);
    shell_printf("\tvalue unit: hex\n");
    shell_printf("Example:\n");
    shell_printf("\t%s char 0x81000000 0xFF 1024\n", cmd);
    shell_printf("\t%s short 0x81000000 0xFFFF 1024\n", cmd);
    shell_printf("\t%s int 0x81000000 0xFFFFFFFF 1024\n", cmd);
}

void cmd_func_memclear(struct cmd_arg *arg, int argc, char **argv)
{
    int ret, i;
    int unit, len;
    unsigned int value;
    unsigned int addr;
    volatile unsigned int *p_int;
    volatile unsigned short *p_short;

    if (argc != 5)
        goto memclear_err;

    unit = get_unit(argv[1]);
    ret = sscanf(argv[2], "%x", &addr);
    if (ret != 1 || !unit)
        goto memclear_err;

    ret = sscanf(argv[3], "%x", &value);
    if (ret != 1)
        goto memclear_err;

    ret = sscanf(argv[4], "%d", &len);
    if (ret != 1 || len <= 0)
        goto memclear_err;

    switch(unit) {
        case 1:
            memset((void *)addr, value, len);
            break;
        case 2:
            p_short = (unsigned short *)addr;
            for (i = 0; i < len; i++, p_short++)
                *p_short = value;
            break;
        case 4:
            p_int = (unsigned int *)addr;
            for (i = 0; i < len; i++, p_int++)
                *p_int = value;
            break;
    }

    shell_printf("\n");

    return;

memclear_err:
    shell_printf("Wrong input parameters\n");
    cmd_func_memclear_help(argv[0]);
}



static void cmd_func_memset_help(char *cmd)
{
    shell_printf("Usage:%s unit startaddr data0 [data...]\n", cmd);
    shell_printf("\tdata unit: hex\n");
    shell_printf("Example:\n");
    shell_printf("\t%s char 0x81000000 0x11 0x22\n", cmd);
    shell_printf("\t%s short 0x81000000 0x1111 0x2222\n", cmd);
    shell_printf("\t%s int 0x81000000 0x11111111 0x22222222\n", cmd);
}

void cmd_func_memset(struct cmd_arg *arg, int argc, char **argv)
{
    int i;
    int unit, ret;
    unsigned int addr;
    unsigned int value;
    volatile unsigned int *p_int;
    volatile unsigned char *p_char;
    volatile unsigned short *p_short;

    if(argc < 4)
        goto memset_err;

    unit = get_unit(argv[1]);
    ret = sscanf(argv[2], "%x", &addr);
    if (ret != 1 || !unit)
        goto memset_err;

    switch(unit) {
       case 1:
            p_char = (unsigned char *) addr;
            for (i = 3; i < argc; i++) {
                ret = sscanf(argv[i], "%x", &value);
                if (ret != 1)
                    goto memset_err;

                *p_char = value;
                p_char++;
            }
            break;
        case 2:
            p_short = (unsigned short *) addr;
            for (i = 3; i < argc; i++) {
                ret = sscanf(argv[i], "%x", &value);
                if (ret != 1)
                    goto memset_err;
                *p_short = value;
                p_short++;
            }
            break;
        case 4:
            p_int = (unsigned int *) addr;
            for (i = 3; i < argc; i++) {
                ret = sscanf(argv[i], "%x", &value);
                if (ret != 1)
                    goto memset_err;
                *p_int = value;
                p_int++;
            }
            break;
    }

    shell_printf("\n");

    return;

memset_err:
    shell_printf("Wrong input parameters\n");
    cmd_func_memset_help(argv[0]);
}

static void cmd_func_memdump_help(char *cmd)
{
    shell_printf("Usage:%s unit startaddr size\n", cmd);
    shell_printf("Example:\n");
    shell_printf("\t%s char 0x81000000 1024\n", cmd);
    shell_printf("\t%s short 0x81000000 1024\n", cmd);
    shell_printf("\t%s int 0x81000000 1024\n", cmd);
}

void cmd_func_memdump(struct cmd_arg *arg, int argc, char **argv)
{
    int ret, i;
    int unit, len;
    unsigned int addr;
    unsigned int *p_int;
    unsigned char *p_char;
    unsigned short *p_short;


    if (argc != 4)
        goto memdump_err;

    unit = get_unit(argv[1]);
    ret = sscanf(argv[2], "%x", &addr);
    if (ret != 1 || !unit)
        goto memdump_err;

    ret = sscanf(argv[3], "%d", &len);
    if (ret != 1 || len <= 0)
        goto memdump_err;

    switch(unit) {
        case 1:
            p_char = (unsigned char *)addr;
            for (i = 0; i < len; i++) {
                if (i % 10 == 0)
                    shell_printf("\n");
                shell_printf("0x%02x ", *p_char++);
            }
            break;
        case 2:
            p_short = (unsigned short *)addr;
            for (i = 0; i < len; i++) {
                if (i % 10 == 0)
                    shell_printf("\n");
                shell_printf("0x%04x ", *p_short++);
            }
            break;
        case 4:
            p_int = (unsigned int *)addr;
            for (i = 0; i < len; i++) {
                if (i % 10 == 0)
                    shell_printf("\n");
                shell_printf("0x%08x ", *p_int++);
            }
            break;
    }

    shell_printf("\n");

    return;

memdump_err:
    shell_printf("Wrong input parameters\n");
    cmd_func_memdump_help(argv[0]);
}

