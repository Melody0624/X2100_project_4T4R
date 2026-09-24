#include <common.h>
#include <soc/base.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <asm/addrspace.h>
#include <spl_rtos_argument.h>
#include "spl_cmdargs_analysis.h"

struct mem_info_t {
    char tag[32];
    void *mem_start;
    unsigned int mem_size;
};

static const char *cmdline = NULL;

static const char *mem_name[] = {"rmem", "nmem", "rtos_size", "lcd_mem", "share_mem", "vpu_mem"};

#define MAX_RESERVE_PART_NUMS  (sizeof(mem_name)/sizeof(char *))

static struct mem_info_t mem_list[MAX_RESERVE_PART_NUMS] = {0};

static inline void *is_address_valid(unsigned long address)
{
    if (address > CKSEG0 && address < CKSEG2)
        return (void *)address;
    else
        return NULL;
}

static inline void *phys_to_virt(unsigned long phys)
{
    return (void *)(phys | 0x80000000);
}

void cmdargs_mem_info_anlysis(void *arg)
{
    struct spl_rtos_argument *spl_argument = (struct spl_rtos_argument *)arg;
    struct rtos_boot_os_args *os_args = NULL;
    void *data = NULL;
    if (is_address_valid((unsigned long)spl_argument))
        data = (void *)spl_argument->os_boot_args;

    if (is_address_valid((unsigned long)data)) {
        if (((struct rtos_boot_os_args *)data)->magic == 0x53475241)
            os_args = (struct rtos_boot_os_args *)data;
    }

    if (!os_args) {
        printf("spl argument boot is args is NULL\n");
        return;
    }

    cmdline = os_args->cmdargs;

    for (int i = 0; i < MAX_RESERVE_PART_NUMS; i++) {
        strcpy(mem_list[i].tag, mem_name[i]);

        mem_list[i].mem_start = NULL;
        mem_list[i].mem_size = -1;

        char token[32] = {0};
        snprintf(token, sizeof(token), "%s=", mem_name[i]);

        char *p = strstr(os_args->cmdargs, token);
        if (p) {
            p += strlen(token);

            char *end;
            mem_list[i].mem_size = strtoul(p, &end, 10);

            if (*end == 'M') {
                mem_list[i].mem_size *= 1024 * 1024;
                end++;
            } else if (*end == 'K' ) {
                mem_list[i].mem_size *= 1024;
                end++;
            }

            if (*end != '@') {
                fprintf(stderr, "Mem start addr could not found! please check out your cmdline!\n");
                break;
            }

            end++;

            mem_list[i].mem_start = (void *)phys_to_virt(strtoul(end, NULL, 16));
        }
    }
}

int cmdargs_mem_info_get(const char *mem_str, void **start, unsigned int *size)
{
    if (!mem_str || !start || !size) {
        fprintf(stderr, "invlid parameter!\n");
        return -1;
    }

    int ret;

    for (int i = 0; i < MAX_RESERVE_PART_NUMS; i++) {
        ret = strcmp(mem_str, mem_list[i].tag);
        if (ret == 0) {
            if (mem_list[i].mem_start == NULL || mem_list[i].mem_size == -1) {
                fprintf(stderr, "Did not reserve this mem part, please check your cmdline!\n");
                return -1;
            }

            *start = mem_list[i].mem_start;
            *size = mem_list[i].mem_size;
            return 0;
        }
    }

    fprintf(stderr, "Do not support this mem type or check your mem_name!\n");
    return -1;
}

const char *cmdargs_get(void)
{
    if (cmdline == NULL) {
        fprintf(stderr, "Command line not initialized!\n");
        return NULL;
    }

    return cmdline;
}