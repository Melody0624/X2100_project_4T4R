#ifndef _MODULE_H_
#define _MODULE_H_

#include <list.h>
#include <elf.h>
#include <kernel_symbol.h>
#include <kernel_param.h>
#include <cpu/module.h>

#define MODULE_NAME_LEN 64

#define ELF_ST_BIND(x)        ((x) >> 4)
#define ELF_ST_TYPE(x)        (((unsigned int) x) & 0xf)

typedef int (*initcall_t)(void);
typedef void (*exitcall_t)(void);

/* Each module must use one module_init(). */
#define module_init(initfn)                    \
    static inline initcall_t __inittest(void)  \
    { return initfn; }                         \
    int init_module(void) __attribute__((alias(#initfn)));

/* This is only required if you want to be unloadable. */
#define module_exit(exitfn)                    \
    static inline exitcall_t __exittest(void)  \
    { return exitfn; }                         \
    void cleanup_module(void) __attribute__((alias(#exitfn)));

struct module_data {
    void *data;
    unsigned int size;
    unsigned int text_size;
    unsigned int ro_size;
    Elf_Sym *symtab;
    unsigned int num_syms;
    char *strtab;
};

struct module {
    /* 模块名,不可重复 */
    char name[MODULE_NAME_LEN];

    /* 是否为独立的模块 */
    unsigned int is_independent:1;

    /* 子模块表 */
    struct list_head list;

    /* 链接到父模块 */
    struct list_head link;

    /* 模块的核心数据 */
    struct module_data core;

    /* 导出的符号 */
    const struct kernel_symbol *syms;
    unsigned int num_syms;

    /* 模块参数 */
    struct kernel_param *kp;
    unsigned int num_kp;

    /* 平台定义的数据 */
    struct mod_arch_specific arch;

    /* 模块入口函数 */
    int (*init)(void);

    /* 模块退出函数 */
    void (*exit)(void);
};

extern struct module this_module;

struct module *load_module(void *data, unsigned int size, int flags);

void unload_module(struct module *mod);

struct kernel_symbol *find_symbol_in_array(const struct kernel_symbol *syms, unsigned int nums, const char *name);

struct kernel_symbol *find_symbol(const char *name);

struct kernel_symbol *find_symbol_in_module(struct module *mod, const char *name);

struct kernel_symbol *find_symbol_in_kernel(const char *name);

void kernel_module_init(void);

int is_in_module(unsigned long addr);

struct list_head *get_kernel_module_list(void);

const char *module_address_lookup(unsigned long addr,unsigned long *size,
        unsigned long *offset, char **modname);

#endif /* _MODULE_H_ */
