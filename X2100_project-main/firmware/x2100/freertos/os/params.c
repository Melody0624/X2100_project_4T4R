#include <module.h>
#include <err_ptr.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <asm/elf.h>
#include <common.h>
#include <driver/cache.h>
#include <lds_symbol.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dfs_debugfs.h>

extern size_t strcspn (const char *, const char *);

#define STANDARD_PARAM_DEF(name, type, format, tmptype, strtolfn)           \
    int param_set_##name(const char *val, const struct kernel_param *kp)    \
    {                                                                       \
        tmptype l;                                                          \
        char * endptr;                                                      \
                                                                            \
        l = strtolfn(val, &endptr,0);                                       \
        if (((type)l != l))                                                 \
            return -EINVAL;                                                 \
                                                                            \
        *((type *)kp->arg) = l;                                             \
        if (kp->ppos)                                                       \
            *kp->ppos += sizeof(tmptype);                                   \
        return 0;                                                           \
    }                                                                       \
    int param_get_##name(char *buffer, const struct kernel_param *kp)       \
    {                                                                       \
        int ret = 0;                                                        \
        if (kp->ppos && !*kp->ppos) {                                       \
            ret = sprintf(buffer, format, *((type *)kp->arg));              \
            *kp->ppos += ret;                                               \
        }                                                                   \
        return ret;                                                         \
    }                                                                       \
    struct kernel_param_ops param_ops_##name = {                            \
        .set = param_set_##name,                                            \
        .get = param_get_##name,                                            \
    };                                                                      \
    EXPORT_SYMBOL(param_set_##name);                                        \
    EXPORT_SYMBOL(param_get_##name);                                        \
    EXPORT_SYMBOL(param_ops_##name)

STANDARD_PARAM_DEF(byte, unsigned char, "%c", unsigned long, strtoul);
STANDARD_PARAM_DEF(short, short, "%hi", long, strtol);
STANDARD_PARAM_DEF(ushort, unsigned short, "%hu", unsigned long, strtoul);
STANDARD_PARAM_DEF(int, int, "%i", long, strtol);
STANDARD_PARAM_DEF(uint, unsigned int, "%u", unsigned long, strtoul);
STANDARD_PARAM_DEF(long, long, "%li", long, strtol);
STANDARD_PARAM_DEF(ulong, unsigned long, "%lu", unsigned long, strtoul);


/* This just allows us to keep track of which parameters are malloced. */
struct malloced_param {
    struct list_head list;
    char val[];
};
static LIST_HEAD(malloced_params);

static void *malloc_parameter(unsigned int size)
{
    struct malloced_param *p;

    p = malloc(sizeof(*p) + size);
    if (!p)
        return NULL;

    list_add(&p->list, &malloced_params);
    return p->val;
}

/* Does nothing if parameter wasn't kmalloced above. */
static void maybe_free_parameter(void *param)
{
    struct malloced_param *p;

    list_for_each_entry(p, &malloced_params, list) {
        if (p->val == param) {
            printf("now free p =%p\n", p);
            list_del(&p->list);
            free(p);
            break;
        }
    }
}

/*
 * charp
 */
int param_set_charp(const char *val, const struct kernel_param *kp)
{
    maybe_free_parameter(*(char **)kp->arg);

    *(char **)kp->arg = malloc_parameter(strlen(val)+1);
    if (!*(char **)kp->arg)
        return -ENOMEM;

    strcpy(*(char **)kp->arg, val);

    return 0;
}
EXPORT_SYMBOL(param_set_charp);

int param_get_charp(char *buffer, const struct kernel_param *kp)
{
    int ret = 0;
    if (kp->ppos && !*kp->ppos) {
        ret = sprintf(buffer, "%s", *((char **)kp->arg));
        *kp->ppos += ret;
    }

    return ret;
}
EXPORT_SYMBOL(param_get_charp);

struct kernel_param_ops param_ops_charp = {
    .set = param_set_charp,
    .get = param_get_charp,
};
EXPORT_SYMBOL(param_ops_charp);

/*
 * string
 */
int param_set_copystring(const char *val, const struct kernel_param *kp)
{
    const struct kparam_string *kps = kp->str;

    if (strlen(val)+1 > kps->maxlen) {
        printf("%s: string doesn't fit in %u chars.\n",
               kp->name, kps->maxlen-1);
        return -ENOSPC;
    }
    strcpy(kps->string + *kp->ppos, val);
    *kp->ppos += strlen(val);

    return 0;
}
EXPORT_SYMBOL(param_set_copystring);

int param_get_string(char *buffer, const struct kernel_param *kp)
{
    const struct kparam_string *kps = kp->str;
    char *src_string = kps->string + *kp->ppos;
    int size = 0;
    int maxlen = kps->maxlen - *kp->ppos;

    /*
     * maxlen:     the string(valid) variable max len
     * kp->count:  the read buffer max size
     * src_string: the string to be copy
     * find the minimum size
     */
    size = (kp->count < maxlen) ? kp->count : maxlen;
    size = (size < strlen(src_string)) ? size : strlen(src_string);

    if (size < kp->count) {
        /* the string less the read buffer max size, append "\n" at string end  */
        strcpy(buffer, src_string);
        *kp->ppos += size;
    } else {
        /* copy to the read buffer max size */
        strncpy(buffer, src_string, size);
        *kp->ppos += size;
    }

    return size;
}
EXPORT_SYMBOL(param_get_string);

struct kernel_param_ops param_ops_string = {
    .set = param_set_copystring,
    .get = param_get_string,
};
EXPORT_SYMBOL(param_ops_string);

/*
 * array
 */
static int param_array(const char *name,
               const char *val,
               unsigned int min, unsigned int max,
               void *elem, int elemsize,
               int (*set)(const char *, const struct kernel_param *kp),
               s16 level,
               unsigned int *num)
{
    int ret;
    struct kernel_param kp;
    char save;

    /* Get the name right for errors. */
    kp.name = name;
    kp.arg = elem;
    kp.level = level;

    *num = 0;
    /* We expect a comma-separated list of values. */
    do {
        int len;

        if (*num == max) {
            printf("%s: can only take %i arguments\n", name, max);
            return -EINVAL;
        }
        len = strcspn(val, ",");

        /* nul-terminate and parse */
        save = val[len];
        ((char *)val)[len] = '\0';

        //BUG_ON(!mutex_is_locked(&param_lock));
        ret = set(val, &kp);
        if (ret != 0)
            return ret;

        kp.arg += elemsize;
        val += len+1;

        (*num)++;
    } while (save == ',');

    if (*num < min) {
        printf("%s: needs at least %i arguments\n", name, min);
        return -EINVAL;
    }

    return 0;
}

static int param_array_set(const char *val, const struct kernel_param *kp)
{
    const struct kparam_array *arr = kp->arr;
    unsigned int temp_num = 0;
    unsigned int *pnum;
    int ret = 0;

    pnum = arr->num ?: &temp_num;
    ret = param_array(kp->name, val, 1, arr->max, arr->elem,
               arr->elemsize, arr->ops->set, kp->level,
               pnum);

     return (ret < 0) ? ret : *pnum;
}
EXPORT_SYMBOL(param_array_set);

static int param_array_get(char *buffer, const struct kernel_param *kp)
{
    int elem_pos, ret;
    int elem_max_num;
    const struct kparam_array *arr = kp->arr;
    static struct kernel_param p;
    static off_t item_pos = 0;

    ret = 0;
    elem_pos = *kp->ppos;
    elem_max_num = arr->num ? *arr->num : arr->max;

    if (elem_pos < elem_max_num ) {
        /* Update element info */
        p.arg = arr->elem + arr->elemsize * elem_pos;
        p.count = kp->count;
        p.ppos = &item_pos;

        ret = arr->ops->get(buffer, &p);

        /* read one array item finish */
        if (ret == 0) {
            /* the last item no report separator : ',' */
            if (elem_pos < elem_max_num - 1) {
                buffer[0] = ',';
            } else {
                buffer[0] = '\0';
            }

            *kp->ppos += 1;
            item_pos = 0;
            ret = 1;
        }
    }

    return ret;
}
EXPORT_SYMBOL(param_array_get);


struct kernel_param_ops param_array_ops = {
    .set = param_array_set,
    .get = param_array_get,
};
EXPORT_SYMBOL(param_array_ops);


/*
 * sys fs module param
 */
#include <dfs_debugfs.h>

struct inode *module_sysfs;
int module_sysfs_initialized = 0;

extern unsigned int __param_start;
extern unsigned int __param_stop;
extern struct module *find_module_all(const char *name);
extern struct list_head *get_kernel_module_list(void);

static int param_sysfs_builtin(void)
{
    struct inode *param_mod;
    struct inode *param_inode;
    struct kernel_param *kp;

    if (!module_sysfs_initialized) {
        printf("module sysfs not initialized\n");
        return -EINVAL;
    }

    param_mod = dfs_debugfs_create_dir("buildin", module_sysfs);
    if (!param_mod) {
        printf("Create module buildin inode failed\n");
        return -ENOMEM;
    }

    for (kp = (struct kernel_param *)&__param_start;
            kp < (struct kernel_param *)&__param_stop;
            kp++) {
        param_inode = dfs_debugfs_create_module(kp->name, kp->perm, param_mod, kp);
        if (!param_inode) {
            printf("Create Moudle(%s) failed\n", kp->name);
        }
    }

    return 0;
}


/* You can use " around spaces, but can't escape ". */
/* Hyphens and underscores equivalent in parameter names. */
static int next_arg(char *args, char **param, char **val)
{
    unsigned int i, equals = 0;
    int in_quote = 0, quoted = 0;
    int ret = 0;

    if (*args == '"') {
        args++;
        in_quote = 1;
        quoted = 1;
    }

    for (i = 0; args[i]; i++) {
        if (isspace(args[i]) && !in_quote)
            break;
        if (equals == 0) {
            if (args[i] == '=')
                equals = i;
        }
        if (args[i] == '"')
            in_quote = !in_quote;
    }

    *param = args;
    if (!equals) {
        *val = NULL;
        ret = -EINVAL;
    } else {
        args[equals] = '\0';
        *val = args + equals + 1;

        /* Don't include quotes in value. */
        if (**val == '"') {
            (*val)++;
            if (args[i-1] == '"')
                args[i-1] = '\0';
        }
        if (quoted && args[i-1] == '"')
            args[i-1] = '\0';
    }

    if (args[i]) {
        args[i] = '\0';
    }

    /* Chew up trailing spaces. */
    return ret;
}

static int parse_arg_one(const char *doing, struct module *mod)
{
    int num_kernel_param = mod->num_kp;
    struct kernel_param *kp = mod->kp;
    char *param, *val;
    int ret = 0;
    int i = 0;

    ret = next_arg((char *)doing, &param, &val);
    if (ret < 0) {
        return ret;
    }

    /* find the matched param */
    for (i = 0; i < num_kernel_param; i++, kp++) {
        if (strcmp(kp->name, param) == 0) {
            kp->ops->set(val, kp);
            break;
        }
    }

    if (i >= num_kernel_param) {
        ret = -EINVAL;
        printf("not find param name = %s\n", param);
    }

    return ret;
}

/* Args looks like "foo=bar,bar2 baz=fuz". */
static int parse_args(int argc, const char **argv, struct module *mod)
{
    assert(mod != NULL);
    int i = 0;
    int ret = 0;

    for (i=0; i<argc; i++) {
        ret = parse_arg_one(argv[i], mod);
        if (ret < 0) {
            printf("parse_arg_one failed\n");
            return ret;
        }
    }

    return 0;
}

static int try_stop_module(struct module *mod)
{
    return 0;
}

static struct inode *find_inode_all(const char *name)
{
    struct list_head *pos;
    struct inode *inode;

    list_for_each(pos, &module_sysfs->l_child) {
        inode = list_entry(pos, struct inode, l_peer);
        if (inode) {
            /* walk parent node infomation */
            if (strcmp(inode->name, name) == 0) {
                return inode;
            }
        }
    } /* end of list_for_each(... */

    return NULL;
}


static struct module *module_in_list_dump_tree(struct list_head *base,
        int (*print_func)(const char *__restrict fmt, ...))
{
    struct list_head *pos;
    struct list_head *next;

    list_for_each_safe(pos, next, base) {
        struct module *module = list_entry(pos, struct module, link);
        if (module) {
            module_in_list_dump_tree(&module->list, print_func);

            /* base info */
            print_func("%-24s%d  ", module->name, module->core.size);
            print_func("%d", 0);
            print_func("\n");

        }
    }

    return NULL;
}


int delete_module(const char *name)
{
    int ret = 0;
    struct inode *module_dir;
    struct module *mod = find_module_all(name);
    if (!mod) {
        return -ENOENT;
    }

    ret = try_stop_module(mod);
    if (ret < 0) {
        return -EBUSY;
    }

    if (mod->exit) {
        mod->exit();
    }

    module_dir = find_inode_all(name);
    if (module_dir) {
        dfs_debugfs_remove_recursive(module_dir);
    }

    unload_module(mod);

    return 0;
}
EXPORT_SYMBOL(delete_module);

void list_show_module(int (*print_func)(const char *__restrict fmt, ...))
{
    struct list_head *root_list = get_kernel_module_list();

    print_func("%-24sSize  Used by\n", "Module");
    module_in_list_dump_tree(root_list, print_func);
}
EXPORT_SYMBOL(list_show_module);

int insert_module(void *data, unsigned long len,int argc, const char **argv)
{
    int ret;
    struct module *module_info;
    int i = 0;

    debug("init_module: mod=%p, len=%lu, args=%p\n", data, len, argv);
    debug("init_module: module parameter arg count = %d\n", argc);
    for (i=0; i<argc; i++) {
        debug("argv[%d]:%s\n", i, argv[i]);
    }

    module_info = load_module(data, len, 0);
    if (IS_ERR(module_info)) {
        return PTR_ERR(module_info);
    }


    /* Module is ready to execute: parsing args may do that. */
    ret = parse_args(argc, argv, module_info);
    if (ret < 0) {
        goto parse_args_error;
    }

    struct inode *module_dir_root;
    struct inode *module_dir_param;
    struct inode *module_param;
    struct inode *module_sec;
    struct kernel_param *kp;

    /* module directory */
    module_dir_root = dfs_debugfs_create_dir(module_info->name, module_sysfs);
    if (!module_dir_root) {
        printf("init module:Create module directory (%s) failed\n", module_info->name);
        ret = -ENOMEM;
        goto parse_args_error;
    }

    /* module parameter */
    module_dir_param = dfs_debugfs_create_dir("parameters", module_dir_root);
    if (!module_dir_param) {
        printf("init module:Create module parameter (%s) failed\n", module_info->name);
        ret = -ENOMEM;
        goto parse_args_error;
    }

    /* parameters */
    for (kp = (struct kernel_param *)module_info->kp, i = 0;
            i < module_info->num_kp;
            kp++, i++) {
        debug("%04d  name=%s perm=%o\n", i, kp->name, kp->perm);
        module_param = dfs_debugfs_create_module(kp->name, kp->perm, module_dir_param, kp);
        if (!module_param) {
            printf("init module:Create Moudle param(%s) failed\n", kp->name);
        }
    }

    /* sections info */
    module_sec = dfs_debugfs_create_u32("coresize", 0644, module_dir_root,  &module_info->core.size);
    if (!module_sec) {
        printf("init module:Create Moudle Section coresize failed\n");
    }

    /* module init */
    module_info->init();

    return 0;

parse_args_error:
    unload_module(module_info);
    return ret;
}
EXPORT_SYMBOL(insert_module);

int module_param_sysfs_init(void)
{
    kernel_module_init();

    module_sysfs = dfs_debugfs_create_dir("module", NULL);
    if (!module_sysfs) {
        printf("%s (%d): error creating module root inode\n", __FILE__, __LINE__);
        return -ENOMEM;
    }
    module_sysfs_initialized = 1;

    param_sysfs_builtin();

    return 0;
}
