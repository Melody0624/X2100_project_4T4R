#ifndef _KERNEL_PARAM_H_
#define _KERNEL_PARAM_H_

#include <types.h>
#include <stdio.h>

struct kernel_param;

struct kernel_param_ops {
    /* Returns 0, or -errno.  arg is in kp->arg. */
    int (*set)(const char *val, const struct kernel_param *kp);
    /* Returns length written or -errno.  Buffer is 4k (ie. be short!) */
    int (*get)(char *buffer, const struct kernel_param *kp);
    /* Optional function to free kp->arg when module unloaded. */
    void (*free)(void *arg);
};

struct kernel_param {
    const char *name;
    const struct kernel_param_ops *ops;
    u16 perm;
    s16 level;
    size_t count;
    off_t *ppos;
    union {
        void *arg;
        const struct kparam_string *str;
        const struct kparam_array *arr;
    };
};

/* Special one for strings we want to copy into */
struct kparam_string {
    unsigned int maxlen;
    char *string;
};

/* Special one for arrays */
struct kparam_array
{
    unsigned int max;
    unsigned int elemsize;
    unsigned int *num;
    const struct kernel_param_ops *ops;
    void *elem;
};

extern int module_param_sysfs_init(void);

#endif /* _KERNEL_PARAM_H_ */
