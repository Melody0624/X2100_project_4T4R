#ifndef _MODULE_PARAM_H_
#define _MODULE_PARAM_H_

#include <types.h>
#include <stdio.h>
#include <kernel_param.h>

#define MODULE_PARAM_PREFIX             ""
#define __moduleparam_const             const

#define __param_check(name, p, type)                                    \
    static inline type *__check_##name(void) { return (type *)(p); }

extern struct kernel_param_ops param_ops_byte;
#define param_check_byte(name, p)           __param_check(name, p, unsigned char)

extern struct kernel_param_ops param_ops_short;
#define param_check_short(name, p)          __param_check(name, p, short)

extern struct kernel_param_ops param_ops_ushort;
#define param_check_ushort(name, p)         __param_check(name, p, unsigned short)

extern struct kernel_param_ops param_ops_int;
#define param_check_int(name, p)            __param_check(name, p, int)

extern struct kernel_param_ops param_ops_uint;
#define param_check_uint(name, p)           __param_check(name, p, unsigned int)

extern struct kernel_param_ops param_ops_long;
#define param_check_long(name, p)           __param_check(name, p, long)

extern struct kernel_param_ops param_ops_ulong;
#define param_check_ulong(name, p)          __param_check(name, p, unsigned long)

extern struct kernel_param_ops param_ops_charp;
#define param_check_charp(name, p)          __param_check(name, p, char *)

extern struct kernel_param_ops param_ops_string;
#define param_check_string(name, p)          __param_check(name, p, char *)

extern struct kernel_param_ops param_array_ops;


/*
 * module_param - typesafe helper for a module/cmdline parameter
 * @value: the variable to alter, and exposed parameter name.
 * @type: the type of the parameter
 * @perm: visibility in debugfs.
 *
 * @value becomes the module parameter, or (prefixed by KBUILD_MODNAME and a
 * ".") the kernel commandline parameter.  Note that - is changed to _, so
 * the user can use "foo-bar=1" even for variable "foo_bar".
 *
 * @perm is 0 if the the variable is not to appear in sysfs, or 0444
 * for world-readable, 0644 for root-writable, etc.  Note that if it
 * is writable, you may need to use kparam_block_sysfs_write() around
 * accesses (esp. charp, which can be kfreed when it changes).
 *
 * The @type is simply pasted to refer to a param_ops_##type and a
 * param_check_##type: for convenience many standard types are provided but
 * you can create your own by defining those variables.
 *
 * Standard types are:
 *  byte, short, ushort, int, uint, long, ulong
 *  charp: a character pointer
 *  bool: a bool, values 0/1, y/n, Y/N.
 *  invbool: the above, only sense-reversed (N = true).
 */
#define module_param(name, type, perm)                                  \
    module_param_named(name, name, type, perm)

/*
 * module_param_named - typesafe helper for a renamed module/cmdline parameter
 * @name: a valid C identifier which is the parameter name.
 * @value: the actual lvalue to alter.
 * @type: the type of the parameter
 * @perm: visibility in sysfs.
 *
 * Usually it's a good idea to have variable names and user-exposed names the
 * same, but that's harder if the variable must be non-static or is inside a
 * structure.  This allows exposure under a different name.
 */
#define module_param_named(name, value, type, perm)                     \
    param_check_##type(name, &(value));                                 \
    module_param_cb(name, &param_ops_##type, &value, perm);

/*
 * module_param_cb - general callback for a module/cmdline parameter
 * @name: a valid C identifier which is the parameter name.
 * @ops: the set & get operations for this parameter.
 * @perm: visibility in sysfs.
 *
 * The ops can have NULL set or get functions.
 */
#define module_param_cb(name, ops, arg, perm)                           \
    __module_param_call(MODULE_PARAM_PREFIX, name, ops, arg, perm, -1)

/* This is the fundamental function for registering boot/module
 * parameters.
 */
#define __module_param_call(prefix, name, ops, arg, perm, level)        \
    static const char __param_str_##name[] = prefix #name;              \
    static struct kernel_param __moduleparam_const __param_##name       \
    __used __attribute__ ((unused,                                      \
    __section__ ("__param"),aligned(sizeof(void *))))                   \
    = { __param_str_##name, ops, perm, level, 0, NULL, { arg } }


/*
 * String
 */
/*
 * module_param_string - a char array parameter
 * @name: the name of the parameter
 * @string: the string variable
 * @len: the maximum length of the string, incl. terminator
 * @perm: visibility in sysfs.
 *
 * This actually copies the string when it's set (unlike type charp).
 * @len is usually just sizeof(string).
 */
#define module_param_string(name, string, len, perm)                    \
    static const struct kparam_string __param_string_##name             \
        = { len, string };                                              \
    __module_param_call(MODULE_PARAM_PREFIX, name,                      \
                &param_ops_string,                                      \
                .str = &__param_string_##name, perm, -1);

/*
 * Array
 */
/*
 * module_param_array - a parameter which is an array of some type
 * @name: the name of the array variable
 * @type: the type, as per module_param()
 * @nump: optional pointer filled in with the number written
 * @perm: visibility in sysfs
 *
 * Input and output are as comma-separated values.  Commas inside values
 * don't work properly (eg. an array of charp).
 *
 * ARRAY_SIZE(@name) is used to determine the number of elements in the
 * array, so the definition must be visible.
 */
#define module_param_array(name, type, nump, perm)                      \
    module_param_array_named(name, name, type, nump, perm)

/**
 * module_param_array_named - renamed parameter which is an array of some type
 * @name: a valid C identifier which is the parameter name
 * @array: the name of the array variable
 * @type: the type, as per module_param()
 * @nump: optional pointer filled in with the number written
 * @perm: visibility in sysfs
 *
 * This exposes a different name than the actual variable name.  See
 * module_param_named() for why this might be necessary.
 */
#define module_param_array_named(name, array, type, nump, perm)         \
    param_check_##type(name, &(array)[0]);                              \
    static const struct kparam_array __param_arr_##name                 \
    = { .max = ARRAY_SIZE(array), .num = (unsigned int *)nump,          \
        .ops = &param_ops_##type,                                       \
        .elemsize = sizeof(array[0]), .elem = array };                  \
    __module_param_call(MODULE_PARAM_PREFIX, name,                      \
                &param_array_ops,                                       \
                .arr = &__param_arr_##name,                             \
                perm, -1);

#endif /* _MODULE_PARAM_H_ */

