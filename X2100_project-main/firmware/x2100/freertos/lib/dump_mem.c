#include <common.h>

static void dump_mem(void *addr, int size, int line_count, int step, int c_style)
{
    void *p = addr;
    int n = size / step;
    int i, j;
    char fmt_buf[16];
    if (c_style) strcpy(fmt_buf, " 0x");
    else         strcpy(fmt_buf, " ");
    if (step == 1) strcat(fmt_buf, "%02x");
    if (step == 2) strcat(fmt_buf, "%04x");
    if (step == 4) strcat(fmt_buf, "%08x");
    if (c_style) strcat(fmt_buf, ",");

    for (i = 0; i < n;) {
        if (!c_style) printf("%p:", p + i);
        for (j = 0; j < line_count; j++, i++) {
            if (i >= n)
                break;
            unsigned int value = 0;
            if (step == 1) value = *(unsigned char *)p;
            if (step == 2) value = *(unsigned short *)p;
            if (step == 4) value = *(unsigned int *)p;
            printf(fmt_buf, value);
            p += step;
        }
        printf("\n");
    }
}

void dump_mem8(void *addr, int size, int line_count)
{
    dump_mem(addr, size, line_count, 1, 0);
}

void dump_mem8_c_style(void *addr, int size, int line_count)
{
    dump_mem(addr, size, line_count, 1, 1);
}

void dump_mem16(void *addr, int size, int line_count)
{
    dump_mem(addr, size, line_count, 2, 0);
}

void dump_mem16_c_style(void *addr, int size, int line_count)
{
    dump_mem(addr, size, line_count, 2, 1);
}

void dump_mem32(void *addr, int size, int line_count)
{
    dump_mem(addr, size, line_count, 4, 0);
}

void dump_mem32_c_style(void *addr, int size, int line_count)
{
    dump_mem(addr, size, line_count, 4, 1);
}
