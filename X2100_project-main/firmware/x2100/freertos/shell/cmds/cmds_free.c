#include <string.h>
#include <shell.h>
#include <lds_symbol.h>

extern char *heap_ptr;

void cmd_func_free(struct cmd_arg *arg, int argc, char **argv)
{
    unsigned long total = (char *)&_user_heap_end - (char *)&_user_heap_start;
    unsigned long used = heap_ptr - (char *)&_user_heap_start;
    unsigned long free = (char *)&_user_heap_end - heap_ptr;

    shell_printf("total        used        free\n");

    shell_printf("%-12ld %-11ld %ld\n", total, used, free);
    return;
}



