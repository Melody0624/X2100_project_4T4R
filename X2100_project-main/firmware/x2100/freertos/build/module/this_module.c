#include <module.h>

// MODULE_INFO(vermagic, VERMAGIC_STRING);

/*
 * THIS_MODULE_NAME 必须定义
 */
#ifdef THIS_MODULE_NAME

extern int init_module(void);
extern void cleanup_module(void);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
    .name = THIS_MODULE_NAME,
    .init = init_module,
    .exit = cleanup_module,
};
#endif

#ifdef THIS_MODULE_DEPENDS
static const char __module_depends[]
__attribute__((section(".modinfo"), unused)) =
THIS_MODULE_DEPENDS;
#endif
