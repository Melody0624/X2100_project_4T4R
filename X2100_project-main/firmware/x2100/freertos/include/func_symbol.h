#ifndef _FUNC_SYMBOL_
#define _FUNC_SYMBOL_

#define __func_symbol_str   __attribute__((section(".func_symbol_str")))
#define __func_symbol_index   __attribute__((section(".func_symbol_index")))

struct func_symbol {
    unsigned long addr;
    const char *name;
};

struct func_symbol *func_symbol_lookup(unsigned long addr);

#endif /* _FUNC_SYMBOL_ */