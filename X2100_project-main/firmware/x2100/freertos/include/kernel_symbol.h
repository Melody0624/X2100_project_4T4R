#ifndef _KERNEL_SYMBOL_H_
#define _KERNEL_SYMBOL_H_

#define __EXPORT_SYMBOL(sym, sec)                             \
    extern typeof(sym) sym;                                   \
    static const char __kstrtab_##sym[]                       \
    __attribute__((section("__ksymtab_strings"), aligned(1))) \
    = #sym;                                                   \
    static const struct kernel_symbol __ksymtab_##sym         \
    __attribute__((used))                                 \
    __attribute__((section("___ksymtab" sec "+" #sym), unused)) \
    = { (unsigned long)&sym, __kstrtab_##sym }

#ifdef CONFIG_OS_MODULE
#define EXPORT_SYMBOL(sym)                    \
    __EXPORT_SYMBOL(sym, "")
#else
#define EXPORT_SYMBOL(sym)
#endif

struct kernel_symbol
{
    unsigned long value;
    const char *name;
};

#endif /* _KERNEL_SYMBOL_H_ */
