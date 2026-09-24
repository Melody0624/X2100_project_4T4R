#include <common.h>
#include <func_symbol.h>
#include <lds_symbol.h>

struct func_symbol *func_symbol_lookup(unsigned long addr)
{
    int i;
    int N = ((char *) &__func_symbol_index_end - (char *) &__func_symbol_index_start) / sizeof(struct func_symbol);
    struct func_symbol *s = (void *) &__func_symbol_index_start;

    for (i = N - 1; i >= 0 ;i--) {
        if (s[i].addr <= addr)
            return &s[i];
    }

    return NULL;
}

#include "func_symbols.txt"
